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

#include <VirtualAllocator.h>
#include <SharedCPUDescriptorHeap.h>
#include <SharedBuffer.h>
#include <ResourceLogger.h>

#include <mutex>
#include <unordered_map>
#include <deque>
#include <memory>
#include <functional>
#include <set>
#include <optional>

#if !defined(KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_PERSISTENT_DEVICE_RESOURCES)
#error "KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_PERSISTENT_DEVICE_RESOURCES must be defined"
#endif
#if !defined(KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_TEMPORAL_DEVICE_RESOURCES)
#error "KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_TEMPORAL_DEVICE_RESOURCES must be defined"
#endif
#if !defined(KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_READBACK_AND_COUNTER_RESOURCES)
#error "KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_READBACK_AND_COUNTER_RESOURCES must be defined"
#endif

namespace KickstartRT_NativeLayer
{
    class ExecuteContext_impl;
    class PersistentWorkingSet;
    class TaskWorkingSet;
    struct RenderPass_DirectLightingCacheAllocation;
    struct RenderPass_DirectLightingCacheInjection;
    struct RenderPass_DirectLightingCacheReflection;
    struct RenderPass_Denoising;
};

namespace KickstartRT
{
    namespace ShaderFactory {
        class Factory;
    };
    namespace VirtualFS {
        class WinResFileSystem;
    };
}

namespace KickstartRT_NativeLayer
{
    class TaskWorkingSet;

    class PersistentWorkingSet
    {
        friend class TaskWorkingSet;
        friend class ExecuteContext_impl;

    public:
        // mutex for all operations.
        std::mutex		m_mutex;

        static constexpr int32_t    m_unboundDescTableUpperbound = 40000;

        ResourceLogger          m_resourceLogger;

        GraphicsAPI::Device     m_device;

        std::unique_ptr<RenderPass_DirectLightingCacheAllocation> m_RP_DirectLightingCacheAllocation;
        std::unique_ptr<RenderPass_DirectLightingCacheInjection> m_RP_DirectLightingCacheInjection;
        std::unique_ptr<RenderPass_DirectLightingCacheReflection> m_RP_DirectLightingCacheReflection;

        std::unique_ptr<GraphicsAPI::Buffer>                m_bufferForZeroClear;

        std::unique_ptr<GraphicsAPI::Buffer>                m_upBufferForZeroView;
        std::unique_ptr<GraphicsAPI::Buffer>                m_bufferForZeroView;
        std::unique_ptr<GraphicsAPI::UnorderedAccessView>   m_zeroBufferUAV;

        std::unique_ptr<GraphicsAPI::Buffer>                m_bufferForNullView;
        std::unique_ptr<GraphicsAPI::UnorderedAccessView>   m_nullBufferUAV;
        std::unique_ptr<GraphicsAPI::ShaderResourceView>    m_nullBufferSRV;

        std::unique_ptr<GraphicsAPI::Texture>               m_texture2DForNullUAView;
        std::unique_ptr<GraphicsAPI::Texture>               m_texture2DForNullSRView;
        std::unique_ptr<GraphicsAPI::UnorderedAccessView>   m_nullTexture2DUAV;
        std::unique_ptr<GraphicsAPI::ShaderResourceView>    m_nullTexture2DSRV;

        std::unique_ptr<ShaderFactory::Factory>             m_shaderFactory;
        std::shared_ptr<VirtualFS::WinResFileSystem>        m_winResFileSystem;

        std::unique_ptr<SharedCPUDescriptorHeap>            m_UAVCPUDescHeap1;
        std::unique_ptr<SharedCPUDescriptorHeap>            m_UAVCPUDescHeap2;

        std::vector<std::unique_ptr<GraphicsAPI::DescriptorHeap>>   m_descHeaps;

    protected:
        std::optional<uint64_t>                             m_currentTaskIndex;
        std::optional<uint64_t>                             m_lastFinishedTaskIndex;

    public:

#if KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_PERSISTENT_DEVICE_RESOURCES
        using AllocatorTypeForPersistentDeviceResources = VirtualAllocator::FixedPageAllocator;
#else
        using AllocatorTypeForPersistentDeviceResources = void;
#endif
#if KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_TEMPORAL_DEVICE_RESOURCES
        using AllocatorTypeForTemporalDeviceResources = VirtualAllocator::FixedPageAllocator;
#else
        using AllocatorTypeForTemporalDeviceResources = void;
#endif
#if KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_READBACK_AND_COUNTER_RESOURCES
        using AllocatorTypeForReadbackAndCounterResources = VirtualAllocator::BuddyAllocator;
#else
        using AllocatorTypeForReadbackAndCounterResources = void;
#endif

        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForPersistentDeviceResources>>   m_sharedBufferForDirectLightingCache;
        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForTemporalDeviceResources>>     m_sharedBufferForDirectLightingCacheTemp;

        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForPersistentDeviceResources>>   m_sharedBufferForVertexPersistent;
        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForTemporalDeviceResources>>     m_sharedBufferForVertexTemporal;

        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForReadbackAndCounterResources>> m_sharedBufferForReadback;
        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForReadbackAndCounterResources>> m_sharedBufferForCounter;

        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForPersistentDeviceResources>>   m_sharedBufferForBLASScratchPermanent;
        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForTemporalDeviceResources>>     m_sharedBufferForBLASScratchTemporal;

        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForPersistentDeviceResources>>   m_sharedBufferForBLASPermanent;
        std::unique_ptr<SharedBuffer_Impl<AllocatorTypeForTemporalDeviceResources>>     m_sharedBufferForBLASTemporal;

    public:
        PersistentWorkingSet(const GraphicsAPI::Device::ApiData &apiData);
        ~PersistentWorkingSet();

        Status Init(const KickstartRT_NativeLayer::ExecuteContext_InitSettings *initSettings);
        Status InitWithCommandList(GraphicsAPI::CommandList* cmdList);

        void SetTaskIndices(uint64_t currentIndex, uint64_t lastFinishedIndex);
        void ClearTaskIndices();
        bool HasTaskIndices() const;
        uint64_t GetCurrentTaskIndex() const;
        uint64_t GetLastFinishedTaskIndex() const;

        void ReleaseDeferredReleasedDeviceObjects(uint64_t finishedTaskIndex);
        void DeferredRelease(std::unique_ptr<GraphicsAPI::DeviceObject> obj);

        std::unique_ptr<GraphicsAPI::Buffer> CreateBufferResource(
            uint64_t sizeInBytesOrNumElements, GraphicsAPI::Resource::Format format,
            GraphicsAPI::Resource::BindFlags bindFlags, GraphicsAPI::Buffer::CpuAccess cpuAccess,
            ResourceLogger::ResourceKind kind);

        std::unique_ptr<GraphicsAPI::Texture> CreateTextureResource(
            GraphicsAPI::Resource::Type type, GraphicsAPI::Resource::Format format, GraphicsAPI::Resource::BindFlags bindFlags,
            uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize, uint32_t mipLevels, uint32_t sampleCount,
            ResourceLogger::ResourceKind kind);

        Status LoadSingleMipTextureFromResource(
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
            ResourceLogger::ResourceKind kind);

        Status GetResourceAllocations(KickstartRT::ResourceAllocations* retAllocation);
        Status BeginLoggingResourceAllocations(const wchar_t* filePath);
        Status EndLoggingResourceAllocations();
    };
};
