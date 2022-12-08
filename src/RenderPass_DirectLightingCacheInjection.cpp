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
#include <RenderPass_DirectLightingCacheInjection.h>
#include <Utils.h>
#include <Log.h>
#include <Platform.h>
#include <PersistentWorkingSet.h>
#include <TaskWorkingSet.h>
#include <Scene.h>
#include <ShaderFactory.h>
#include <ShaderTableRT.h>
#include <RenderPass_Common.h>

#include <WinResFS.h>

#include <inttypes.h>
#include <cstring>

namespace KickstartRT_NativeLayer
{
	Status RenderPass_DirectLightingCacheInjection::Init(PersistentWorkingSet *pws, bool enableInlineRaytracing, bool enableShaderTableRaytracing)
	{
		GraphicsAPI::Device* dev = &pws->m_device;
		ShaderFactory::Factory *sf = pws->m_shaderFactory.get();
		(void)dev;
		(void)sf;

		m_enableInlineRaytracing = enableInlineRaytracing;
		m_enableShaderTableRaytracing = enableShaderTableRaytracing;

		// RootSig for Injection_rt_LIB / Injection_rt_CS
		{
			// set 1 [CB, SRV, SRV]
			{
				m_descTableLayout1.AddRange(GraphicsAPI::DescriptorHeap::Type::Cbv, 0, 1, 0); // b0, cb
				m_descTableLayout1.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 0, 1, 0); //t0, depth.
				m_descTableLayout1.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 1, 1, 0); //t1, lighting.
				m_descTableLayout1.SetAPIData(dev);
			}

