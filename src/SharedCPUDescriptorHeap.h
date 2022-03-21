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

#include <deque>
#include <list>
#include <set>

namespace KickstartRT_NativeLayer
{
    class SharedCPUDescriptorHeap
    {
    protected:
        GraphicsAPI::DescriptorHeap::Type       m_descType;
        GraphicsAPI::DescriptorTableLayout		m_fixedLayout;
        size_t                                  m_fixedAllocationSize;
        size_t                                  m_totalNumberOfDescTableInHeapBlock = 0;

        struct SharedHeapBlock
        {
            GraphicsAPI::DescriptorHeap				    m_heap;
            size_t	                                    m_totalCreated = 0;
            std::list<GraphicsAPI::DescriptorTable*>	m_availableTables;
            std::set<GraphicsAPI::DescriptorTable*>     m_usingTables;
        };
        std::deque<std::unique_ptr<SharedHeapBlock>>    m_heapBlocks;

    public:
        struct SharedTableEntry
        {
        protected:
            SharedCPUDescriptorHeap* const m_manager;
            SharedHeapBlock* const         m_heapBlock;

        public:
            GraphicsAPI::DescriptorTable* const m_table;

        public:
            SharedTableEntry(SharedCPUDescriptorHeap* manager, SharedHeapBlock* heapBlock, GraphicsAPI::DescriptorTable* table) :
                m_manager(manager),
                m_heapBlock(heapBlock),
                m_table(table)
            {};

            ~SharedTableEntry()
            {
                auto itr = m_heapBlock->m_usingTables.find(m_table);

                // invalid desctable
                if (itr == m_heapBlock->m_usingTables.end()) {
                    Log::Fatal(L"Failed to release a descriptor heap entry");
                }
                else {
                    // remove from used entry and put it to the tail of available list.
                    m_heapBlock->m_usingTables.erase(itr);
                    m_heapBlock->m_availableTables.push_back(m_table);
                }
            };
        };

    public:
        Status Init(GraphicsAPI::Device* dev,
            GraphicsAPI::DescriptorHeap::Type type, size_t fixeAllocationSize, size_t heapBlockSize);
        std::unique_ptr<SharedTableEntry> Allocate(GraphicsAPI::Device* dev);
    };
};
