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
#include <Log.h>

#include <set>
#include <filesystem>
#include <fstream>
#include <type_traits>

namespace KickstartRT_ExportLayer
{
	template<typename D3D11Type, typename D3D12Type>
	Status InteropCache<D3D11Type, D3D12Type>::Convert(ID3D12Device *dev12, D3D11Type* src, D3D12Type** dst, intptr_t usedTaskContainer)
	{
		auto Register = [&](std::unique_ptr<Interopped> iRes, intptr_t usedTaskContainer)
		{
			iRes->m_lastUsedFenceValue = 0xFFFF'FFFF'FFFF'FFFF;
			iRes->m_referencedTaskContainerPtr.insert(usedTaskContainer);

			m_cacheMap.insert({ iRes->m_11.Get(), std::move(iRes) });

			return Status::OK;
		};
		auto Find = [&](D3D11Type * src, intptr_t usedTaskContainer) -> Interopped *
		{
			std::map<D3D11Type*, std::unique_ptr<Interopped>>::iterator itr = m_cacheMap.find(src);
			if (itr != m_cacheMap.end()) {
				itr->second->m_lastUsedFenceValue = 0xFFFF'FFFF'FFFF'FFFF;
				itr->second->m_referencedTaskContainerPtr.insert(usedTaskContainer);
				return itr->second.get();
			}

			return nullptr;
		};

		*dst = nullptr;

		if (src == nullptr)
			return Status::OK;

		using SharedStruct = InteropCache<D3D11Type, D3D12Type>::Interopped;

		SharedStruct* iResPtr = Find(src, usedTaskContainer);

		if (iResPtr == nullptr) {
			HRESULT hr;
			std::unique_ptr<SharedStruct> iRes = std::make_unique<SharedStruct>();

			iRes->m_11 = src;

			if constexpr (std::is_same<D3D11Type, ID3D11Fence>::value) {
				hr = iRes->m_11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &iRes->m_handle);
				if (FAILED(hr)) {
					return Status::ERROR_INTERNAL;
				}
				iRes->m_isNTHandle = true;
			}
			else {
				Microsoft::WRL::ComPtr<IDXGIResource> pDXGIResource;
				hr = iRes->m_11->QueryInterface(IID_PPV_ARGS(&pDXGIResource));
				if (FAILED(hr)) {
					return Status::ERROR_INTERNAL;
				}
				hr = pDXGIResource->GetSharedHandle(&iRes->m_handle);
				if (FAILED(hr)) {
					return Status::ERROR_INTERNAL;
				}
				iRes->m_isNTHandle = false;
			}

			hr = dev12->OpenSharedHandle(iRes->m_handle, IID_PPV_ARGS(&iRes->m_12));
			if (FAILED(hr)) {
				return Status::ERROR_INTERNAL;
			}

			iResPtr = iRes.get();
			Status sts = Register(std::move(iRes), usedTaskContainer);
			if (sts != Status::OK) {
				return Status::ERROR_INTERNAL;
			}
		}
		if (iResPtr == nullptr) {
			return Status::ERROR_INTERNAL;
		}