			// set 2 [AS, UAV ...]
			{
				m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::AccelerationStructureSrv, 0, 1, 1); // t0, space1 TLAS
				m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 0, 1, 1); // u0, space1 TileTable
				m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 1, -pws->m_unboundDescTableUpperbound, 1); //u1 ~ space1, tileIndex, tileBuffer ... 40000 is the upper bound of the array in VK.
				m_descTableLayout2.SetAPIData(dev);
			}

			std::vector<GraphicsAPI::DescriptorTableLayout*> tableLayouts = { &m_descTableLayout1 , &m_descTableLayout2 };
			if (!m_rootSignature.Init(dev, tableLayouts)) {
				Log::Fatal(L"Failed to create rootSignature");
				return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
			}
			m_rootSignature.SetName(DebugName(L"RP_DirectLightingCacheInjection"));
		}

		// RootSig for Transfer_rt_LIB / Transfer_rt_CS
		{
			// set 1 [CB, SRV, SRV]
			{
				m_descTableLayoutTransfer1.AddRange(GraphicsAPI::DescriptorHeap::Type::Cbv, 0, 1, 0); // b0, cb
				m_descTableLayoutTransfer1.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 0, 1, 0); //u0, indexvertex.
				m_descTableLayoutTransfer1.SetAPIData(dev);
			}

			// set 2 [AS, UAV ...]
			{
				m_descTableLayoutTransfer2.AddRange(GraphicsAPI::DescriptorHeap::Type::AccelerationStructureSrv, 0, 1, 1); // t0, space1 TLAS
				m_descTableLayoutTransfer2.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 0, 1, 1); // u0, space1 TileTable
				m_descTableLayoutTransfer2.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 1, -pws->m_unboundDescTableUpperbound, 1); //u1 ~ space1, tileIndex, tileBuffer ... 40000 is the upper bound of the array in VK.
				m_descTableLayoutTransfer2.SetAPIData(dev);
			}

			std::vector<GraphicsAPI::DescriptorTableLayout*> tableLayouts = { &m_descTableLayoutTransfer1 , &m_descTableLayoutTransfer2 };
			if (!m_rootSignatureTransfer.Init(dev, tableLayouts)) {
				Log::Fatal(L"Failed to create rootSignature");
				return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
			}
			m_rootSignatureTransfer.SetName(DebugName(L"RP_DirectLightingCacheTransfer"));
		}

		{
			std::filesystem::path libPath(L"DirectLightingCache/Injection_rt_LIB.hlsl");
			std::filesystem::path csPath(L"DirectLightingCache/Injection_rt_CS.hlsl");
			std::filesystem::path csClearPath(L"DirectLightingCache/Injection_Clear_CS.hlsl");
			std::filesystem::path libTransferPath(L"DirectLightingCache/Transfer_rt_LIB.hlsl");
			std::filesystem::path csTransferPath(L"DirectLightingCache/Transfer_rt_CS.hlsl");

			auto RegisterShader = [&sf](
				const std::wstring& fileName, const std::wstring& entryName, const std::wstring& shaderName,
				ShaderFactory::ShaderType::Enum type, const std::vector<ShaderFactory::ShaderMacro>& shaderMacro, GraphicsAPI::RootSignature& rootSig)
				-> std::pair<Status, ShaderFactory::ShaderDictEntry*>
			{
				std::unique_ptr<ShaderFactory::ShaderDictEntry> dictEnt = std::make_unique< ShaderFactory::ShaderDictEntry>();
				dictEnt->m_fileName = fileName;
				dictEnt->m_entryName = entryName;
				dictEnt->m_shaderName = shaderName;
				dictEnt->m_type = type;
				dictEnt->m_shaderMacro_CRC = sf->GetShaderMacroCRC(&shaderMacro);
				dictEnt->m_rootSig = &rootSig;

				auto ofsSize = sf->FindShaderOffset(dictEnt->m_fileName.c_str(), dictEnt->m_entryName.c_str(), dictEnt->m_shaderMacro_CRC, dictEnt->m_type);
				if (!ofsSize.has_value()) {
					Log::Fatal(L"Failed to find a binary entry for shader:%s", dictEnt->m_fileName.c_str());
					return { Status::ERROR_FAILED_TO_INIT_RENDER_PASS, nullptr };
				}
				dictEnt->m_offset = ofsSize.value().first;
				dictEnt->m_size = ofsSize.value().second;

				dictEnt->CalcCRC();

				return sf->RegisterShader(std::move(dictEnt));
			};

			{
				std::vector<ShaderFactory::ShaderMacro> defines;

				{
					auto [sts, itr] = RegisterShader(csClearPath.wstring(), L"main", DebugName(L"RP_DirectLightingCacheInjection-Clear"),
						ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_pso_clear = itr;
				}
				if (m_enableInlineRaytracing)
				{
					auto [sts, itr] = RegisterShader(csPath.wstring(), L"main", DebugName(L"RP_DirectLightingCacheInjection"),
						ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_pso = itr;
				}
				{
					auto [sts, itr] = RegisterShader(libPath.wstring(), L"main", DebugName(L"RP_DirectLightingCacheInjection"),
						ShaderFactory::ShaderType::Enum::SHADER_RAY_GENERATION, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_shaderTable = itr;
				}
				if (m_enableInlineRaytracing)
				{
					auto [sts, itr] = RegisterShader(csTransferPath.wstring(), L"main", DebugName(L"RP_DirectLightingCacheTransfer"),
						ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, m_rootSignatureTransfer);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_psoTransfer = itr;
				}
				{
					auto [sts, itr] = RegisterShader(libTransferPath.wstring(), L"main", DebugName(L"RP_DirectLightingCacheTransfer"),
						ShaderFactory::ShaderType::Enum::SHADER_RAY_GENERATION, defines, m_rootSignatureTransfer);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_shaderTableTransfer = itr;
				}
			}
		}

		return Status::OK;
	};

	// need to set root sig and desc table #1 before calling this function.
	Status RenderPass_DirectLightingCacheInjection::DispatchInject(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList, RenderPass_ResourceRegistry* registry, const RenderTask::DirectLightingInjectionTask *input)
	{
		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);
		auto& dev(pws->m_device);

		if (input->useInlineRT && (!m_enableInlineRaytracing)) {
			Log::Fatal(L"Inline raytracing is disabled at the SDK initialization.");
			return Status::ERROR_INVALID_PARAM;
		}
		if ((!input->useInlineRT) && (!m_enableShaderTableRaytracing)) {
			Log::Fatal(L"ShaderTable raytracing is disabled at the SDK initialization.");
			return Status::ERROR_INVALID_PARAM;
		}

#if defined(GRAPHICS_API_D3D12)
		// Check input resource states.
		if (cmdList->HasDebugCommandList()) {
			constexpr D3D12_RESOURCE_STATES expectedState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			if (!Utils::CheckInputTextureState(cmdList, &input->depth.tex, GraphicsAPI::ResourceState::GetResourceState(expectedState))) {
				Log::Fatal(L"Invalid depth input texture's resource state detected in direct light injection pass. Expected resource is : %d", expectedState);
				return Status::ERROR_INVALID_PARAM;
			}
			if (! Utils::CheckInputTextureState(cmdList, &input->directLighting, GraphicsAPI::ResourceState::GetResourceState(expectedState))) {
				Log::Fatal(L"Invalid direct lighting input texture's resource state detected in direct light injection pass. Expected resource is : %d", expectedState);
				return Status::ERROR_INVALID_PARAM;
			}
		}
#endif

		ShaderTableRT* shaderTableRT = nullptr;
		if (input->useInlineRT) {
			// set PSO.
			cmdList->SetComputePipelineState(m_pso->GetCSPSO(pws));
		}
		else {
			// set rtPSO.
			shaderTableRT = m_shaderTable->GetShaderTableRT(pws, cmdList);
			cmdList->SetRayTracingPipelineState(shaderTableRT->m_rtPSO.get());
		}

		GraphicsAPI::DescriptorTable descTable;
		if (!descTable.Allocate(tws->m_CBVSRVUAVHeap.get(), &m_descTableLayout1)) {
			Log::Fatal(L"Faild to allocate a portion of desc heap.");
			return Status::ERROR_INTERNAL;
		}

		GraphicsAPI::ConstantBufferView cbv;
		void* cbPtrForWrite;
		RETURN_IF_STATUS_FAILED(tws->m_volatileConstantBuffer.Allocate(sizeof(CB), &cbv, &cbPtrForWrite));

		const uint32_t threadCountX = GraphicsAPI::ROUND_UP(input->viewport.width, input->injectionResolutionStride);
		const uint32_t threadCountY = GraphicsAPI::ROUND_UP(input->viewport.height, input->injectionResolutionStride);

		CB cb = {};
		cb.m_Viewport_TopLeftX = input->viewport.topLeftX;
		cb.m_Viewport_TopLeftY = input->viewport.topLeftY;
		cb.m_Viewport_Width = input->viewport.width;
		cb.m_Viewport_Height = input->viewport.height;
		cb.m_Viewport_MinDepth = input->viewport.minDepth;
		cb.m_Viewport_MaxDepth = input->viewport.maxDepth;
		cb.m_CTA_Swizzle_GroupDimension_X = GraphicsAPI::ROUND_UP(threadCountX, m_threadDim_XY[0]);
		cb.m_CTA_Swizzle_GroupDimension_Y = GraphicsAPI::ROUND_UP(threadCountY, m_threadDim_XY[1]);

		{
			Math::Float_4 originf = Math::Transform(input->viewToWorldMatrix, { 0.f, 0.f, 0.f, 1.f });
			cb.m_rayOrigin[0] = originf.f[0] / originf.f[3];
			cb.m_rayOrigin[1] = originf.f[1] / originf.f[3];
			cb.m_rayOrigin[2] = originf.f[2] / originf.f[3];
		}
		cb.m_depthType = (uint32_t)input->depth.type;

		cb.m_averageWindow = std::clamp<float>(input->averageWindow, 1.f, 1.0e3);

		cb.m_subPixelJitterOffsetX = 0;
		cb.m_subPixelJitterOffsetY = 0;

		cb.m_strideX = input->injectionResolutionStride;
		cb.m_strideY = input->injectionResolutionStride;


		std::hash<uint64_t> hash_f;
		const size_t randomOffset	= hash_f(s_seed++);

		const size_t offsetCounts = cb.m_strideX * cb.m_strideY;
		const uint32_t randomOffsetIndex = (uint32_t)(randomOffset % (offsetCounts));

		cb.m_strideOffsetX = randomOffsetIndex % cb.m_strideX;
		cb.m_strideOffsetY = randomOffsetIndex / cb.m_strideX;

		cb.m_clipToViewMatrix = input->clipToViewMatrix;
		cb.m_viewToWorldMatrix = input->viewToWorldMatrix;

		memcpy(cbPtrForWrite, &cb, sizeof(cb));

		{
			descTable.SetCbv(&dev, 0, 0, &cbv); // Layout1: [0]
		}

		registry->TrackResource(input->depth.tex, GraphicsAPI::ResourceState::State::ShaderResource);
		registry->TrackResource(input->directLighting, GraphicsAPI::ResourceState::State::ShaderResource);

		RenderPass_ResourceStateTransition stateTransitions;

		{
			using SRV = GraphicsAPI::ShaderResourceView;

			std::unique_ptr<SRV> depthSrv = registry->GetSRV(input->depth.tex, stateTransitions, GraphicsAPI::ResourceState::State::ShaderResource);
			std::unique_ptr<SRV> directLightingSrv = registry->GetSRV(input->directLighting, stateTransitions,GraphicsAPI::ResourceState::State::ShaderResource);

			descTable.SetSrv(&dev, 1, 0, depthSrv.get()); //Layout1: [1]
			descTable.SetSrv(&dev, 2, 0, directLightingSrv.get());//Layout1: [2]

			pws->DeferredRelease(std::move(depthSrv));
			pws->DeferredRelease(std::move(directLightingSrv));
		}

		stateTransitions.Flush(cmdList);

		std::vector<GraphicsAPI::DescriptorTable*> tableArr{ &descTable };

		if (input->useInlineRT) {
			cmdList->SetComputeRootDescriptorTable(&m_rootSignature, 0, tableArr.data(), tableArr.size());
			cmdList->Dispatch(cb.m_CTA_Swizzle_GroupDimension_X, cb.m_CTA_Swizzle_GroupDimension_Y, 1);
		}
		else {
			// VK uses different binding point.
			cmdList->SetRayTracingRootDescriptorTable(&m_rootSignature, 0, tableArr.data(), tableArr.size());
			shaderTableRT->DispatchRays(cmdList, threadCountX, threadCountY);
		}

		return Status::OK;
	};

	// need to set root sig and desc table #1 before calling this function.
	Status RenderPass_DirectLightingCacheInjection::DispatchTransfer(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList, RenderPass_ResourceRegistry* , const RenderTask::DirectLightTransferTask* input, const TransferParams& params)
	{
		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);
		auto& dev(pws->m_device);

		if (input->useInlineRT && (!m_enableInlineRaytracing)) {
			Log::Fatal(L"Inline raytracing is disabled at the SDK initialization.");
			return Status::ERROR_INVALID_PARAM;
		}
		if ((!input->useInlineRT) && (!m_enableShaderTableRaytracing)) {
			Log::Fatal(L"ShaderTable raytracing is disabled at the SDK initialization.");
			return Status::ERROR_INVALID_PARAM;
		}

		ShaderTableRT* shaderTableRT = nullptr;
		if (input->useInlineRT) {
			// set PSO.
			cmdList->SetComputePipelineState(m_psoTransfer->GetCSPSO(pws));
		}
		else {
			// set rtPSO.
			shaderTableRT = m_shaderTableTransfer->GetShaderTableRT(pws, cmdList);
			cmdList->SetRayTracingPipelineState(shaderTableRT->m_rtPSO.get());
		}

		GraphicsAPI::DescriptorTable descTable;
		if (!descTable.Allocate(tws->m_CBVSRVUAVHeap.get(), &m_descTableLayoutTransfer1)) {
			Log::Fatal(L"Faild to allocate a portion of desc heap.");
			return Status::ERROR_INTERNAL;
		}

		GraphicsAPI::ConstantBufferView cbv;
		void* cbPtrForWrite;
		RETURN_IF_STATUS_FAILED(tws->m_volatileConstantBuffer.Allocate(sizeof(CB_Transfer), &cbv, &cbPtrForWrite));

		BVHTask::Instance* targetInstance = BVHTask::Instance::ToPtr(input->target);
		// BVHTask::Instance* sourceInstance = BVHTask::Instance::ToPtr(input->source);

		const uint32_t triangleCount = targetInstance->m_numberOfTiles;

		CB_Transfer cb = {};
		cb.m_triangleCount = triangleCount;
		cb.m_targetInstanceIndex = params.targetInstanceIndex;
		cb.m_dstVertexBufferOffsetIdx = (uint32_t)(targetInstance->m_geometry->m_vertexBufferOffsetInBytes / sizeof(uint32_t));
		cb.m_targetInstanceTransform = targetInstance->m_input.transform;

		memcpy(cbPtrForWrite, &cb, sizeof(cb));

		{
			descTable.SetCbv(&dev, 0, 0, &cbv); // Layout1: [0]
		}

		{
			descTable.SetUav(&dev, 1, 0, targetInstance->m_geometry->m_index_vertexBuffer->m_uav.get());
		}

		std::vector<GraphicsAPI::DescriptorTable*> tableArr{ &descTable };

		if (input->useInlineRT) {
			cmdList->SetComputeRootDescriptorTable(&m_rootSignatureTransfer, 0, tableArr.data(), tableArr.size());
			cmdList->Dispatch(GraphicsAPI::ROUND_UP(triangleCount, 128u), 1, 1);
		}
		else {
			// VK uses different binding point.
			cmdList->SetRayTracingRootDescriptorTable(&m_rootSignatureTransfer, 0, tableArr.data(), tableArr.size());
			shaderTableRT->DispatchRays(cmdList, triangleCount, 1);
		}

		return Status::OK;
	};

	// need to set root sig and desc table #1 before calling this function.
	Status RenderPass_DirectLightingCacheInjection::DispatchClear(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList, const RenderPass_DirectLightingCacheInjection::CB_clear& clCB)
	{
		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);
		auto& dev(pws->m_device);

		GraphicsAPI::DescriptorTable descTable;
		if (!descTable.Allocate(tws->m_CBVSRVUAVHeap.get(), &m_descTableLayout1)) {
			Log::Fatal(L"Faild to allocate a portion of desc heap.");
			return Status::ERROR_INTERNAL;
		}

		GraphicsAPI::ConstantBufferView cbv;
		void* cbPtrForWrite;
		RETURN_IF_STATUS_FAILED(tws->m_volatileConstantBuffer.Allocate(sizeof(CB_clear), &cbv, &cbPtrForWrite));

		memcpy(cbPtrForWrite, &clCB, sizeof(clCB));

		{
			descTable.SetCbv(&dev, 0, 0, &cbv); // Layout1: [0]
		}

		std::vector<GraphicsAPI::DescriptorTable*> tableArr{ &descTable };

		cmdList->SetComputeRootDescriptorTable(&m_rootSignature, 0, tableArr.data(), tableArr.size());

		// write 4 tiles / thread.
		uint32_t dimX = GraphicsAPI::ROUND_UP(clCB.m_numberOfTiles/4, 64u);

		cmdList->Dispatch(dimX, 1, 1);

		return Status::OK;
	};

	Status RenderPass_DirectLightingCacheInjection::BuildCommandListClear(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList,
		GraphicsAPI::DescriptorTable* lightingCache_descTable, const std::deque<RenderPass_DirectLightingCacheInjection::CB_clear>& clearList)
	{
		if (clearList.size() == 0)
			return Status::OK;

		{
			// need clear pass before light injections.
			GraphicsAPI::Utils::ScopedEventObject ev(cmdList, { 0, 128, 0 }, DebugName("RT Injection - Clear]"));

			cmdList->SetComputeRootSignature(&m_rootSignature);

			std::vector<GraphicsAPI::DescriptorTable*> tableArr{ lightingCache_descTable };
			cmdList->SetComputeRootDescriptorTable(&m_rootSignature, 1, tableArr.data(), (uint32_t)tableArr.size());

			cmdList->SetComputePipelineState(m_pso_clear->GetCSPSO(tws->m_persistentWorkingSet));

			for (auto&& clCB : clearList) {
				RETURN_IF_STATUS_FAILED(DispatchClear(tws, cmdList, clCB));
			}
		}

		return Status::OK;
	}

	Status RenderPass_DirectLightingCacheInjection::BuildCommandListInject(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList,
		RenderPass_ResourceRegistry* resources,
		GraphicsAPI::DescriptorTable* lightingCache_descTable,
		const RenderTask::DirectLightingInjectionTask *directLightingInjection)
	{
		cmdList->SetComputeRootSignature(&m_rootSignature);

		{
			std::vector<GraphicsAPI::DescriptorTable*> tableArr{ lightingCache_descTable };
			if (directLightingInjection->useInlineRT) {
				cmdList->SetComputeRootDescriptorTable(&m_rootSignature, 1, tableArr.data(), (uint32_t)tableArr.size());
			}
			else {
				cmdList->SetRayTracingRootDescriptorTable(&m_rootSignature, 1, tableArr.data(), (uint32_t)tableArr.size());
			}
		}

		{
			GraphicsAPI::Utils::ScopedEventObject ev(cmdList, { 0, 128, 0 }, DebugName("RT:DLC Injection"));

			RETURN_IF_STATUS_FAILED(DispatchInject(tws, cmdList, resources, directLightingInjection));
		}

		return Status::OK;
	}

	Status RenderPass_DirectLightingCacheInjection::BuildCommandListTransfer(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList,
		RenderPass_ResourceRegistry* resources,
		GraphicsAPI::DescriptorTable* lightingCache_descTable, const RenderTask::DirectLightTransferTask* input, const TransferParams& params)
	{
		if (input->useInlineRT && (!m_enableInlineRaytracing)) {
			Log::Fatal(L"Inline raytracing is required for light cache transfer but is disabled at the SDK initialization.");
			return Status::ERROR_INVALID_PARAM;
		}

		cmdList->SetComputeRootSignature(&m_rootSignatureTransfer);

		{
			std::vector<GraphicsAPI::DescriptorTable*> tableArr{ lightingCache_descTable };
			if (input->useInlineRT) {
				cmdList->SetComputeRootDescriptorTable(&m_rootSignatureTransfer, 1, tableArr.data(), (uint32_t)tableArr.size());
			}
			else {
				cmdList->SetRayTracingRootDescriptorTable(&m_rootSignatureTransfer, 1, tableArr.data(), (uint32_t)tableArr.size());
			}
		}

		{
			GraphicsAPI::Utils::ScopedEventObject ev(cmdList, { 0, 128, 0 }, DebugName("RT:DLC Injection"));

			RETURN_IF_STATUS_FAILED(DispatchTransfer(tws, cmdList, resources, input, params));
		}

		return Status::OK;
	}
};
