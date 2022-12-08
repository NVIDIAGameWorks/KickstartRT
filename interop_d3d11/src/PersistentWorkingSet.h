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
#pragma once
#include <Platform.h>

#include <map>
#include <unordered_set>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

namespace KickstartRT_ExportLayer
{
    class DeviceObject
    {
    public:
        virtual ~DeviceObject() {};
    };

    // this class just for releasing device objects with certain frame delay.
    // not meant to logg resource, but the native layer uses the same name for it.
    class ResourceLogger {
    protected:
        std::deque<std::pair<uint64_t, std::unique_ptr<DeviceObject>>> m_deferredReleasedDeviceObjects;

    public:
        ~ResourceLogger()
        {
        }

        void ImmediateRelease(std::unique_ptr<DeviceObject> trackedObj)
        {
            trackedObj.reset();
        }

        void DeferredRelease(uint64_t fenceValue, std::unique_ptr<DeviceObject> trackedObj)
        {
            if (trackedObj) {
                m_deferredReleasedDeviceObjects.push_back(
                    {
                        fenceValue,
                        std::move(trackedObj)
                    });
            }
        }

        void ReleaseDeferredReleasedDeviceObjects(uint64_t completedFenceValue)
        {
            while (!m_deferredReleasedDeviceObjects.empty()) {
                if (m_deferredReleasedDeviceObjects.begin()->first > completedFenceValue)
                    break;

                // destruct unique_ptr<> and it incurs dtor of the device object.
                m_deferredReleasedDeviceObjects.pop_front();
            }
        }
    };

    template<typename D3D11Type, typename D3D12Type>
    class InteropCache
    {
        friend class InteropCacheSet;

    protected:
        struct Interopped : public DeviceObject {
            Microsoft::WRL::ComPtr<D3D11Type>       m_11;
            Microsoft::WRL::ComPtr<D3D12Type>       m_12;
            bool                                    m_isNTHandle = false;
            HANDLE                                  m_handle = 0;
            uint64_t                                m_lastUsedFenceValue = 0;
            std::unordered_set<intptr_t>            m_referencedTaskContainerPtr; // This is a small list to keep reference from in-flight taskContainers.

            virtual ~Interopped()
            {
                m_12.Reset();
                if (m_handle != nullptr && m_isNTHandle) {
                    CloseHandle(m_handle);
                };
                m_handle = 0;
                m_11.Reset();
            };
        };
        std::map<D3D11Type*, std::unique_ptr<Interopped>> m_cacheMap;

    protected:
        Status Convert(ID3D12Device *dev12, D3D11Type* src, D3D12Type** dst, intptr_t usedTaskContainer);
    };

    class InteropCacheSet {
        std::mutex		                                m_mutex;
        Microsoft::WRL::ComPtr<ID3D12Device5>           m_device_12;
        InteropCache<ID3D11Resource, ID3D12Resource>    m_geometryCache;
        InteropCache<ID3D11Resource, ID3D12Resource>    m_textureCache;
        InteropCache<ID3D11Fence, ID3D12Fence>          m_fenceCache;

    public:
        InteropCacheSet(Microsoft::WRL::ComPtr<ID3D12Device5>& dev_12)
        {
            m_device_12 = dev_12;
        }

        Status ConvertTexture(ID3D11Resource* src, ID3D12Resource** dst, intptr_t usedTaskContainer)
        {
            std::scoped_lock mtx(m_mutex);

            return m_textureCache.Convert(m_device_12.Get(), src, dst, usedTaskContainer);
        }

        Status ConvertGeometry(ID3D11Resource* src, ID3D12Resource** dst, intptr_t usedTaskContainer)
        {
            std::scoped_lock mtx(m_mutex);

            return m_geometryCache.Convert(m_device_12.Get(), src, dst, usedTaskContainer);
        }

        Status ConvertFence(ID3D11Fence* src, ID3D12Fence** dst, intptr_t usedTaskContainer)
        {
            std::scoped_lock mtx(m_mutex);

            return m_fenceCache.Convert(m_device_12.Get(), src, dst, usedTaskContainer);
        }

