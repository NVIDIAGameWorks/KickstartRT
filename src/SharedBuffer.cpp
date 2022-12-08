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
#include <Utils.h>
#include <Log.h>
#include <PersistentWorkingSet.h>

#include <SharedBuffer.h>

namespace KickstartRT_NativeLayer
{
	using ClassifiedBufferEntry = ClassifiedDeviceObject<SharedBuffer::BufferEntry>;

	Status SharedBuffer::Init(
		GraphicsAPI::Device* dev,
		uint64_t allocationAlignmentInBytes, bool useClear, bool useGPUPtr,
		uint64_t blockSizeInBytes, GraphicsAPI::Resource::Format format,
		GraphicsAPI::Resource::BindFlags bindFlags, GraphicsAPI::Buffer::CpuAccess cpuAccess,
		ResourceLogger::ResourceKind bufferBlockKind, ResourceLogger::ResourceKind bufferEntryKind,
		const std::wstring& debugName)
	{
		if (useClear && !is_set(bindFlags, GraphicsAPI::Resource::BindFlags::UnorderedAccess)) {
			Log::Fatal(L"useClear flag requires uav binding flag.");
			return Status::ERROR_INTERNAL;
		}
		if (useGPUPtr && !is_set(bindFlags, GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress)) {
			Log::Fatal(L"useGPUPtr flag requires ShaderDeviceAddress flag.");
			return Status::ERROR_INTERNAL;
		}

		m_format = format;
		m_useClear = useClear;
		m_useGPUPtr = useGPUPtr;
		m_blockSizeInBytes = blockSizeInBytes;
		m_alignmentSizeInBytes = allocationAlignmentInBytes;
		m_formatSizeInByte = GraphicsAPI::Resource::GetFormatBytesPerBlock(m_format);

		// both block and page sizes need to be power of two.
		if (std::bitset<64>(m_blockSizeInBytes).count() != 1) {
			Log::Fatal(L"BlockSizeInByte needs to be power of 2.");
			return Status::ERROR_INTERNAL;
		}
		if (std::bitset<64>(m_alignmentSizeInBytes).count() != 1) {
			Log::Fatal(L"Allocation alignment needs to be power of 2.");
			return Status::ERROR_INTERNAL;
		}

		m_bindFlags = bindFlags;
		m_cpuAccess = cpuAccess;
		m_bufferBlockKind = bufferBlockKind;
		m_bufferEntryKind = bufferEntryKind;
		m_debugName = debugName;

		// Create desc layout
		if (m_useClear) {
			m_oneUAVLayout.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 0, 1, 0);
			m_oneUAVLayout.SetAPIData(dev);
		}

