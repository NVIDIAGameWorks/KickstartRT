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
#include <GraphicsAPI/GraphicsAPI.h>

#include <memory>
#include <string>
#include <deque>

namespace KickstartRT_NativeLayer
{
    template<typename T> struct ClassifiedDeviceObject;

    // this class simply try to understand the current allocation by SDK, not meant to track all resources precisely.
    class ResourceLogger {
        template<typename T> friend struct ClassifiedDeviceObject;

        friend class PersistentWorkingSet;

    public:
        using ResourceKind = KickstartRT::ResourceAllocations::ResourceKind;

    protected:
        KickstartRT::ResourceAllocations                                    m_allocationInfo;
        std::deque<std::pair<uint64_t, std::unique_ptr<GraphicsAPI::DeviceObject>>> m_deferredReleasedDeviceObjects;

        bool                        m_isLogging;
        std::wstring                m_logFilePath;
        static constexpr size_t     m_logFlushFrames = 600;
        size_t                      m_flushTimes;
        std::deque<std::pair<uint64_t, KickstartRT::ResourceAllocations>> m_frameLogs;

        void DeferredRelease(uint64_t fenceValue, std::unique_ptr<GraphicsAPI::DeviceObject> trackedObj)
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

        void CheckLeaks();
        void LogResource(uint64_t frameIndex);

        void FlushLog();
    public:
        ResourceLogger()
        {
            using RA = KickstartRT::ResourceAllocations;

            for (size_t i = 0; i < (size_t)RA::ResourceKind::e_Num_Kinds; ++i) {
                m_allocationInfo.m_numResources[i] = 0;
                m_allocationInfo.m_totalRequestedBytes[i] = 0;
            }

            m_isLogging = false;
            m_flushTimes = 0;
        }

        ~ResourceLogger()
        {
            // to flush incomplete log.
            EndLoggingResourceAllocations();
        }


        Status BeginLoggingResourceAllocations(const wchar_t* filePath);
        Status EndLoggingResourceAllocations();

        Status GetResourceAllocations(KickstartRT::ResourceAllocations* retAllocation);
    };

    template<typename T>
    struct ClassifiedDeviceObject : public T {
    protected:
        ResourceLogger*                 m_logger = {};
        ResourceLogger::ResourceKind    m_kind = (ResourceLogger::ResourceKind)-1;
        size_t                          m_loggedSizeInBytes = 0;

    public:
        ClassifiedDeviceObject(ResourceLogger *logger, ResourceLogger::ResourceKind kind, size_t loggedSizeInBytes)
        {
            m_logger = logger;
            m_kind = kind;
            m_loggedSizeInBytes = loggedSizeInBytes;

            if (m_kind < ResourceLogger::ResourceKind::e_Num_Kinds) {
                ++m_logger->m_allocationInfo.m_numResources[(size_t)m_kind];
                m_logger->m_allocationInfo.m_totalRequestedBytes[(size_t)m_kind] += m_loggedSizeInBytes;
            }
            else {
                Log::Fatal(L"Failed to create classified device object. Invalid resource kind detected.");
            }
        };

        virtual ~ClassifiedDeviceObject()
        {
            --m_logger->m_allocationInfo.m_numResources[(size_t)m_kind];
            m_logger->m_allocationInfo.m_totalRequestedBytes[(size_t)m_kind] -= m_loggedSizeInBytes;
        };
    };
};

