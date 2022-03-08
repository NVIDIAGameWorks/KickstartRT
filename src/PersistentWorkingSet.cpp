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
#include <PersistentWorkingSet.h>
#include <Utils.h>
#include <Log.h>
#include <TaskWorkingSet.h>
#include <WinResFS.h>
#include <ShaderFactory.h>

#include <cstring>
#include <set>
#include <filesystem>
#include <fstream>

#include <RenderPass_DirectLightingCacheAllocation.h>
#include <RenderPass_DirectLightingCacheInjection.h>
#include <RenderPass_DirectLightingCacheReflection.h>
#include <RenderPass_DirectLightingCacheDenoising.h>

namespace KickstartRT_NativeLayer
{
	using ClassifiedTexture = ClassifiedDeviceObject<GraphicsAPI::Texture>;
	using ClassifiedBuffer = ClassifiedDeviceObject<GraphicsAPI::Buffer>;

	PersistentWorkingSet::PersistentWorkingSet(const GraphicsAPI::Device::ApiData& apiData)
	{
		if (!m_device.CreateFromApiData(apiData)) {
			Log::Fatal(L"Faild to initialize PersistentWorkingSet");
		}
	};

	PersistentWorkingSet::~PersistentWorkingSet()
	{
		std::scoped_lock mtx(m_mutex);

		m_winResFileSystem.reset();

		// release all device objects anyway.
		m_resourceLogger.ReleaseDeferredReleasedDeviceObjects(0xFFFF'FFFF'FFFF'FFFF);

		m_RP_DirectLightingCacheAllocation.reset();
		m_RP_DirectLightingCacheInjection.reset();
		m_RP_DirectLightingCacheReflection.reset();

		m_shaderFactory.reset();

		m_bufferForZeroClear.reset();

		m_zeroBufferUAV.reset();
		m_upBufferForZeroView.reset();
		m_bufferForZeroView.reset();

		m_nullBufferUAV.reset();
		m_nullBufferSRV.reset();
		m_bufferForNullView.reset();

		m_nullTexture2DUAV.reset();
		m_nullTexture2DSRV.reset();
		m_texture2DForNullUAView.reset();
		m_texture2DForNullSRView.reset();

		m_sharedBufferForDirectLightingCache.reset();
		m_sharedBufferForDirectLightingCacheTemp.reset();

		m_sharedBufferForVertexTemporal.reset();
		m_sharedBufferForVertexPersistent.reset();

		m_sharedBufferForReadback.reset();
		m_sharedBufferForCounter.reset();

		m_sharedBufferForBLASTemporal.reset();
		m_sharedBufferForBLASPermanent.reset();
		m_sharedBufferForBLASScratchTemporal.reset();
		m_sharedBufferForBLASScratchPermanent.reset();

		m_UAVCPUDescHeap1.reset();
		m_UAVCPUDescHeap2.reset();

		m_resourceLogger.CheckLeaks();
	};

	Status PersistentWorkingSet::Init(const ExecuteContext_InitSettings* initSettings)
	{
		std::scoped_lock mtx(m_mutex);

		m_winResFileSystem = std::make_shared<VirtualFS::WinResFileSystem>();
		std::filesystem::path		basePath;
		m_shaderFactory = std::make_unique<ShaderFactory::Factory>(m_winResFileSystem, basePath, initSettings->coldLoadShaderList, initSettings->coldLoadShaderListSize);

		std::unique_ptr<RenderPass_DirectLightingCacheAllocation> directLightingCacheAllocation = std::make_unique<RenderPass_DirectLightingCacheAllocation>();
		std::unique_ptr<RenderPass_DirectLightingCacheInjection> directLightingCacheInjection = std::make_unique<RenderPass_DirectLightingCacheInjection>();
		std::unique_ptr<RenderPass_DirectLightingCacheReflection> directLightingCacheReflection = std::make_unique<RenderPass_DirectLightingCacheReflection>();

		RETURN_IF_STATUS_FAILED(directLightingCacheAllocation->Init(&m_device, m_shaderFactory.get()));
		RETURN_IF_STATUS_FAILED(directLightingCacheInjection->Init(this, initSettings->useInlineRaytracing, initSettings->useShaderTableRaytracing));
		RETURN_IF_STATUS_FAILED(directLightingCacheReflection->Init(this, initSettings->useInlineRaytracing, initSettings->useShaderTableRaytracing));

		m_RP_DirectLightingCacheAllocation = std::move(directLightingCacheAllocation);
		m_RP_DirectLightingCacheInjection = std::move(directLightingCacheInjection);
		m_RP_DirectLightingCacheReflection = std::move(directLightingCacheReflection);

		m_UAVCPUDescHeap1 = std::make_unique<SharedCPUDescriptorHeap>();
		RETURN_IF_STATUS_FAILED(m_UAVCPUDescHeap1->Init(&m_device, GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 1, initSettings->descHeapSize / 4)); // various usage.

		m_UAVCPUDescHeap2 = std::make_unique<SharedCPUDescriptorHeap>();
		RETURN_IF_STATUS_FAILED(m_UAVCPUDescHeap2->Init(&m_device, GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 2, initSettings->descHeapSize / 4)); // a set of [tileIdx, tile]

		m_sharedBufferForDirectLightingCache = std::make_unique<decltype(m_sharedBufferForDirectLightingCache)::element_type>();
		m_sharedBufferForDirectLightingCache->Init(
			&m_device,
			256, // 256 Bytes
			true, // useUAV
			true, // useGpuPtr
			16 * 1024 * 1024, // blockSize 16MB
			GraphicsAPI::Buffer::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress | GraphicsAPI::Resource::BindFlags::AllowShaderAtomics, GraphicsAPI::Buffer::CpuAccess::None,
			ResourceLogger::ResourceKind::e_DirectLightingCache_SharedBlock, ResourceLogger::ResourceKind::e_DirectLightingCache_SharedEntry,
			L"SharedBufferForDirectLightingCache");

		m_sharedBufferForDirectLightingCacheTemp = std::make_unique<decltype(m_sharedBufferForDirectLightingCacheTemp)::element_type>();
		m_sharedBufferForDirectLightingCacheTemp->Init(
			&m_device,
			256, // 256 Bytes
			true, // useUAV
			true, // useGpuPtr
			16 * 1024 * 1024, // blockSize 16MB
			GraphicsAPI::Buffer::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress | GraphicsAPI::Resource::BindFlags::AllowShaderAtomics, GraphicsAPI::Buffer::CpuAccess::None,
			ResourceLogger::ResourceKind::e_DirectLightingCacheTemp_SharedBlock, ResourceLogger::ResourceKind::e_DirectLightingCacheTemp_SharedEntry,
			L"m_sharedBufferForDirectLightingCacheTemp");

		m_sharedBufferForVertexTemporal = std::make_unique<decltype(m_sharedBufferForVertexTemporal)::element_type>();
		m_sharedBufferForVertexTemporal->Init(
			&m_device,
			256, // 256 Bytes
			true, // useUAV
			true, // useGpuPtr
			8 * 1024 * 1024, // blockSize 8MB
			GraphicsAPI::Buffer::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress, GraphicsAPI::Buffer::CpuAccess::None,
			ResourceLogger::ResourceKind::e_VertexTemporay_SharedBlock, ResourceLogger::ResourceKind::e_VertexTemporay_SharedEntry,
			L"SharedBufferForVertexTemp");

		m_sharedBufferForVertexPersistent = std::make_unique<decltype(m_sharedBufferForVertexTemporal)::element_type>();
		m_sharedBufferForVertexPersistent->Init(
			&m_device,
			256, // 256 Bytes
			true, // useUAV
			true, // useGpuPtr
			4 * 1024 * 1024, // blockSize 4MB
			GraphicsAPI::Buffer::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress, GraphicsAPI::Buffer::CpuAccess::None,
			ResourceLogger::ResourceKind::e_VertexPersistent_SharedBlock, ResourceLogger::ResourceKind::e_VertexPersistent_SharedEntry,
			L"SharedBufferForVertexPers");

		m_sharedBufferForReadback = std::make_unique<decltype(m_sharedBufferForReadback)::element_type>();
		m_sharedBufferForReadback->Init(
			&m_device,
			sizeof(uint32_t) * 4, //allocation alignment size
			false, // useUAV
			false, // useGpuPtr
			256 * 1024, // blockSize 256KB
			GraphicsAPI::Buffer::Format::R32Uint, GraphicsAPI::Resource::BindFlags::None, GraphicsAPI::Buffer::CpuAccess::Read,
			ResourceLogger::ResourceKind::e_Readback_SharedBlock, ResourceLogger::ResourceKind::e_Readback_SharedEntry,
			L"SharedBufferForReadbacks");

		m_sharedBufferForCounter = std::make_unique<decltype(m_sharedBufferForCounter)::element_type>();
		m_sharedBufferForCounter->Init(
			&m_device,
			sizeof(uint32_t) * 4, //allocation alignment size
			true, // useClear
			true, // useGpuPtr
			256 * 1024, // blockSize 256KB
			GraphicsAPI::Buffer::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress, GraphicsAPI::Buffer::CpuAccess::None,
			ResourceLogger::ResourceKind::e_Counter_SharedBlock, ResourceLogger::ResourceKind::e_Counter_SharedEntry,
			L"SharedBufferForCounter");

		{
			std::array<std::add_pointer<decltype(m_sharedBufferForBLASTemporal)>::type, 2> bArr =
			{ &m_sharedBufferForBLASTemporal , &m_sharedBufferForBLASPermanent };
			std::array<std::wstring, 2> nArr =
			{ L"BLASTemporal", L"BLASPermanent" };
			std::array<std::pair<ResourceLogger::ResourceKind, ResourceLogger::ResourceKind>, 2> kArr =
			{ std::make_pair(ResourceLogger::ResourceKind::e_BLASSTemporary_SharedBlock, ResourceLogger::ResourceKind::e_BLASSTemporary_SharedEntry),
			  std::make_pair(ResourceLogger::ResourceKind::e_BLASSPermanent_SharedBlock, ResourceLogger::ResourceKind::e_BLASSPermanent_SharedEntry)};
			for (size_t i = 0; i < bArr.size(); ++i){
				(*bArr[i]) = std::make_unique<decltype(m_sharedBufferForBLASTemporal)::element_type>();
				(*bArr[i])->Init(
					&m_device,
					256,	// AS allocation alignment
					false,	// useClear
					true,	// useGpuPtr
					32 * 1024 * 1024, // blockSize 32M Bytes.
					GraphicsAPI::Buffer::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress | GraphicsAPI::Resource::BindFlags::AccelerationStructure, GraphicsAPI::Buffer::CpuAccess::None,
					kArr[i].first, kArr[i].second,
					nArr[i]);
			}
		}
		{
			std::array<std::add_pointer<decltype(m_sharedBufferForBLASScratchTemporal)>::type, 2> bArr =
			{ &m_sharedBufferForBLASScratchTemporal, &m_sharedBufferForBLASScratchPermanent };
			std::array<std::wstring, 2> nArr =
			{ L"BLASScratchTemporal", L"BLASScratchPermanent" };
			std::array<std::pair<ResourceLogger::ResourceKind, ResourceLogger::ResourceKind>, 2> kArr =
			{ std::make_pair(ResourceLogger::ResourceKind::e_BLASScratchTemp_SharedBlock, ResourceLogger::ResourceKind::e_BLASScratchTemp_SharedEntry),
			  std::make_pair(ResourceLogger::ResourceKind::e_BLASScratchPerm_SharedBlock, ResourceLogger::ResourceKind::e_BLASScratchPerm_SharedEntry)};
			for (size_t i = 0; i < bArr.size(); ++i) {
				(*bArr[i]) = std::make_unique<decltype(m_sharedBufferForBLASTemporal)::element_type>();
				(*bArr[i])->Init(
					&m_device,
					256,	// AS allocation alignment
					false,	// useClear
					true,	// useGpuPtr
					8 * 1024 * 1024, // blockSize 8M Bytes.
					GraphicsAPI::Buffer::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress, GraphicsAPI::Buffer::CpuAccess::None,
					kArr[i].first, kArr[i].second,
					nArr[i]);
			}
		}

		m_upBufferForZeroView = std::make_unique<GraphicsAPI::Buffer>();
		m_upBufferForZeroView->Create(&m_device, 32, GraphicsAPI::Resource::Format::R32Uint, GraphicsAPI::Resource::BindFlags::None, GraphicsAPI::Buffer::CpuAccess::Write);
		m_upBufferForZeroView->SetName(DebugName(L"Upbuf for ZeroView"));
		{
			void* dst = m_upBufferForZeroView->Map(&m_device, GraphicsAPI::Buffer::MapType::WriteDiscard, 0, 0, 0);
			if (dst != nullptr) {
				std::array<uint32_t, 32> a = { 0 };
				memcpy(dst, a.data(), sizeof(uint32_t) * a.size());
				m_upBufferForZeroView->Unmap(&m_device, 0, 0, sizeof(uint32_t) * a.size());
			}
			else {
				Log::Fatal(L"Faild to map buffer for zero UAV.");
				return Status::ERROR_INTERNAL;
			}
		}
		m_bufferForZeroView = std::make_unique<GraphicsAPI::Buffer>();
		m_bufferForZeroView->Create(&m_device, 32, GraphicsAPI::Resource::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderResource, GraphicsAPI::Buffer::CpuAccess::None);
		m_bufferForZeroView->SetName(DebugName(L"For ZeroView"));

		m_bufferForZeroClear = std::make_unique<GraphicsAPI::Buffer>();
		m_bufferForZeroClear->Create(&m_device, 32, GraphicsAPI::Resource::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderResource, GraphicsAPI::Buffer::CpuAccess::None);
		m_bufferForZeroClear->SetName(DebugName(L"For ZeroClear"));


#if defined(GRAPHICS_API_D3D12)
		m_zeroBufferUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
		m_zeroBufferUAV->InitNullView(GraphicsAPI::Resource::Type::Buffer, false);

		m_nullBufferUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
		m_nullBufferUAV->InitNullView(GraphicsAPI::Resource::Type::Buffer, false);
		m_nullBufferSRV = std::make_unique<GraphicsAPI::ShaderResourceView>();
		m_nullBufferSRV->InitNullView(GraphicsAPI::Resource::Type::Buffer, false);

		m_nullTexture2DUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
		m_nullTexture2DUAV->InitNullView(GraphicsAPI::Resource::Type::Texture2D, false);
		m_nullTexture2DSRV = std::make_unique<GraphicsAPI::ShaderResourceView>();
		m_nullTexture2DSRV->InitNullView(GraphicsAPI::Resource::Type::Texture2D, false);
#elif defined(GRAPHICS_API_VK)
		// check if VK_EXT_robustness2 is supported on the givien device.
		bool isNullViewSupported = false;
		{
			//If the VkPhysicalDeviceRobustness2FeaturesEXT structure is included in the pNext chain of the VkPhysicalDeviceFeatures2 structure passed to vkGetPhysicalDeviceFeatures2,
			VkPhysicalDeviceFeatures2 feature2 = {};
			VkPhysicalDeviceRobustness2FeaturesEXT rbFeatureExt = {};

			feature2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			feature2.pNext = &rbFeatureExt;
			rbFeatureExt.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT;

			vkGetPhysicalDeviceFeatures2(m_device.m_apiData.m_physicalDevice, &feature2);

			isNullViewSupported = rbFeatureExt.nullDescriptor;
		}
#if 1
		// currently disabled since I hit an error when setting a null desc.
		isNullViewSupported = false;
#endif

		if (isNullViewSupported) {
			m_zeroBufferUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
			m_zeroBufferUAV->InitNullView(&m_device, GraphicsAPI::Resource::Type::Buffer, GraphicsAPI::Resource::Format::R32Uint, false);
			m_nullBufferUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
			m_nullBufferUAV->InitNullView(&m_device, GraphicsAPI::Resource::Type::Buffer, GraphicsAPI::Resource::Format::R32Uint, false);
			m_nullBufferSRV = std::make_unique<GraphicsAPI::ShaderResourceView>();
			m_nullBufferSRV->InitNullView(&m_device, GraphicsAPI::Resource::Type::Buffer, GraphicsAPI::Resource::Format::R32Uint, false);

			m_nullTexture2DUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
			m_nullTexture2DUAV->InitNullView(&m_device, GraphicsAPI::Resource::Type::Texture2D, GraphicsAPI::Resource::Format::RGBA8Unorm, false);
			m_nullTexture2DSRV = std::make_unique<GraphicsAPI::ShaderResourceView>();
			m_nullTexture2DSRV->InitNullView(&m_device, GraphicsAPI::Resource::Type::Texture2D, GraphicsAPI::Resource::Format::RGBA8Unorm, false);
		}
		else {
			// null view is not supported, so need to prepare dummy resources.
			m_zeroBufferUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
			m_zeroBufferUAV->Init(&m_device, m_bufferForZeroView.get());

			m_bufferForNullView = std::make_unique<GraphicsAPI::Buffer>();
			m_bufferForNullView->Create(&m_device, 8, GraphicsAPI::Resource::Format::R32Uint, GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderResource, GraphicsAPI::Buffer::CpuAccess::None);
			m_bufferForNullView->SetName(DebugName(L"For NullView"));
			m_nullBufferUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
			m_nullBufferUAV->Init(&m_device, m_bufferForNullView.get());
			m_nullBufferSRV = std::make_unique<GraphicsAPI::ShaderResourceView>();
			m_nullBufferSRV->Init(&m_device, m_bufferForNullView.get());

			m_texture2DForNullUAView = std::make_unique<GraphicsAPI::Texture>();
			m_texture2DForNullUAView->Create(&m_device, GraphicsAPI::Resource::Type::Texture2D, GraphicsAPI::Resource::Format::RGBA8Unorm, GraphicsAPI::Resource::BindFlags::UnorderedAccess, 8, 8, 1, 1, 1, 1);
			m_texture2DForNullUAView->SetName(DebugName(L"For NullUAView"));
			m_texture2DForNullSRView = std::make_unique<GraphicsAPI::Texture>();
			m_texture2DForNullSRView->Create(&m_device, GraphicsAPI::Resource::Type::Texture2D, GraphicsAPI::Resource::Format::RGBA8Unorm, GraphicsAPI::Resource::BindFlags::ShaderResource, 8, 8, 1, 1, 1, 1);
			m_texture2DForNullSRView->SetName(DebugName(L"For NullSRView"));
			m_nullTexture2DUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
			m_nullTexture2DUAV->Init(&m_device, m_texture2DForNullUAView.get());
			m_nullTexture2DSRV = std::make_unique<GraphicsAPI::ShaderResourceView>();
			m_nullTexture2DSRV->Init(&m_device, m_texture2DForNullSRView.get());
		}
#endif

		// Load cold load shaders that are registerd unil here.
		if (m_shaderFactory->LoadColdLoadShaders(this) != Status::OK) {
			Log::Fatal(L"Faild to create shader object with cold load list at initialization.");
			return Status::ERROR_INTERNAL;
		}

		return Status::OK;
	}

	Status PersistentWorkingSet::InitWithCommandList(GraphicsAPI::CommandList* cmdList)
	{
		if (m_upBufferForZeroView) {
			// set resource barrier
			{
				std::vector<GraphicsAPI::Resource*>			resArr;
				std::vector<GraphicsAPI::ResourceState::State>	stateArr;

				resArr.push_back(m_bufferForZeroClear.get());
				stateArr.push_back(GraphicsAPI::ResourceState::State::CopyDest);
				resArr.push_back(m_bufferForZeroView.get());
				stateArr.push_back(GraphicsAPI::ResourceState::State::CopyDest);
				// D3D12 doesn't need resource transition for upload heap (GENERIC_READ)
				//resArr.push_back(m_upBufferForZeroView.get());
				//stateArr.push_back(GraphicsAPI::ResourceState::State::CopySource);

				if (!cmdList->ResourceTransitionBarrier(&resArr[0], resArr.size(), &stateArr[0])) {
					Log::Fatal(L"Failed ResourceTransitionBarrier.");
					return Status::ERROR_INTERNAL;
				}
			}

			cmdList->CopyBufferRegion(m_bufferForZeroView.get(), 0, m_upBufferForZeroView.get(), 0, m_upBufferForZeroView->m_sizeInBytes);
			cmdList->CopyBufferRegion(m_bufferForZeroClear.get(), 0, m_upBufferForZeroView.get(), 0, m_upBufferForZeroView->m_sizeInBytes);

			// set resource barrier
			{
				std::vector<GraphicsAPI::Resource*>			resArr;
				std::vector<GraphicsAPI::ResourceState::State>	stateArr;

				resArr.push_back(m_bufferForZeroView.get());
				stateArr.push_back(GraphicsAPI::ResourceState::State::UnorderedAccess);

				resArr.push_back(m_bufferForZeroClear.get());
				stateArr.push_back(GraphicsAPI::ResourceState::State::CopySource);

				if (m_texture2DForNullUAView) {
					resArr.push_back(m_texture2DForNullUAView.get());
					stateArr.push_back(GraphicsAPI::ResourceState::State::UnorderedAccess);
				}
				if (m_texture2DForNullSRView) {
					resArr.push_back(m_texture2DForNullSRView.get());
					stateArr.push_back(GraphicsAPI::ResourceState::State::NonPixelShader);
				}

				if (!cmdList->ResourceTransitionBarrier(&resArr[0], resArr.size(), &stateArr[0])) {
					Log::Fatal(L"Failed ResourceTransitionBarrier.");
					return Status::ERROR_INTERNAL;
				}
			}

			// m_upBufferForZeroView is out of ResourceLogger.
			DeferredRelease(std::move(m_upBufferForZeroView));
		}

		return Status::OK;
	}

	void PersistentWorkingSet::SetTaskIndices(uint64_t currentIndex, uint64_t lastFinishedTaskIndex)
	{
		m_currentTaskIndex = currentIndex;
		m_lastFinishedTaskIndex = lastFinishedTaskIndex;
	}

	void PersistentWorkingSet::ClearTaskIndices()
	{
		m_currentTaskIndex.reset();
		m_lastFinishedTaskIndex.reset();
	}

	bool PersistentWorkingSet::HasTaskIndices() const
	{
		return m_currentTaskIndex.has_value() || m_lastFinishedTaskIndex.has_value();
	}

	uint64_t PersistentWorkingSet::GetCurrentTaskIndex() const
	{
		if (m_currentTaskIndex.has_value())
			return m_currentTaskIndex.value();

		Log::Fatal(L"Invalid current task index was referenced.");
		return 0xFFFF'FFFF'FFFF'FFFFull;
	}

	uint64_t PersistentWorkingSet::GetLastFinishedTaskIndex() const
	{
		if (m_lastFinishedTaskIndex.has_value())
			return m_lastFinishedTaskIndex.value();

		Log::Fatal(L"Invalid last finished task index was referenced.");
		return 0xFFFF'FFFF'FFFF'FFFFull;
	}

	void PersistentWorkingSet::ReleaseDeferredReleasedDeviceObjects(uint64_t finishedTaskIndex)
	{
		static uint64_t	logIndex;

		m_resourceLogger.ReleaseDeferredReleasedDeviceObjects(finishedTaskIndex);
		m_resourceLogger.LogResource(logIndex++);

		uint64_t framesToRemove = 30;
		m_sharedBufferForDirectLightingCache->CheckUnusedBufferBlocks(framesToRemove);
		m_sharedBufferForDirectLightingCacheTemp->CheckUnusedBufferBlocks(framesToRemove);
		m_sharedBufferForVertexTemporal->CheckUnusedBufferBlocks(framesToRemove);
		m_sharedBufferForVertexPersistent->CheckUnusedBufferBlocks(framesToRemove);

		m_sharedBufferForReadback->CheckUnusedBufferBlocks(framesToRemove);
		m_sharedBufferForCounter->CheckUnusedBufferBlocks(framesToRemove);

		m_sharedBufferForBLASTemporal->CheckUnusedBufferBlocks(framesToRemove);
		m_sharedBufferForBLASScratchTemporal->CheckUnusedBufferBlocks(framesToRemove);

		m_sharedBufferForBLASPermanent->CheckUnusedBufferBlocks(framesToRemove);
		m_sharedBufferForBLASScratchPermanent->CheckUnusedBufferBlocks(framesToRemove);
	}

	void PersistentWorkingSet::DeferredRelease(std::unique_ptr<GraphicsAPI::DeviceObject> obj)
	{
		m_resourceLogger.DeferredRelease(GetCurrentTaskIndex(), std::move(obj));
	}

	std::unique_ptr<GraphicsAPI::Buffer> PersistentWorkingSet::CreateBufferResource(
		uint64_t sizeInBytesOrNumElements, GraphicsAPI::Resource::Format format,
		GraphicsAPI::Resource::BindFlags bindFlags, GraphicsAPI::Buffer::CpuAccess cpuAccess,
		ResourceLogger::ResourceKind kind)
	{
		std::unique_ptr<ClassifiedBuffer> retPtr;

		size_t sizeInBytes = sizeInBytesOrNumElements * (format == GraphicsAPI::Resource::Format::Unknown ? 1 : GraphicsAPI::Resource::GetFormatBytesPerBlock(format));
		retPtr = std::make_unique<ClassifiedBuffer>(&m_resourceLogger, kind, sizeInBytes);
		if (!retPtr->Create(&m_device, sizeInBytesOrNumElements, format, bindFlags, cpuAccess)) {
			Log::Fatal(L"Faild to create buffer resouce");
			return std::unique_ptr<GraphicsAPI::Buffer>();
		}

		return std::move(retPtr);
	}

	std::unique_ptr<GraphicsAPI::Texture> PersistentWorkingSet::CreateTextureResource(
		GraphicsAPI::Resource::Type type, GraphicsAPI::Resource::Format format, GraphicsAPI::Resource::BindFlags bindFlags,
		uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize, uint32_t mipLevels, uint32_t sampleCount,
		ResourceLogger::ResourceKind kind)
	{
		size_t sizeInBytes = (size_t)GraphicsAPI::Resource::GetFormatBytesPerBlock(format) * width * height;
		std::unique_ptr<ClassifiedTexture> retPtr = std::make_unique<ClassifiedTexture>(&m_resourceLogger, kind, sizeInBytes);

		if (!retPtr->Create(&m_device, type, format, bindFlags,
			width, height, depth, arraySize, mipLevels, sampleCount)) {
			Log::Fatal(L"Faild to create texture resouce");
			return std::unique_ptr<GraphicsAPI::Texture>();
		}

		return retPtr;
	}

	Status PersistentWorkingSet::LoadSingleMipTextureFromResource(
		std::wstring& resourcePath,
		uint32_t w,
		uint32_t h,
		uint32_t d,
		uint32_t pixelInBytes,
		GraphicsAPI::Resource::Type type,
		GraphicsAPI::Resource::Format format,
		std::function<void(void* src, void* dest, uint32_t nbPixels)> pixelCopyFunc,
		std::unique_ptr<GraphicsAPI::Texture>& deviceTexture,
		std::unique_ptr<GraphicsAPI::Buffer>& uploadBuffer,
		ResourceLogger::ResourceKind kind)
	{
		std::filesystem::path texPath(resourcePath);
		if (!m_winResFileSystem->fileExists(texPath)) {
			Log::Fatal(L"Failed to find a binary entry for a texture:%s", texPath.generic_wstring().c_str());
			return Status::ERROR_INTERNAL;
		}

		auto blob = m_winResFileSystem->readFile(texPath);

		if (blob->size() != (size_t)w * h * d * pixelInBytes) {
			Log::Fatal(L"Invalid binary data size detected for a texture:%s", texPath.generic_wstring().c_str());
			return Status::ERROR_INTERNAL;
		}

		deviceTexture = CreateTextureResource(type, format, GraphicsAPI::Resource::BindFlags::ShaderResource, w, h, d, 1, 1, 1, kind);
		if (!deviceTexture) {
			Log::Fatal(L"Faild to create texture texture.");
			return Status::ERROR_INTERNAL;
		}

		uint32_t rowPitchInBytes;
		uint32_t totalSizeInBytes;
		if (!deviceTexture->GetUploadBufferFootplint(&m_device, 0, &rowPitchInBytes, &totalSizeInBytes)) {
			Log::Fatal(L"Faild to get upload buffer foot print.");
			return Status::ERROR_INTERNAL;
		}

		// create upload heap and map, write.
		uploadBuffer = CreateBufferResource(totalSizeInBytes, GraphicsAPI::Resource::Format::Unknown,
			GraphicsAPI::Resource::BindFlags::None, GraphicsAPI::Buffer::CpuAccess::Write, kind);
		if (!uploadBuffer) {
			Log::Fatal(L"Faild to create upload buffer resource.");
			return Status::ERROR_INTERNAL;
		}

		{
			void* mappedPtr = uploadBuffer->Map(&m_device, GraphicsAPI::Buffer::MapType::WriteDiscard, 0, 0, 0);

			intptr_t	mappedAddr = reinterpret_cast<intptr_t>(mappedPtr);
			intptr_t	blobAddr = reinterpret_cast<intptr_t>(blob->data());

			for (uint32_t slice = 0; slice < d; ++slice) {
				for (uint32_t line = 0; line < h; ++line) {
					uint64_t srcOffset = ((uint64_t)slice * h + line) * (uint64_t)pixelInBytes * w;
					uint64_t destOffset = ((uint64_t)slice * h + line) * (uint64_t)rowPitchInBytes;

					uint8_t* src = reinterpret_cast<uint8_t*>(blobAddr + srcOffset);
					uint8_t* dest = reinterpret_cast<uint8_t*>(mappedAddr + destOffset);
					pixelCopyFunc(dest, src, w);
				}
			}

			uploadBuffer->Unmap(&m_device, 0, 0, totalSizeInBytes);
		}

		return Status::OK;
	}

	// These are external interface, so those need to take mutex.
	Status PersistentWorkingSet::GetResourceAllocations(KickstartRT::ResourceAllocations* retAllocation)
	{
		std::scoped_lock mtx(m_mutex);
		return m_resourceLogger.GetResourceAllocations(retAllocation);
	}

	Status PersistentWorkingSet::BeginLoggingResourceAllocations(const wchar_t* filePath)
	{
		std::scoped_lock mtx(m_mutex);
		return m_resourceLogger.BeginLoggingResourceAllocations(filePath);
	}

	Status PersistentWorkingSet::EndLoggingResourceAllocations()
	{
		std::scoped_lock mtx(m_mutex);
		return m_resourceLogger.EndLoggingResourceAllocations();
	}
};