		return Status::OK;
	}

	Status SharedBuffer::DoClear(GraphicsAPI::Device* dev, GraphicsAPI::CommandList* cmdList, GraphicsAPI::IDescriptorHeap* currentGPUDescHeap)
	{
		(void)dev; (void)currentGPUDescHeap;

		// ------------
		// be carefull not to touch resources that are about to destruct.

		if (!m_useClear) {
			Log::Fatal(L"Faild to clear shared buffer. UAV was not allocated for buffer blocks.");
			return Status::ERROR_INTERNAL;
		}

		for (auto&& bbM : m_bufferBlocks) {
			auto& bb(bbM.second);
			if (bb->m_clearRequests.size() == 0)
				continue;

#if defined(GRAPHICS_API_D3D12)
			// gather clear rects.
			// not fully verified but seems D3D12 takes the rects as let-top, right-bottom with an exclusive manner.
			// if you set to clear R32UINT buffer resource with 0,0,4,0, it produces an error in debug layer.
			// if you set to clear R32UINT buffer resource with 2,0,3,1, it clears 4 bytes with 8 bytes offset.
			// if you set to clear RGBA32UINT buffer resource with 2,0,3,1, it clears 16 bytes with 32 bytes offset.
			// it only uses the first element of the clear value for R32UINT view.
			std::vector<D3D12_RECT>	clearRect(bb->m_clearRequests.size());
			for (size_t i = 0; i < bb->m_clearRequests.size(); ++i) {
				LONG left = (LONG)(bb->m_clearRequests[i].first / m_formatSizeInByte);
				LONG right = (LONG)((bb->m_clearRequests[i].first + bb->m_clearRequests[i].second) / m_formatSizeInByte);
				clearRect[i] = { left, 0, right, 1};
			}
			bb->m_clearRequests.clear();

			UINT cv[4] = { 0, 0, 0, 0 };

			// set gpu visible uav.
			GraphicsAPI::DescriptorTable dt;
			if (!dt.Allocate(currentGPUDescHeap, &m_oneUAVLayout, 0)) {
				Log::Fatal(L"Faild to allocate CPU desc heap for clear.");
				return Status::ERROR_INTERNAL;
			}
			if (!dt.Copy(dev, 0, 0, bb->m_cpuDesc->m_table)) {
				Log::Fatal(L"Faild to copy CPU desc heap for clear.");
				return Status::ERROR_INTERNAL;
			}

			{
				// NV driver was crashed with more than about 128 rects.
				// Not sure how many rects can be processed at once in other IHVs.
				size_t currentOfs = 0;
				while (currentOfs < clearRect.size()) {
					size_t numRects = currentOfs + 63 < clearRect.size() ? 63 : clearRect.size() - currentOfs;

					cmdList->m_apiData.m_commandList->ClearUnorderedAccessViewUint(
						dt.m_apiData.m_heapAllocationInfo.m_hGPU,
						bb->m_cpuDesc->m_table->m_apiData.m_heapAllocationInfo.m_hCPU,
						bb->m_buffer->m_apiData.m_resource,
						cv,
						(UINT)numRects,
						clearRect.data() + currentOfs);
					currentOfs += numRects;
				}
			}
#elif defined(GRAPHICS_API_VK)
			for (auto&& cr : bb->m_clearRequests) {
				vkCmdFillBuffer(
					cmdList->m_apiData.m_commandBuffer,
					bb->m_buffer->m_apiData.m_buffer,
					(VkDeviceSize)cr.first, // offset in bytes.
					(VkDeviceSize)cr.second, // size in bytes.
					0); // filldata.
			}
			bb->m_clearRequests.clear();
#endif
		}

		return Status::OK;
	}

	Status SharedBuffer::TransitionBarrier(GraphicsAPI::CommandList* cmdList, GraphicsAPI::ResourceState::State state)
	{
		// ------------
		// be carefull not to touch resources that are about to destruct.

		std::vector<GraphicsAPI::Resource*>			resArr;
		std::vector<GraphicsAPI::ResourceState::State>	stateArr;

		for (auto&& bbM : m_bufferBlocks) {
			auto& bb(bbM.second);
			if (bb->m_barrierRequest) {
				resArr.push_back(bb->m_buffer.get());
				stateArr.push_back(state);

				bb->m_barrierRequest = false;
			}
		}

		if (resArr.size() > 0) {
			if (!cmdList->ResourceTransitionBarrier(&resArr[0], resArr.size(), &stateArr[0])) {
				Log::Fatal(L"Faild to set resource state transition.");
				return Status::ERROR_INTERNAL;
			}
		}

		return Status::OK;
	}

	Status SharedBuffer::UAVBarrier(GraphicsAPI::CommandList* cmdList)
	{
		// ------------
		// be carefull not to touch resources that are about to destruct.

		std::vector<GraphicsAPI::Resource*>			resArr;

		for (auto&& bbM : m_bufferBlocks) {
			auto& bb(bbM.second);
			if (bb->m_barrierRequest) {
				resArr.push_back(bb->m_buffer.get());

				bb->m_barrierRequest = false;
			}
		}

		if (resArr.size() > 0) {
			if (!cmdList->ResourceUAVBarrier(&resArr[0], resArr.size())) {
				Log::Fatal(L"Faild to set resource UAV barrier.");
				return Status::ERROR_INTERNAL;
			}
		}

		return Status::OK;
	}

	Status SharedBuffer::BatchMap(GraphicsAPI::Device* dev, GraphicsAPI::Buffer::MapType mapType)
	{
		// ------------
		// be carefull not to touch resources that are about to destruct.

		if (m_cpuAccess == GraphicsAPI::Buffer::CpuAccess::None) {
			Log::Fatal(L"Invalid map operation detected");
			return Status::ERROR_INTERNAL;
		}

		for (auto&& bbM : m_bufferBlocks) {
			auto& bb(bbM.second);
			if (bb->m_mappedPtr != 0) {
				Log::Fatal(L"Invalid map operation detected");
				return Status::ERROR_INTERNAL;
			}
			if (!bb->m_batchMapRequest)
				continue;

			uint64_t readRangeEnd = 0;
			if (mapType == GraphicsAPI::Buffer::MapType::Read) {
				readRangeEnd = bb->m_buffer->m_sizeInBytes;
			}

			bb->m_mappedPtr = reinterpret_cast<intptr_t>(bb->m_buffer->Map(dev, mapType, 0, 0, readRangeEnd));
			bb->m_batchMapRequest = false;
		}

		return Status::OK;
	}

	Status SharedBuffer::BatchUnmap(GraphicsAPI::Device* dev, GraphicsAPI::Buffer::MapType mapType)
	{
		// ------------
		// be carefull not to touch resources that are about to destruct.

		for (auto&& bbM : m_bufferBlocks) {
			auto& bb(bbM.second);
			if (bb->m_mappedPtr == 0)
				continue;

			uint64_t writeRangeEnd = 0;
			if (mapType == GraphicsAPI::Buffer::MapType::Write || mapType == GraphicsAPI::Buffer::MapType::WriteDiscard)
				writeRangeEnd = bb->m_buffer->m_sizeInBytes;

			bb->m_buffer->Unmap(dev, 0, 0, writeRangeEnd);
			bb->m_mappedPtr = 0;
		}

		return Status::OK;
	}

	decltype(SharedBuffer::m_bufferBlocks)::iterator SharedBuffer::AddBufferBlock(PersistentWorkingSet* pws, size_t allocationSizeInBytes)
	{
		size_t elmSize = allocationSizeInBytes;
		if (m_formatSizeInByte > 0) {
			elmSize /= m_formatSizeInByte;
		}

		auto buf = pws->CreateBufferResource(elmSize, m_format, m_bindFlags, m_cpuAccess, m_bufferBlockKind);
		if (!buf) {
			Log::Fatal(L"Faild to create a new buffer.");
			return m_bufferBlocks.end();
		}
		buf->SetName(DebugName(m_debugName));

		std::unique_ptr<GraphicsAPI::UnorderedAccessView>			uav;
		std::unique_ptr<SharedCPUDescriptorHeap::SharedTableEntry>	cpuDesc;

		if (m_useClear) {
			uav = std::make_unique<GraphicsAPI::UnorderedAccessView>();

			if (!uav->Init(&pws->m_device, buf.get())) {
				Log::Fatal(L"Faild to create a uav for a new buffer.");
				return m_bufferBlocks.end();
			}
			cpuDesc = pws->m_UAVCPUDescHeap1->Allocate(&pws->m_device);
			if (!cpuDesc->m_table->SetUav(&pws->m_device, 0, 0, uav.get())) {
				Log::Fatal(L"Faild to set cpu descriptor.");
				return m_bufferBlocks.end();
			}
		}

		uint64_t	gPtr = (uint64_t)-1;
		if (m_useGPUPtr) {
			gPtr = buf->GetGpuAddress();
		}

		auto bb = std::make_unique<BufferBlock>();
		bb->m_buffer = std::move(buf);
		bb->m_uav = std::move(uav);
		bb->m_cpuDesc = std::move(cpuDesc);
		bb->m_gpuPtr = gPtr;

		auto [itr, sts] = m_bufferBlocks.insert({ reinterpret_cast<intptr_t>(bb.get()), std::move(bb) });
		if (!sts) {
			Log::Fatal(L"Faild to insert new bufferBlock..");
		}

		return itr;
	}

	// call dtor of corresponded BufferBlock.
	void SharedBuffer_Impl<void>::ReleaseAllocation(BufferEntry* ent)
	{
		ent->m_uav.reset();

		if (!ent->m_isAllocatedExclusively) {
			// this should always be true in this configuration.
			Log::Fatal(L"Failed to release shared buffer allocation..");
		}
		else {
			auto itr = m_bufferBlocks.find(reinterpret_cast<intptr_t>(ent->m_block));
			if (itr == m_bufferBlocks.end()) {
				Log::Fatal(L"Failed to release shared buffer allocation.");
			}
			m_bufferBlocks.erase(itr);
		}
	}

	// Simply allocate new BufferBlock for each allocation.
	std::unique_ptr<SharedBuffer::BufferEntry> SharedBuffer_Impl<void>::Allocate(PersistentWorkingSet* pws, size_t requestedSizeInBytes, bool useUAV)
	{
		if (useUAV && !is_set(m_bindFlags, GraphicsAPI::Buffer::BindFlags::UnorderedAccess)) {
			Log::Fatal(L"Faild to Allocate. useUAV needs BindFlags::UnorderedAccess at initialization.");
			return std::unique_ptr<SharedBuffer::BufferEntry>();
		}

		size_t allocationSize = GraphicsAPI::ALIGN(m_alignmentSizeInBytes, requestedSizeInBytes);

		// always allocate a new buffer;
		auto itr = AddBufferBlock(pws, allocationSize);
		if (itr == m_bufferBlocks.end()) {
			Log::Fatal(L"Faild to allocate exclusive memory chunk.");
			return std::unique_ptr<SharedBuffer::BufferEntry>();
		}
		bool allocatedExclusively = true;

		auto* bb = itr->second.get();
		auto ent = std::make_unique<ClassifiedBufferEntry>(&pws->m_resourceLogger, m_bufferEntryKind, requestedSizeInBytes);
		ent->m_manager = this;
		ent->m_block = bb;
		ent->m_isAllocatedExclusively = allocatedExclusively;
		if (useUAV) {
			ent->m_uav = std::make_unique<GraphicsAPI::UnorderedAccessView>();
			{
				size_t elmOfs = 0;
				size_t elmSize = allocationSize;
				if (m_formatSizeInByte > 0)
					elmSize /= m_formatSizeInByte;

				ent->m_uav->Init(&pws->m_device, ent->m_block->m_buffer.get(), (uint32_t)elmOfs, (uint32_t)elmSize);
			}
		}
		ent->m_offset = 0;
		ent->m_size = allocationSize;

		return std::move(ent);
	};

	Status SharedBuffer_Impl<void>::CheckUnusedBufferBlocks(uint64_t /*framesToRemove*/)
	{
		// there is no shared buffer block in this implementation.
		return Status::OK;
	}

	// Tell releasing allocation of a part of BufferBlock. Buffer block remain unchanged with shared blocks.
	template<typename Allocator>
	void SharedBuffer_VirtualAllocatorImpl<Allocator>::ReleaseAllocation(BufferEntry* ent)
	{
		ent->m_uav.reset();

		if (ent->m_isAllocatedExclusively) {
			// It was a unique buffer allocation.
			auto itr = m_bufferBlocks.find(reinterpret_cast<intptr_t>(ent->m_block));
			if (itr == m_bufferBlocks.end()) {
				Log::Fatal(L"Failed to release shared buffer allocation.");
			}
			m_bufferBlocks.erase(itr);
		}
		else {
			// It belonged to a shared block
			m_allocator.Free(ent->m_globalOffset);
		}
	}

	template<typename Allocator>
	Status SharedBuffer_VirtualAllocatorImpl<Allocator>::Init(
		GraphicsAPI::Device* dev,
		uint64_t allocationAlignmentInBytes, bool useClear, bool useGPUPtr,
		uint64_t blockSizeInBytesOrNumElements, GraphicsAPI::Resource::Format format,
		GraphicsAPI::Resource::BindFlags bindFlags, GraphicsAPI::Buffer::CpuAccess cpuAccess,
		ResourceLogger::ResourceKind bufferBlockKind, ResourceLogger::ResourceKind bufferEntryKind,
		const std::wstring& debugName)
	{
		auto sts = SharedBuffer::Init(
			dev, allocationAlignmentInBytes, useClear, useGPUPtr,
			blockSizeInBytesOrNumElements, format,
			bindFlags, cpuAccess,
			bufferBlockKind, bufferEntryKind, debugName);
		if (sts!= Status::OK)
			return sts;

		if (!m_allocator.Init(
			true,					// allowMultipleBlocks,
			m_blockSizeInBytes,		// blockSizeInBytes,
			m_alignmentSizeInBytes)	// allocationPageSizeInBytes
			) {
			Log::Fatal(L"Failed to initialize allocator.");
			return Status::ERROR_INTERNAL;
		}

		return sts;
	}

	// Allocate a part of shared buffer if possible.
	template<typename Allocator>
	std::unique_ptr<SharedBuffer::BufferEntry> SharedBuffer_VirtualAllocatorImpl<Allocator>::Allocate(PersistentWorkingSet* pws, size_t requestedSizeInBytes, bool useUAV)
	{
		std::unique_ptr<SharedBuffer::BufferEntry> ret_ent;

		if (requestedSizeInBytes == 0) {
			Log::Fatal(L"Zero byte allocation happened.");
			return std::move(ret_ent);
		}

		size_t allocationSize = GraphicsAPI::ALIGN(m_alignmentSizeInBytes, requestedSizeInBytes);

		BufferBlock* foundBlock = nullptr;
		size_t localOffset = 0;
		size_t globalOffset = (size_t)-1;
		bool isAllocatedExclusively = false;

		if (allocationSize > m_blockSizeInBytes / 2) {
			// Allocation size is bigger than the half of shaerd block size.
			auto itr = AddBufferBlock(pws, allocationSize);
			if (itr == m_bufferBlocks.end()) {
				Log::Fatal(L"Faild to allocate exclusive memory chunk.");
				return std::move(ret_ent);
			}
			foundBlock = itr->second.get();
			isAllocatedExclusively = true;
			Log::Info(L"A large allocation (%fMB)MB in a shared resource (%s) occurred. Please consider using more larger memory block (currently %.02fMB).",
				(double)allocationSize / (1024.0 * 1024.0),
				m_debugName.c_str(),
				(double)m_blockSizeInBytes / (1024.0 * 1024.0));
		}
		else {
			// search sutable allocation.
			if (!m_allocator.Alloc(allocationSize, &globalOffset)) {
				Log::Fatal(L"Faild to allocate shared memory chunk.");
				return std::move(ret_ent);
			}
			else {
				// globalOffset consists of blockID and localOffset.
				size_t blockID = globalOffset / m_blockSizeInBytes;
				localOffset = globalOffset - (blockID * m_blockSizeInBytes);

				auto itr = m_sharedBlocks.find((uint32_t)blockID);
				if (itr == m_sharedBlocks.end()) {
					// (re)allocate a new buffer for shared block.
					foundBlock = AddBufferBlock(pws, m_blockSizeInBytes)->second.get();
					m_sharedBlocks.insert({(uint32_t)blockID, foundBlock});
				}
				else {
					foundBlock = itr->second;
				}
			}
		}
		if (foundBlock == nullptr) {
			Log::Fatal(L"Faild to allocate shared memory chunk.");
			return std::move(ret_ent);
		}

		ret_ent = std::make_unique<ClassifiedBufferEntry>(&pws->m_resourceLogger, m_bufferEntryKind, requestedSizeInBytes);
		ret_ent->m_manager = this;
		ret_ent->m_block = foundBlock;
		ret_ent->m_isAllocatedExclusively = isAllocatedExclusively;
		if (useUAV) {
			ret_ent->m_uav = std::make_unique<GraphicsAPI::UnorderedAccessView>();
			{
				size_t elmOfs = localOffset;
				size_t elmSize = allocationSize;
				if (m_formatSizeInByte > 0) {
					elmOfs /= m_formatSizeInByte;
					elmSize /= m_formatSizeInByte;
				}
				ret_ent->m_uav->Init(&pws->m_device, ret_ent->m_block->m_buffer.get(), (uint32_t)elmOfs, (uint32_t)elmSize);
			}
		}
		ret_ent->m_globalOffset = globalOffset;
		ret_ent->m_offset = localOffset;
		ret_ent->m_size = allocationSize;

		return std::move(ret_ent);
	};

	template<typename Allocator>
	Status SharedBuffer_VirtualAllocatorImpl<Allocator>::CheckUnusedBufferBlocks(uint64_t framesToRemove)
	{
		size_t nbBlocks = m_allocator.NumberOfBlocks();
		if (nbBlocks == 0) {
			// there is no shared block.
			m_usingBlockStatus.m_freeFrames.clear();
			return Status::OK;
		}

		std::vector<uint32_t>	currentIDs(nbBlocks);
		std::vector<uint32_t>	currentOccupancy(nbBlocks);
		m_allocator.BlockStatus(currentIDs.data(), currentOccupancy.data());

		if (m_usingBlockStatus.m_freeFrames.size() != nbBlocks) {
			// number of blocks has been changed. Reset all counter.
			m_usingBlockStatus.m_freeFrames.resize(nbBlocks);
			std::fill(m_usingBlockStatus.m_freeFrames.begin(), m_usingBlockStatus.m_freeFrames.end(), 0);
			std::swap(m_usingBlockStatus.m_blockIDs, currentIDs);
			return Status::OK;
		}
		if (!std::equal(m_usingBlockStatus.m_blockIDs.begin(), m_usingBlockStatus.m_blockIDs.end(), currentIDs.begin())) {
			// ID array has been changed. Reset all counter.
			m_usingBlockStatus.m_freeFrames.resize(nbBlocks);
			std::fill(m_usingBlockStatus.m_freeFrames.begin(), m_usingBlockStatus.m_freeFrames.end(), 0);
			std::swap(m_usingBlockStatus.m_blockIDs, currentIDs);
			return Status::OK;
		}

		for (size_t i = 0; i < currentOccupancy.size(); ++i) {
			if (currentOccupancy[i] == 1) {
				// not unused or already have removed.
				m_usingBlockStatus.m_freeFrames[i] = 0;
				continue;
			}
			++m_usingBlockStatus.m_freeFrames[i];
		}

		std::vector<uint32_t>	aIDsToRemove;
		{
			size_t idxToRemove = m_usingBlockStatus.m_freeFrames.size();
			size_t numberToRemove = 10;

			while (idxToRemove > 0) {
				if (m_usingBlockStatus.m_freeFrames[--idxToRemove] < framesToRemove)
					continue;

				uint32_t aID = m_usingBlockStatus.m_blockIDs[idxToRemove]; // allocator ID.

																		   // safely removes unused block.
				BufferBlock* b = nullptr;
				auto itr = m_sharedBlocks.find(aID);
				assert(itr != m_sharedBlocks.end());
				b = itr->second;
				assert(b != nullptr);

				aIDsToRemove.push_back(aID); // store allocator ID.

				// Immediately delete the buffer block, since all Entries that used the corresponded block should already be freed with DeferredRelease.
				m_bufferBlocks.erase(reinterpret_cast<intptr_t>(b));
				m_sharedBlocks.erase(aID);
				if (--numberToRemove == 0)
					break;
			};
		}

		if (aIDsToRemove.size() > 0) {
			// remove from the allocator.
			m_allocator.RemoveUnusedBlocks(aIDsToRemove);

			// reset all counter once if any of shared block has been removed.
			std::fill(m_usingBlockStatus.m_freeFrames.begin(), m_usingBlockStatus.m_freeFrames.end(), 0);
		}

		return Status::OK;
	}

	template class SharedBuffer_VirtualAllocatorImpl<VirtualAllocator::BuddyAllocator>;
	template class SharedBuffer_VirtualAllocatorImpl<VirtualAllocator::FixedPageAllocator>;
	template class SharedBuffer_Impl<VirtualAllocator::BuddyAllocator>;
	template class SharedBuffer_Impl<VirtualAllocator::FixedPageAllocator>;

};