		*dst = iResPtr->m_12.Get();
		return Status::OK;
	};

	template
		Status InteropCache<ID3D11Resource, ID3D12Resource>::Convert(ID3D12Device* dev12, ID3D11Resource* src, ID3D12Resource** dst, intptr_t usedTaskContainer);
	template
		Status InteropCache<ID3D11Fence, ID3D12Fence>::Convert(ID3D12Device* dev12, ID3D11Fence* src, ID3D12Fence** dst, intptr_t usedTaskContainer);


	Status InteropCacheSet::ReleaseCacheResources(ResourceLogger& logger, uint64_t lastSubmittedFenceValue, uint64_t completedFenceValue)
	{
		(void)lastSubmittedFenceValue;

		// actually release object registered before the completed fence value.
		if (completedFenceValue != (uint64_t)-1) {
			// -1 means there is no completed task, so skip this.
			logger.ReleaseDeferredReleasedDeviceObjects(completedFenceValue);
		}

		// Remove cache entries after use. 
		{
			std::scoped_lock mtx(m_mutex);

			// All resource cache entries will arrive until used GPU task has been finished.
			{
				auto itr = m_geometryCache.m_cacheMap.begin();
				while (itr != m_geometryCache.m_cacheMap.end()) {
					if (itr->second->m_lastUsedFenceValue <= completedFenceValue) {
						logger.ImmediateRelease(std::move(itr->second));
						itr = m_geometryCache.m_cacheMap.erase(itr);
					}
					else {
						++itr;
					}
				}
			}
			{
				auto itr = m_textureCache.m_cacheMap.begin();
				while (itr != m_textureCache.m_cacheMap.end()) {
					if (itr->second->m_lastUsedFenceValue <= completedFenceValue) {
						logger.ImmediateRelease(std::move(itr->second));
						itr = m_textureCache.m_cacheMap.erase(itr);
					}
					else {
						++itr;
					}
				}
			}
			{
				auto itr = m_fenceCache.m_cacheMap.begin();
				while (itr != m_fenceCache.m_cacheMap.end()) {
					if (itr->second->m_lastUsedFenceValue <= completedFenceValue) {
						logger.ImmediateRelease(std::move(itr->second));
						itr = m_fenceCache.m_cacheMap.erase(itr);
					}
					else {
						++itr;
					}
				}
			}
		}

		return Status::OK;
	};

	Status InteropCacheSet::SetLastUsedFenceValue(uint64_t fenceValueToSet, intptr_t usedTaskContainer)
	{
		std::scoped_lock mtx(m_mutex);

		auto setIdx = [&](InteropCache<ID3D11Resource, ID3D12Resource>& c) {
			for (auto&& itr : c.m_cacheMap) {
				if (itr.second->m_lastUsedFenceValue == 0xFFFF'FFFF'FFFF'FFFF) {
					// Try to erase this container's reference.
					size_t num = itr.second->m_referencedTaskContainerPtr.erase(usedTaskContainer);
					if (num > 0 && itr.second->m_referencedTaskContainerPtr.empty()) {
						// Now reference list is empty so that set the last used fence value.
						itr.second->m_lastUsedFenceValue = fenceValueToSet;
					}
				}
			}
		};

		setIdx(m_geometryCache);
		setIdx(m_textureCache);

		for (auto&& itr : m_fenceCache.m_cacheMap) {
			if (itr.second->m_lastUsedFenceValue == 0xFFFF'FFFF'FFFF'FFFF) {
				// Try to erase this container's reference.
				size_t num = itr.second->m_referencedTaskContainerPtr.erase(usedTaskContainer);
				if (num > 0 && itr.second->m_referencedTaskContainerPtr.empty()) {
					// Now reference list is empty so that set the last used fence value.
					itr.second->m_lastUsedFenceValue = fenceValueToSet;
				}
			}
		}

		return Status::OK;
	};


	PersistentWorkingSet::PersistentWorkingSet()
	{
	};

	PersistentWorkingSet::~PersistentWorkingSet()
	{
		if (m_SDK_12) {
			auto sts = D3D12::ExecuteContext::Destruct(m_SDK_12);
			if (sts != Status::OK) {
				Log::Fatal(L"Faild to destruct D3D12 KickstartRT instance.");
			}
			m_SDK_12 = nullptr;
		}

		m_queue_12 = nullptr;
		m_device_12 = nullptr;
		m_DXGIAdapter = nullptr;
	}

	Status PersistentWorkingSet::Init(const ExecuteContext_InitSettings* initSettings)
	{
#if _DEBUG
		{
			Microsoft::WRL::ComPtr<ID3D12Debug1> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();
				// GPU based validation was causing unexpected TDRs, so be careful if you use it
				//debugController->SetEnableGPUBasedValidation(TRUE);
			}
		}
