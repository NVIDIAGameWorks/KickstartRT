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
#include <SharedCPUDescriptorHeap.h>
#include <Utils.h>

namespace KickstartRT_NativeLayer
{
	Status SharedCPUDescriptorHeap::Init(GraphicsAPI::Device* dev,
		GraphicsAPI::DescriptorHeap::Type type, size_t fixeAllocationSize, size_t heapBlockSize)
	{
		m_descType = type;
		m_fixedAllocationSize = fixeAllocationSize;
		m_totalNumberOfDescTableInHeapBlock = GraphicsAPI::ALIGN(m_fixedAllocationSize, heapBlockSize);

		// create desc layout
		m_fixedLayout.AddRange(m_descType, 0, (uint32_t)m_fixedAllocationSize, 0);
		m_fixedLayout.SetAPIData(dev);

		return Status::OK;
	};

	std::unique_ptr<SharedCPUDescriptorHeap::SharedTableEntry> SharedCPUDescriptorHeap::Allocate(GraphicsAPI::Device* dev)
	{
		SharedHeapBlock* availableH = nullptr;

		// try to find a entry form exisitng pools
		for (auto&& h : m_heapBlocks) {
			if (h->m_availableTables.size() == 0 && h->m_totalCreated >= m_totalNumberOfDescTableInHeapBlock)
				continue;
			availableH = h.get();
			break;
		}

		if (availableH == nullptr) {
			// create a new CPU desc heap.
			using DH = GraphicsAPI::DescriptorHeap;
			DH::Desc	desc = {};
			desc.m_totalDescCount = (uint32_t)m_totalNumberOfDescTableInHeapBlock;
			desc.m_descCount[DH::value(m_descType)] = desc.m_totalDescCount;

			auto newH = std::make_unique<SharedHeapBlock>();
			if (!newH->m_heap.Create(dev, desc, false)) {
				Log::Fatal(L"Failed to create descriptor heap pool");
				return std::unique_ptr<SharedCPUDescriptorHeap::SharedTableEntry>();
			}
			newH->m_heap.SetName(DebugName(L"Shared CPU Descriptor."));

			availableH = newH.get();
			m_heapBlocks.push_back(std::move(newH));
		}

		GraphicsAPI::DescriptorTable* retTable = nullptr;

		if (availableH->m_availableTables.size() > 0) {
			// reuse an available entry.
			retTable = availableH->m_availableTables.front();
			availableH->m_availableTables.pop_front();
		}
		else
		{
			// create new entry.
			auto dt = std::make_unique<GraphicsAPI::DescriptorTable>();
			if (!dt->Allocate(&availableH->m_heap, &m_fixedLayout, 0)) {
				Log::Fatal(L"Failed to allocate descriptor table from a pool.");
				return std::unique_ptr<SharedCPUDescriptorHeap::SharedTableEntry>();
			}
			retTable = dt.release();
			availableH->m_totalCreated += m_fixedAllocationSize;
		}
		availableH->m_usingTables.insert(retTable);

		return std::make_unique<SharedCPUDescriptorHeap::SharedTableEntry>(this, availableH, retTable);
	}
};
