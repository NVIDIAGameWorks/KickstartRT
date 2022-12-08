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
#include <ResourceLogger.h>

#include <string>
#include <map>
#include <deque>

namespace KickstartRT_NativeLayer
{
    class PersistentWorkingSet;

    // base implementation with no allocator
    class SharedBuffer {
    public:
        struct BufferBlock {
            std::unique_ptr<GraphicsAPI::Buffer>                m_buffer;
            std::unique_ptr<GraphicsAPI::UnorderedAccessView>   m_uav;
            std::unique_ptr<SharedCPUDescriptorHeap::SharedTableEntry>  m_cpuDesc;
            intptr_t        m_mappedPtr = 0;
            uint64_t        m_gpuPtr = 0;

            std::deque<std::pair<uint64_t, uint64_t>>           m_clearRequests;
            bool            m_barrierRequest = false;
            bool            m_batchMapRequest = false;

            ~BufferBlock()
            {
                m_clearRequests.clear();
                m_cpuDesc.reset();
                m_uav.reset();
                m_buffer.reset();
            }
        };

    protected:
        bool    m_useClear = false;
        bool    m_useGPUPtr = false;
        size_t  m_blockSizeInBytes = 0;
        size_t  m_alignmentSizeInBytes = 0;
        size_t  m_formatSizeInByte = 0;
        GraphicsAPI::Resource::Format m_format = GraphicsAPI::Resource::Format::Unknown;
        GraphicsAPI::Resource::BindFlags m_bindFlags = GraphicsAPI::Resource::BindFlags::None;
        GraphicsAPI::Buffer::CpuAccess m_cpuAccess = GraphicsAPI::Buffer::CpuAccess::None;

        GraphicsAPI::DescriptorTableLayout	m_oneUAVLayout;

        ResourceLogger::ResourceKind        m_bufferBlockKind;
        ResourceLogger::ResourceKind        m_bufferEntryKind;
        std::wstring                        m_debugName;

        std::map<intptr_t, std::unique_ptr<BufferBlock>>    m_bufferBlocks;

    public:
        struct BufferEntry : public GraphicsAPI::DeviceObject {
            SharedBuffer* m_manager = nullptr;
            BufferBlock* m_block = nullptr;
            bool                m_isAllocatedExclusively = false; // if true, using buffer block is allocated exclusively for this entry. need to destruct it when dtor called.

            std::unique_ptr<GraphicsAPI::UnorderedAccessView>   m_uav;
            size_t          m_globalOffset = (size_t)-1;
            size_t          m_offset = (size_t)-1;
            size_t          m_size = (size_t)-1;

            virtual ~BufferEntry()
            {
                m_manager->ReleaseAllocation(this);
            }

            void* GetMappedPtr()
            {
                return reinterpret_cast<void*>(m_block->m_mappedPtr + m_offset);
            };

            uint64_t GetGpuPtr()
            {
                return m_block->m_gpuPtr + m_offset;
            };

            void RegisterClear()
            {
                m_block->m_clearRequests.push_back({ m_offset, m_size });
            };

            void RegisterBarrier()
            {
                m_block->m_barrierRequest = true;
            };

            void RegisterBatchMap()
            {
                m_block->m_batchMapRequest = true;
            };
        };

    protected:
        virtual void ReleaseAllocation(BufferEntry* ent) = 0;

    public:
        virtual Status Init(
            GraphicsAPI::Device* dev,
            uint64_t allocationAlignmentInBytes, bool useClear, bool useGPUPtr,
            uint64_t blockSizeInBytes, GraphicsAPI::Resource::Format format,
            GraphicsAPI::Resource::BindFlags bindFlags, GraphicsAPI::Buffer::CpuAccess cpuAccess,
            ResourceLogger::ResourceKind bufferBlockKind, ResourceLogger::ResourceKind bufferEntryKind,
            const std::wstring& debugName);

        Status DoClear(GraphicsAPI::Device* dev, GraphicsAPI::CommandList* cmdList, GraphicsAPI::IDescriptorHeap* currentGPUDescHeap); // D3D12 clear op need (currently GPU Visible) GPU desc table...

        Status TransitionBarrier(GraphicsAPI::CommandList* cmdList, GraphicsAPI::ResourceState::State state);
        Status UAVBarrier(GraphicsAPI::CommandList* cmdList);

        Status BatchMap(GraphicsAPI::Device* dev, GraphicsAPI::Buffer::MapType mapType);
        Status BatchUnmap(GraphicsAPI::Device* dev, GraphicsAPI::Buffer::MapType mapType);

        virtual std::unique_ptr<BufferEntry> Allocate(PersistentWorkingSet* pws, size_t requestedSizeInBytes, bool useUAV) = 0;

        virtual Status CheckUnusedBufferBlocks(uint64_t framesToRemove) = 0;

    protected:
        decltype(m_bufferBlocks)::iterator AddBufferBlock(PersistentWorkingSet* pws, size_t requestedSizeInBytes);
    };

    template<typename Allocator>
    class SharedBuffer_Impl {};

    template<>
    class SharedBuffer_Impl<void> : public SharedBuffer
    {
    protected:
        void ReleaseAllocation(BufferEntry* ent) override;

    public:
        std::unique_ptr<BufferEntry> Allocate(PersistentWorkingSet* pws, size_t requestedSizeInBytes, bool useUAV) override;

        Status CheckUnusedBufferBlocks(uint64_t framesToRemove) override;
    };

    // using VirtualAllocator::* 
    template<typename Allocator>
    class SharedBuffer_VirtualAllocatorImpl : public SharedBuffer {
    protected:
        std::map<uint32_t, BufferBlock*>    m_sharedBlocks;
        Allocator                           m_allocator;

        struct UsingBlockStatus {
            std::vector<uint32_t>    m_blockIDs;
            std::vector<uint32_t>   m_freeFrames;
        };
        UsingBlockStatus             m_usingBlockStatus;

    protected:
        void ReleaseAllocation(BufferEntry* ent) override;

    public:
        Status Init(
            GraphicsAPI::Device* dev,
            uint64_t allocationAlignmentInBytes, bool useUAV, bool useGPUPtr,
            uint64_t blockSizeInBytes, GraphicsAPI::Resource::Format format,
            GraphicsAPI::Resource::BindFlags bindFlags, GraphicsAPI::Buffer::CpuAccess cpuAccess,
            ResourceLogger::ResourceKind bufferBlockKind, ResourceLogger::ResourceKind bufferEntryKind,
            const std::wstring& debugName) override;

        std::unique_ptr<BufferEntry> Allocate(PersistentWorkingSet* pws, size_t requestedSizeInBytes, bool useUAV) override;

        Status CheckUnusedBufferBlocks(uint64_t framesToRemove) override;

        std::string DumpAllocator(bool dumpEntry, bool dumpFreed, bool dumpVis) const
        {
            return m_allocator.Dump(dumpEntry, dumpFreed, dumpVis);
        };
    };

    template<>
    class SharedBuffer_Impl<VirtualAllocator::BuddyAllocator> : public SharedBuffer_VirtualAllocatorImpl<VirtualAllocator::BuddyAllocator> {};
    template<>
    class SharedBuffer_Impl<VirtualAllocator::FixedPageAllocator> : public SharedBuffer_VirtualAllocatorImpl<VirtualAllocator::FixedPageAllocator> {};
};