#endif
		// copy adapter.
		m_DXGIAdapter = initSettings->DXGIAdapter;

		// create 12 device.
		{
			DXGI_ADAPTER_DESC1 adapterDesc;
			m_DXGIAdapter->GetDesc1(&adapterDesc);

			if (SUCCEEDED(D3D12CreateDevice(m_DXGIAdapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&m_device_12))))
			{
				// Check if the device supports ray tracing
				D3D12_FEATURE_DATA_D3D12_OPTIONS5 features = {};
				HRESULT hr = m_device_12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features, sizeof(features));
				if (FAILED(hr) || features.RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
				{
					Log::Fatal(L"Faild to create D3D12 device. RT was not supported.");
					return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
				}
				m_device_12->SetName(L"KS Interop");
			}
			else {
				Log::Fatal(L"Faild to create D3D12 device.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
		}

		D3D12_COMMAND_LIST_TYPE nativeCommandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (initSettings->usingCommandQueue == ExecuteContext_InitSettings::UsingCommandQueue::Compute) {
			nativeCommandListType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
		}

		// create command queue.
		{
			D3D12_COMMAND_QUEUE_DESC desc = {};
			desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			desc.Type = nativeCommandListType;

			HRESULT hr = m_device_12->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_queue_12));
			if (FAILED(hr)) {
				Log::Fatal(L"Faild to create D3D12 command queue.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
			m_queue_12->SetName(L"KS Interop");
			m_queueType = desc.Type;
		}

#if _DEBUG
		{
			// Configure debug device (if active).
			Microsoft::WRL::ComPtr<ID3D12InfoQueue> d3dInfoQueue;
			if (SUCCEEDED(m_device_12.As(&d3dInfoQueue)))
			{
				d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
				d3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
			}
		}
#endif

		{
			HRESULT hr = m_device_12->CreateFence(0ull, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence.m_taskFence_12));
			if (FAILED(hr)) {
				Log::Fatal(L"Faild to create D3D12 fence.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}

			// command allocator and command lists
			{
				m_fence.m_commandAllocators.resize(initSettings->supportedWorkingSet);
				m_fence.m_commandLists.resize(initSettings->supportedWorkingSet);
				m_fence.m_commandAllocatorUsed.resize(initSettings->supportedWorkingSet, 0);

				for (auto&& ca : m_fence.m_commandAllocators) {
					if (FAILED(m_device_12->CreateCommandAllocator(nativeCommandListType, IID_PPV_ARGS(&ca))))
					{
						Log::Fatal(L"Failed to create command allocators");
						return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
					}
				}
				for (size_t i = 0; i < m_fence.m_commandLists.size(); ++i) {
					if (FAILED(m_device_12->CreateCommandList(0, nativeCommandListType, m_fence.m_commandAllocators[i].Get(), nullptr, IID_PPV_ARGS(&m_fence.m_commandLists[i]))))
					{
						Log::Fatal(L"Failed to create command lists");
						return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
					}
					m_fence.m_commandLists[i]->Close();
				}
			}
		}

		// crete interop cache set.
		m_interopCacheSet = std::make_unique<InteropCacheSet>(m_device_12);

		// init KS SDK.
		{
			D3D12::ExecuteContext_InitSettings initSettings_12 = {};

			initSettings_12.D3D12Device = m_device_12.Get();
			initSettings_12.descHeapSize = initSettings->descHeapSize;
			initSettings_12.supportedWorkingsets = initSettings->supportedWorkingSet;
			initSettings_12.uploadHeapSizeForVolatileConstantBuffers = initSettings->uploadHeapSizeForVolatileConstantBuffers;
			initSettings_12.coldLoadShaderList = initSettings->coldLoadShaderList;
			initSettings_12.coldLoadShaderListSize = initSettings->coldLoadShaderListSize;

			auto sts = D3D12::ExecuteContext::Init(&initSettings_12, &m_SDK_12);
			if (sts != Status::OK) {
				Log::Fatal(L"Failed to init execute context.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}


		}
		return Status::OK;
	};
};