        Status ReleaseCacheResources(ResourceLogger& logger, uint64_t lastSubmittedFenceValue, uint64_t completedFenceValue);
        Status SetLastUsedFenceValue(uint64_t fenceValueToSet, intptr_t usedTaskContainer);
    };

    class PersistentWorkingSet
    {
    public:
        ResourceLogger                                  m_resourceLogger;
        std::unique_ptr<InteropCacheSet>                m_interopCacheSet;

        Microsoft::WRL::ComPtr<IDXGIAdapter1>       m_DXGIAdapter;
        Microsoft::WRL::ComPtr<ID3D12Device5>       m_device_12;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue>  m_queue_12;
        D3D12_COMMAND_LIST_TYPE                     m_queueType = D3D12_COMMAND_LIST_TYPE_COMPUTE;

        KickstartRT::D3D12::ExecuteContext  *m_SDK_12 = nullptr;

        struct NativeFence {
            struct InflightTask {
                size_t                                      m_commandAllocatorIndex = (size_t) - 1;
                uint64_t                                    m_fenceValue = 0;
                KickstartRT::D3D12::GPUTaskHandle   m_handle = KickstartRT::D3D12::GPUTaskHandle::Null;
            };

            Microsoft::WRL::ComPtr<ID3D12Fence>     m_taskFence_12;
            uint64_t                                m_completedFenceValue_12 = (uint64_t)-1;
            uint64_t                                m_lastSubmittedFenceValue_12 = (uint64_t)-1;

            std::vector< Microsoft::WRL::ComPtr<ID3D12CommandAllocator>>	    m_commandAllocators;
            std::vector< Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList5>>	m_commandLists;
            std::vector<uint32_t>                                               m_commandAllocatorUsed;

            std::list<InflightTask>                 m_inflightTasks;

            void GetIdleCommandList(size_t *caIdx, ID3D12CommandAllocator **ca, ID3D12GraphicsCommandList5 **cl)
            {
                for (size_t i = 0; i < m_commandAllocatorUsed.size(); ++i) {
                    if (m_commandAllocatorUsed[i] == 0) {
                        m_commandAllocatorUsed[i] = 1;
                        *caIdx = i;
                        *ca = m_commandAllocators[i].Get();
                        *cl = m_commandLists[i].Get();
                        return;
                    }
                }
                *caIdx = (size_t) - 1;
                *ca = nullptr;
                *cl = nullptr;
            }

            // record in-flight SDK task and update completed fence value.
            void RecordInflightTask(size_t commandAllocatorIndex, uint64_t submittedFenceValue, KickstartRT::D3D12::GPUTaskHandle submittedTaskHandle)
            {
                m_lastSubmittedFenceValue_12 = submittedFenceValue;
                m_inflightTasks.push_back({ commandAllocatorIndex, submittedFenceValue, submittedTaskHandle });
            }

            void UpdateCompltedValue()
            {
                m_completedFenceValue_12 = m_taskFence_12->GetCompletedValue();
            }

            // secure at least one TaskWorkingSet in D3D12 layer and a commandList to execute.
            Status WaitForIdleTaskWorkingSet(KickstartRT::D3D12::ExecuteContext* sdk12, uint32_t numberOfWorkingSets)
            {
                for (;;) {
                    m_completedFenceValue_12 = m_taskFence_12->GetCompletedValue();

                    for (auto hItr = m_inflightTasks.begin(); hItr != m_inflightTasks.end();) {
                        if (hItr->m_fenceValue <= m_completedFenceValue_12) {
                            auto sts = sdk12->MarkGPUTaskAsCompleted(hItr->m_handle);
                            if (sts != Status::OK) {
                                return sts;
                            }
                            // free up the used command allocator/list.
                            m_commandAllocatorUsed[hItr->m_commandAllocatorIndex] = 0;
                            hItr = m_inflightTasks.erase(hItr);
                            continue;
                        }
                        break;
                    }
                    if (m_inflightTasks.size() < numberOfWorkingSets)
                        break;

                    Sleep(0);
                }

                return Status::OK;
            }

        };
        NativeFence m_fence;


    public:
        PersistentWorkingSet();
        ~PersistentWorkingSet();

        Status Init(const KickstartRT_ExportLayer::ExecuteContext_InitSettings* initSettings);
    };
};
