/*
* Copyright (c) 2022 NVIDIA CORPORATION. All rights reserved
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include <ShaderTableRT.h>

#include <memory>
#include <deque>
#include <cstring>

namespace KickstartRT_NativeLayer
{
	ShaderTableRT::~ShaderTableRT()
	{
		// do nothing specially.
	};

#if defined(GRAPHICS_API_D3D12)
	std::unique_ptr<ShaderTableRT> ShaderTableRT::Init(PersistentWorkingSet *pws, const GraphicsAPI::RootSignature* globalRootSig, std::shared_ptr<ShaderBlob::Blob::IBlob> blob)
	{
		// init rtPSO.
		std::unique_ptr<GraphicsAPI::RaytracingPipelineState> rtPSO = std::make_unique<decltype(rtPSO)::element_type>();
		{
			std::vector<D3D12_STATE_SUBOBJECT> stateSubobjects;
			stateSubobjects.reserve(16); // need to reserve to perserve address of each element, to refer from other element by its address.
			auto AddSubobject = [&](D3D12_STATE_SUBOBJECT_TYPE type, const void* desc)
			{
				D3D12_STATE_SUBOBJECT sub;
				sub.Type = type;
				sub.pDesc = desc;
				stateSubobjects.push_back(sub);
			};

			// shader exports.
			std::array<D3D12_EXPORT_DESC, 3> libExports = { {
				{ L"RayGen", nullptr, D3D12_EXPORT_FLAG_NONE },
				{ L"ClosestHit",	nullptr, D3D12_EXPORT_FLAG_NONE },
				{ L"Miss",	nullptr, D3D12_EXPORT_FLAG_NONE }
			} };

			D3D12_DXIL_LIBRARY_DESC libDesc{};
			libDesc.DXILLibrary.pShaderBytecode = blob->data();
			libDesc.DXILLibrary.BytecodeLength = blob->size();
			libDesc.NumExports = (UINT)libExports.size();
			libDesc.pExports = libExports.data();
			AddSubobject(D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &libDesc);

			// hit group. There is only one hit group ther in this rtPSO.
			D3D12_HIT_GROUP_DESC hitGroupDesc{};
			hitGroupDesc.HitGroupExport = L"HitGroup";
			hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
			AddSubobject(D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroupDesc);

			// shader exports.
			std::array<const wchar_t*, 3> shaderExports = {
				L"RayGen",
				L"Miss",
				L"HitGroup"
			};

			// shader config.
			D3D12_RAYTRACING_SHADER_CONFIG shCnfDesc{};
			shCnfDesc.MaxPayloadSizeInBytes = sizeof(float) * 4;		// float3: color, float: hitT
			shCnfDesc.MaxAttributeSizeInBytes = sizeof(float) * 2;	// float2 barycentrics
			AddSubobject(D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shCnfDesc);

			// Exports assosiation with shader and shader config.
			D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION asDesc{};
			asDesc.pSubobjectToAssociate = &stateSubobjects.back();
			asDesc.NumExports = (UINT)shaderExports.size();
			asDesc.pExports = shaderExports.data();
			AddSubobject(D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION, &asDesc);

			// global root sig.
			D3D12_GLOBAL_ROOT_SIGNATURE	rsDesc = {};
			rsDesc.pGlobalRootSignature = globalRootSig->m_apiData.m_rootSignature;
			AddSubobject(D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &rsDesc);

			// pipeline config.
			D3D12_RAYTRACING_PIPELINE_CONFIG pipelineCngDesc{};
			pipelineCngDesc.MaxTraceRecursionDepth = 1;
			AddSubobject(D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipelineCngDesc);

			// create rtPSO
			D3D12_STATE_OBJECT_DESC psoDesc{};
			psoDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
			psoDesc.NumSubobjects = (UINT)stateSubobjects.size();
			psoDesc.pSubobjects = stateSubobjects.data();

			if (FAILED(pws->m_device.m_apiData.m_device->CreateStateObject(&psoDesc, IID_PPV_ARGS(&rtPSO->m_apiData.m_rtPSO)))) {
				Log::Fatal(L"Failed to create rtPSO.");
				return std::unique_ptr<ShaderTableRT>();
			}
		}

		// init shader table.
		std::unique_ptr<GraphicsAPI::Buffer>		uploadBuf;
		std::unique_ptr<GraphicsAPI::Buffer>		deviceBuf;
		uint64_t shaderRecordSizeInBytes = 0;
		{
			Microsoft::WRL::ComPtr<ID3D12StateObjectProperties>		psoProps;
			rtPSO->m_apiData.m_rtPSO->QueryInterface(IID_PPV_ARGS(&psoProps));

			if (psoProps.Get() == nullptr) {
				Log::Fatal(L"Failed to query interface.");
				return std::unique_ptr<ShaderTableRT>();
			};

			void* RG_ID = psoProps->GetShaderIdentifier(L"RayGen");
			void* MS_ID = psoProps->GetShaderIdentifier(L"Miss");
			void* HG_ID = psoProps->GetShaderIdentifier(L"HitGroup");

			// shader record size with shader table alignment
			shaderRecordSizeInBytes = (uint64_t)GraphicsAPI::ALIGN(D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, (D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 0));

			uploadBuf = pws->CreateBufferResource(shaderRecordSizeInBytes * 3, GraphicsAPI::Resource::Format::Unknown,
				GraphicsAPI::Resource::BindFlags::None, GraphicsAPI::Buffer::CpuAccess::Write, ResourceLogger::ResourceKind::e_Other);
			if (!uploadBuf) {
				Log::Fatal(L"Faild to create upload buffer resource.");
				return std::unique_ptr<ShaderTableRT>();
			}
			// create Device buffer.
			deviceBuf = pws->CreateBufferResource(shaderRecordSizeInBytes * 3, GraphicsAPI::Resource::Format::Unknown,
				GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress | GraphicsAPI::Resource::BindFlags::UnorderedAccess, GraphicsAPI::Buffer::CpuAccess::None, ResourceLogger::ResourceKind::e_Other);
			if (!deviceBuf) {
				Log::Fatal(L"Faild to create device buffer resource.");
				return std::unique_ptr<ShaderTableRT>();
			}

			{
				intptr_t mappedPtr = reinterpret_cast<intptr_t>(uploadBuf->Map(&pws->m_device, GraphicsAPI::Buffer::MapType::WriteDiscard, 0, 0, 0));

				memcpy(reinterpret_cast<void*>(mappedPtr + shaderRecordSizeInBytes * 0), RG_ID, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
				memcpy(reinterpret_cast<void*>(mappedPtr + shaderRecordSizeInBytes * 1), MS_ID, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
				memcpy(reinterpret_cast<void*>(mappedPtr + shaderRecordSizeInBytes * 2), HG_ID, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);

				uploadBuf->Unmap(&pws->m_device, 0, 0, shaderRecordSizeInBytes * 3);
			}
		}

		std::unique_ptr<ShaderTableRT> st = std::make_unique<ShaderTableRT>();

		st->m_rtPSO = std::move(rtPSO);
		st->m_uploadBuf = std::move(uploadBuf);
		st->m_deviceBuf = std::move(deviceBuf);

		// need to copy the upload buffer when the first use.
		st->m_needToCoyBuffer = true;

		st->m_RG_Addr.StartAddress = st->m_deviceBuf->m_apiData.m_resource->GetGPUVirtualAddress();
		st->m_RG_Addr.SizeInBytes = shaderRecordSizeInBytes;

		st->m_MS_Addr.StartAddress = st->m_RG_Addr.StartAddress + shaderRecordSizeInBytes * 1;
		st->m_MS_Addr.StrideInBytes = shaderRecordSizeInBytes;
		st->m_MS_Addr.SizeInBytes = shaderRecordSizeInBytes;

		st->m_HG_Addr.StartAddress = st->m_RG_Addr.StartAddress + shaderRecordSizeInBytes * 2;
		st->m_HG_Addr.StrideInBytes = shaderRecordSizeInBytes;
		st->m_HG_Addr.SizeInBytes = shaderRecordSizeInBytes;

		return std::move(st);
	}

	void ShaderTableRT::DispatchRays(GraphicsAPI::CommandList *cmdList, uint32_t width, uint32_t height)
	{
		D3D12_DISPATCH_RAYS_DESC rDesc = {};

		rDesc.RayGenerationShaderRecord = m_RG_Addr;
		rDesc.MissShaderTable = m_MS_Addr;
		rDesc.HitGroupTable = m_HG_Addr;
		rDesc.Width = width;
		rDesc.Height = height;
		rDesc.Depth = 1;

		cmdList->m_apiData.m_commandList->DispatchRays(&rDesc);
	}

#elif defined(GRAPHICS_API_VK)
	std::unique_ptr<ShaderTableRT> ShaderTableRT::Init(PersistentWorkingSet* pws, const GraphicsAPI::RootSignature* globalRootSig, std::shared_ptr<ShaderBlob::Blob::IBlob> blob)
	{
		// init rtPSO.
		std::unique_ptr<GraphicsAPI::RaytracingPipelineState> rtPSO = std::make_unique<decltype(rtPSO)::element_type>();
		{
			enum StageIndices : uint32_t
			{
				eRaygen = 0,
				eMiss,
				eClosestHit,
				eShaderGroupCount
			};

			{
				VkShaderModuleCreateInfo createInfo = {};
				createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
				createInfo.codeSize = blob->size();
				createInfo.pCode = (uint32_t*)blob->data();

				if (vkCreateShaderModule(pws->m_device.m_apiData.m_device, &createInfo, nullptr, &rtPSO->m_apiData.m_module) != VK_SUCCESS)
				{
					Log::Fatal(L"Faild to create shader module.");
					return std::unique_ptr<ShaderTableRT>();
				}
			}

			// All stages
			std::array<VkPipelineShaderStageCreateInfo, eShaderGroupCount> stages{};
			{
				VkPipelineShaderStageCreateInfo stage = {};
				stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				stage.module = rtPSO->m_apiData.m_module;

				// Raygen
				stage.pName = "RayGen";
				stage.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
				stages[eRaygen] = stage;

				// Miss
				stage.pName = "Miss";
				stage.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
				stages[eMiss] = stage;

				// Hit Group - Closest Hit
				stage.pName = "ClosestHit";
				stage.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
				stages[eClosestHit] = stage;
			}

			// Shader groups
			std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> groups;
			{
				VkRayTracingShaderGroupCreateInfoKHR              group = {};
				group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;

				group.anyHitShader = VK_SHADER_UNUSED_KHR;
				group.closestHitShader = VK_SHADER_UNUSED_KHR;
				group.generalShader = VK_SHADER_UNUSED_KHR;
				group.intersectionShader = VK_SHADER_UNUSED_KHR;

				// 0 - Raygen
				group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
				group.generalShader = eRaygen;
				groups[0] = group;

				// 1 - Miss
				group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
				group.generalShader = eMiss;
				groups[1] = group;

				// 2 - Hit group. (closest hit shader)
				group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
				group.generalShader = VK_SHADER_UNUSED_KHR;
				group.closestHitShader = eClosestHit;
				groups[2] = group;
			}

			{
				VkRayTracingPipelineCreateInfoKHR rayPipelineInfo = {};
				rayPipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;

				rayPipelineInfo.stageCount = (uint32_t)stages.size();
				rayPipelineInfo.pStages = stages.data();

				rayPipelineInfo.groupCount = (uint32_t)groups.size();
				rayPipelineInfo.pGroups = groups.data();

				rayPipelineInfo.maxPipelineRayRecursionDepth = 1;  // Ray depth

				rayPipelineInfo.layout = globalRootSig->m_apiData.m_pipelineLayout; // global root sig.

				// Create a deferred operation (compiling in parallel)
				if (GraphicsAPI::VK::vkCreateRayTracingPipelinesKHR(pws->m_device.m_apiData.m_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayPipelineInfo, nullptr, &rtPSO->m_apiData.m_pipeline) != VK_SUCCESS) {
					Log::Fatal(L"Faild to create shader module.");
					return std::unique_ptr<ShaderTableRT>();
				}
			}
			rtPSO->m_apiData.m_device = pws->m_device.m_apiData.m_device;
		}

		// init shader table.
		std::unique_ptr<GraphicsAPI::Buffer>		uploadBuf;
		std::unique_ptr<GraphicsAPI::Buffer>		deviceBuf;
		uint32_t sbtStride = 0;
		{
			uint32_t shaderGroupHandleSize = 0;
			uint32_t shaderGroupBaseAlignment = 0;
			{
				VkPhysicalDeviceProperties2                     properties = {};
				VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties = {};
				properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
				rtProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
				properties.pNext = &rtProperties;

				vkGetPhysicalDeviceProperties2(pws->m_device.m_apiData.m_physicalDevice, &properties);

				shaderGroupHandleSize = rtProperties.shaderGroupHandleSize;
				shaderGroupBaseAlignment = rtProperties.shaderGroupBaseAlignment;
			}

			// Fetch all the shader handles used in the pipeline, so that they can be written in the SBT
			constexpr uint32_t totalGroupCount = 3;
			uint32_t  sbtSize = totalGroupCount * shaderGroupHandleSize;
			std::vector<uint8_t> shaderHandleStorage(sbtSize);

			if (GraphicsAPI::VK::vkGetRayTracingShaderGroupHandlesKHR(pws->m_device.m_apiData.m_device, rtPSO->m_apiData.m_pipeline, 0, totalGroupCount, sbtSize, shaderHandleStorage.data()) != VK_SUCCESS) {
				Log::Fatal(L"Faild to get shader group handles.");
				return std::unique_ptr<ShaderTableRT>();
			}

			sbtStride = GraphicsAPI::ALIGN(shaderGroupBaseAlignment, shaderGroupHandleSize);

			std::vector<uint8_t>		sbtData(sbtStride * 4, 0); // RG, MS, HitGroup, Callable(All null);

			// white shader identifier to SBT table.
			for (size_t i = 0; i < 3; ++i) {
				memcpy(sbtData.data() + sbtStride * i, shaderHandleStorage.data() + shaderGroupHandleSize * i, shaderGroupHandleSize);
			}

			// create SBT upload buffer.
			uploadBuf = pws->CreateBufferResource(sbtData.size(), GraphicsAPI::Resource::Format::Unknown,
				GraphicsAPI::Resource::BindFlags::None, GraphicsAPI::Buffer::CpuAccess::Write, ResourceLogger::ResourceKind::e_Other);
			if (!uploadBuf) {
				Log::Fatal(L"Faild to create upload buffer resource.");
				return std::unique_ptr<ShaderTableRT>();
			}
			{
				void* mappedPtr = uploadBuf->Map(&pws->m_device, GraphicsAPI::Buffer::MapType::WriteDiscard, 0, 0, 0);
				memcpy(mappedPtr, sbtData.data(), sbtData.size());
				uploadBuf->Unmap(&pws->m_device, 0, 0, sbtData.size());
			}
			// create Device buffer.
			deviceBuf = pws->CreateBufferResource(sbtData.size(), GraphicsAPI::Resource::Format::Unknown,
				GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress | GraphicsAPI::Resource::BindFlags::UnorderedAccess, GraphicsAPI::Buffer::CpuAccess::None, ResourceLogger::ResourceKind::e_Other);
			if (!deviceBuf) {
				Log::Fatal(L"Faild to create device buffer resource.");
				return std::unique_ptr<ShaderTableRT>();
			}
		}

		std::unique_ptr<ShaderTableRT> st = std::make_unique<ShaderTableRT>();

		st->m_rtPSO = std::move(rtPSO);
		st->m_uploadBuf = std::move(uploadBuf);
		st->m_deviceBuf = std::move(deviceBuf);

		// need to copy the upload buffer when the first use.
		st->m_needToCoyBuffer = true;

		st->m_RG_Addr = {};
		st->m_RG_Addr.deviceAddress = st->m_deviceBuf->m_apiData.m_deviceAddress;
		st->m_RG_Addr.stride = sbtStride;
		st->m_RG_Addr.size = sbtStride * 1;

		st->m_MS_Addr = st->m_RG_Addr;
		st->m_MS_Addr.deviceAddress += (uint64_t)sbtStride;

		st->m_HG_Addr = st->m_MS_Addr;
		st->m_HG_Addr.deviceAddress += (uint64_t)sbtStride;

		st->m_CL_Addr = st->m_HG_Addr;
		st->m_CL_Addr.deviceAddress += (uint64_t)sbtStride;

		return std::move(st);
	}

	void ShaderTableRT::DispatchRays(GraphicsAPI::CommandList* cmdList, uint32_t width, uint32_t height)
	{
		GraphicsAPI::VK::vkCmdTraceRaysKHR(cmdList->m_apiData.m_commandBuffer,
			&m_RG_Addr,
			&m_MS_Addr,
			&m_HG_Addr,
			&m_CL_Addr,
			width, height, 1);
	}

#endif

	Status ShaderTableRT::BatchCopy(GraphicsAPI::CommandList *cmdList, std::vector<ShaderTableRT*> stArr)
	{
		std::vector<GraphicsAPI::Resource*>			dstBufArr;
		std::vector<GraphicsAPI::ResourceState::State>	beforeStateArr;
		std::vector<GraphicsAPI::ResourceState::State>	stateArr;

		auto beforeCpy = [&](ShaderTableRT* sbt) {
			dstBufArr.push_back(sbt->m_deviceBuf.get());
			beforeStateArr.push_back(GraphicsAPI::ResourceState::State::CopyDest);
			stateArr.push_back(GraphicsAPI::ResourceState::State::NonPixelShader);
		};

		auto cpy = [&](ShaderTableRT* sbt) {
			cmdList->CopyBufferRegion(
				sbt->m_deviceBuf.get(), 0,
				sbt->m_uploadBuf.get(), 0,
				sbt->m_uploadBuf->m_sizeInBytes);
			sbt->m_needToCoyBuffer = false;
		};

		for (auto&& s : stArr) {
			beforeCpy(s);
		}

		if (!cmdList->ResourceTransitionBarrier(&dstBufArr[0], dstBufArr.size(), &beforeStateArr[0])) {
			Log::Fatal(L"Failed ResourceTransitionBarrier.");
			return Status::ERROR_INTERNAL;
		}

		for (auto&& s : stArr) {
			cpy(s);
		}

		if (!cmdList->ResourceTransitionBarrier(&dstBufArr[0], dstBufArr.size(), &stateArr[0])) {
			Log::Fatal(L"Failed ResourceTransitionBarrier.");
			return Status::ERROR_INTERNAL;
		}

		for (auto&& s : stArr) {
			s->m_needToCoyBuffer = false;
		}

		return Status::OK;
	}
};
