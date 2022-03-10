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

#include <type_traits>
#include <tuple>
#include <vector>
#include <deque>
#include <array>
#include <map>
#include <set>
#include <bitset>

#define VIRTUAL_ALLOCATOR_ENALBLE_STRING_DUMP
#if defined(VIRTUAL_ALLOCATOR_ENALBLE_STRING_DUMP)
#include <iostream>
#include <string>
#include <sstream>
#endif

#define VIRTUAL_ALLOCATOR_CHECK_ASSERT
#if defined(VIRTUAL_ALLOCATOR_CHECK_ASSERT)
#include <cassert>
#endif

namespace VirtualAllocator {
#if !defined(VIRTUAL_ALLOCATOR_CHECK_ASSERT)
#if !defined(assert)
#define assert(ignore) ((void)0)
#endif
#endif

	enum class Type {
		FixedPage,
		Buddy
	};

	template<Type type>
	class Allocator {
	};

	// This allocator doesn't manage actual memory, just provides an offset for a requested allocation size.
	// It manages virtual memory space in a pretty simple way, by fixed page size. (e.g. manage 256MB with 64KB entry size, Maximum number of pages is 4096)
	// It returns offset and allocation handle which can be used to free the allocation in faster way.
	// If it is set to allow multiple virtual memory blocks at initialization, it will return an allocation in new block of memory when existed blocks are full. In this case, it will return an offset with block sized offsets. 
	// Allocation strategy is pretty simple, It simply searches for requested memory page in free continuous page blocks.
	// If found free block is large enough to split, allocator will split the block, otherwise it simply gives the free block to avoid fragmentation.
	// (Please refer the following code around m_smallPageThreshold. This controls the strategy for splitting chunks and thus controls fragmentation.)
	template<>
	class Allocator<Type::FixedPage> {
		struct Entry {
			uint32_t	m_offset = uint32_t(-1);
			uint32_t	m_nbPages = uint32_t(-1);
			size_t		m_realUsed = size_t(-1);

			Entry* m_previous = nullptr;
			Entry* m_next = nullptr;

			std::map<uint32_t, Entry*>::iterator		m_offsetItr;
			std::multimap<uint32_t, Entry*>::iterator	m_freeItr;
		};

		class EntryBank {
			static constexpr size_t				m_entryArraySize = 64;
			std::deque<std::array<Entry, 64>*>	m_entryBank;
			size_t								m_newEntryIdx = m_entryArraySize;
			Entry*								m_freedEntries = nullptr;
			size_t								m_totalAllocatedEntry = 0;

		public:
			Entry* Alloc()
			{
				++m_totalAllocatedEntry;

				if (m_freedEntries != nullptr) {
					Entry* retPtr = m_freedEntries;
					m_freedEntries = m_freedEntries->m_next;
					retPtr->m_next = nullptr;

					return retPtr;
				}

				if (m_newEntryIdx == m_entryArraySize) {
					m_entryBank.push_back(new std::array<Entry, 64>());
					m_newEntryIdx = 0;
				}

				return &((*m_entryBank.back())[m_newEntryIdx++]);
			}

			void Free(Entry* ptr)
			{
				--m_totalAllocatedEntry;

				ptr->m_next = m_freedEntries;
				m_freedEntries = ptr;
			}

#if defined(VIRTUAL_ALLOCATOR_ENALBLE_STRING_DUMP)
			std::string Dump() const
			{
				std::stringstream ss;
				size_t cnt = 0;
				Entry* e = m_freedEntries;
				while (e != nullptr) {
					++cnt;
					e = e->m_next;
				};
				ss << "ArraySize:" << m_entryArraySize << " ArrayCount:" << m_entryBank.size() << " FreedEntry:" << cnt << " TotalUsing:" << m_totalAllocatedEntry << std::endl;

				return ss.str();
			}
#endif
		};

		struct Block {
			uint32_t	m_id = (uint32_t)-1;
			Entry*		m_entries = {};
			uint32_t	m_allocatedInPages = 0;
			uint32_t	m_largestFreeInPages = 0;
			std::map<uint32_t, Entry*>			m_offsetMap;
			std::multimap<uint32_t, Entry*>		m_freePages;
		};

		struct BlockContainer {
			bool						m_needToSearchUniqueID = false;
			uint32_t					m_nextID = 0;
			std::vector<Block*>			m_blockVec; // it's for traversing all blocks.
			std::map<uint32_t, Block*>	m_blockMap;	// it's for retrieving a block by its ID.

			Block* Find(uint32_t blockID) const
			{
				auto itr = m_blockMap.find(blockID);
				if (itr == m_blockMap.end()) {
					assert(false);
					return nullptr;
				}
				return itr->second;
			}

			Block *AddNew()
			{
				Block* newBlock = new Block();
				newBlock->m_id = m_nextID++;
				m_blockVec.push_back(newBlock);
				m_blockMap.insert({ newBlock->m_id, newBlock });

				if (m_nextID == 0 || m_needToSearchUniqueID) {
					// ID have reached to 0xFFFF'FFFF so need to search unique ID with existed blocks.
					m_needToSearchUniqueID = true;

					for (;;) {
						if (m_blockMap.find(m_nextID) == m_blockMap.end())
							break;
						++m_nextID;
					}
				}

				return newBlock;
			}

			bool Remove(uint32_t blockIDToRemove)
			{
				std::vector<Block*> newVec;
				newVec.reserve(m_blockVec.size()-1);

				bool removed = false;
				for (auto itr = m_blockMap.begin(); itr != m_blockMap.end();) {
					if (itr->first == blockIDToRemove) {
						itr = m_blockMap.erase(itr);
						removed = true;
						continue;
					}
					newVec.push_back(itr->second);
					++itr;
				}
				std::swap(m_blockVec, newVec);

				return removed;
			}

			bool Remove(const std::vector<uint32_t>& blockIDsToRemove)
			{
				std::vector<Block*> newVec;
				newVec.reserve(m_blockVec.size());

				uint32_t removedCnt = 0;
				for (auto itr = m_blockMap.begin(); itr != m_blockMap.end();) {
					bool removed = false;
					for (auto&& bID : blockIDsToRemove) {
						if (itr->first == bID) {
							itr = m_blockMap.erase(itr);
							removed = true;
							++removedCnt;
							break;
						}
					}
					if (!removed) {
						newVec.push_back(itr->second);
						++itr;
					}
				}
				std::swap(m_blockVec, newVec);

				return removedCnt == blockIDsToRemove.size();
			}
		};

		bool		m_allowMultipleBlocks = false;
		size_t		m_pageSizeInBytes = 0;
		size_t		m_blockSizeInBytes = 0;
		uint32_t	m_pagesInBlock = 0;

		uint32_t	m_smallPageThreshold = 0;

		size_t		m_totalAllocatedSizeInBytes = 0;

		EntryBank		m_entryBank;
		BlockContainer	m_blockContainer;


		static inline void PickMiddle(Entry* prev, Entry* /*current*/, Entry* next) {
			if (prev != nullptr)
				prev->m_next = next;
			if (next != nullptr)
				next->m_previous = prev;
		};
		static inline void InsertMiddle(Entry* prev, Entry* current, Entry* next) {
			if (prev != nullptr)
				prev->m_next = current;
			if (next != nullptr)
				next->m_previous = current;

			current->m_previous = prev;
			current->m_next = next;
		};

	public:
		bool Init(bool allowMultipleBlocks, size_t blockSizeInBytes, size_t allocationPageSizeInBytes)
		{
			m_allowMultipleBlocks = allowMultipleBlocks;
			m_blockSizeInBytes = blockSizeInBytes;
			m_pageSizeInBytes = allocationPageSizeInBytes;
			m_pagesInBlock = (uint32_t)(blockSizeInBytes / allocationPageSizeInBytes);

			// Setting a default value for small page threshold.
			// less than 1/128 of the block size will be treated as small buffer.
			m_smallPageThreshold = m_pagesInBlock / 128;

			if (m_pagesInBlock == 0)
				return false;

			return true;
		}

		void SetSmallPageThreshold(uint32_t numberOfPages)
		{
			m_smallPageThreshold = numberOfPages;
		}

		bool Alloc(size_t siz, size_t* offset)
		{
			if (siz > m_blockSizeInBytes) {
				assert(siz <= m_blockSizeInBytes);
				return false;
			}

			uint32_t nbPages = (uint32_t)((siz + m_pageSizeInBytes - 1) / m_pageSizeInBytes);

			Block* foundBlock = nullptr;
			for (auto&& b : m_blockContainer.m_blockVec) {
				if (b->m_largestFreeInPages < nbPages)
					continue;
				foundBlock = b;
				break;
			}

			// Failed to find a block to accomodate the requested size.
			if (foundBlock == nullptr) {
				if (!m_allowMultipleBlocks && m_blockContainer.m_blockVec.size() > 0) {
					return false;
				}
				// allocate new block. 
				{
					Block* newBlock = m_blockContainer.AddNew();
					Entry* ent = m_entryBank.Alloc();

					ent->m_offset = newBlock->m_id * m_pagesInBlock;
					ent->m_nbPages = m_pagesInBlock;
					ent->m_realUsed = 0;
					ent->m_previous = nullptr;
					ent->m_next = nullptr;
					ent->m_freeItr = newBlock->m_freePages.insert({ ent->m_nbPages, ent });
					ent->m_offsetItr = newBlock->m_offsetMap.insert({ ent->m_offset, ent }).first;

					newBlock->m_entries = ent;
					newBlock->m_largestFreeInPages = ent->m_nbPages;

					foundBlock = newBlock;
				}
			}

			// pick the smallest entry that can accomodate the requested size. 
			Entry* foundEnt;
			{
				auto itr = foundBlock->m_freePages.lower_bound(nbPages);
				if (itr == foundBlock->m_freePages.end()) {
					// Allocation failed. it shouldn't be happened.
					assert(itr != foundBlock->m_freePages.end());
					return false;
				}
				foundEnt = itr->second;
			}

			if ((foundEnt->m_nbPages == nbPages) ||
				(nbPages <= m_smallPageThreshold && foundEnt->m_nbPages <= (nbPages + (nbPages + 1) / 2))) {
				// in case of perfect fit or small sized allocation,
				// if found entry size is less than 3/2 of the requested size, it simply gives the chunk to avoid fragmentation.
				foundBlock->m_freePages.erase(foundEnt->m_freeItr);
				foundEnt->m_freeItr = decltype(foundBlock->m_freePages)::iterator();
				foundEnt->m_realUsed = siz;

				// update allocated and the largest free size if needed.
				foundBlock->m_allocatedInPages += foundEnt->m_nbPages;
				if (foundBlock->m_largestFreeInPages == foundEnt->m_nbPages) {
					if (foundBlock->m_freePages.size() > 0)
						foundBlock->m_largestFreeInPages = foundBlock->m_freePages.rbegin()->first;
					else
						foundBlock->m_largestFreeInPages = 0;
				}

				m_totalAllocatedSizeInBytes += siz;

				*offset = (size_t)foundEnt->m_offset * m_pageSizeInBytes;

				return true;
			}

			// Split the entry and give the latter part.
			{
				uint32_t originalPages = foundEnt->m_nbPages;

				Entry* newEnt = m_entryBank.Alloc();
				newEnt->m_offset = foundEnt->m_offset + (foundEnt->m_nbPages - nbPages);
				newEnt->m_nbPages = nbPages;
				newEnt->m_realUsed = siz;
				newEnt->m_freeItr = decltype(foundBlock->m_freePages)::iterator();
				newEnt->m_offsetItr = foundBlock->m_offsetMap.insert({ newEnt->m_offset, newEnt }).first;
				InsertMiddle(foundEnt, newEnt, foundEnt->m_next);

				// update free map with the deducted size.
				foundEnt->m_nbPages -= nbPages;
				foundBlock->m_freePages.erase(foundEnt->m_freeItr);
				foundEnt->m_freeItr = foundBlock->m_freePages.insert({ foundEnt->m_nbPages, foundEnt });

				// update allocated and the largest free size if needed.
				foundBlock->m_allocatedInPages += newEnt->m_nbPages;
				if (foundBlock->m_largestFreeInPages == originalPages) {
					foundBlock->m_largestFreeInPages = foundBlock->m_freePages.rbegin()->first;
				}

				m_totalAllocatedSizeInBytes += siz;

				*offset = (size_t)newEnt->m_offset * m_pageSizeInBytes;
			}

			return true;
		}

		bool Free(size_t offset)
		{
			uint32_t key = (uint32_t)(offset / m_pageSizeInBytes);
			uint32_t blockID = key / m_pagesInBlock;

			// make sure the page size alignment.
			if (key * m_pageSizeInBytes != offset) {
				assert(false);
				return false;
			}

			Block* foundBlock = m_blockContainer.Find(blockID);
			if (foundBlock == nullptr) {
				// invalid block ID detected.
				assert(false);
				return false;
			}

			auto& offsetMap(foundBlock->m_offsetMap);
			auto& freePages(foundBlock->m_freePages);

			auto offsetItr = offsetMap.find(key);
			if (offsetItr == offsetMap.end()) {
				// unknown entry detected.
				assert(false);
				return false;
			}

			Entry* foundEnt = offsetItr->second;

			foundBlock->m_allocatedInPages -= foundEnt->m_nbPages;
			m_totalAllocatedSizeInBytes -= foundEnt->m_realUsed;
			foundEnt->m_realUsed = 0;

			// merge free blocks if available.
			if (foundEnt->m_previous != nullptr) {
				Entry* prev = foundEnt->m_previous;
				if (prev->m_realUsed == 0) {
					// merege with prev. remove current.
					prev->m_nbPages += foundEnt->m_nbPages;

					PickMiddle(prev, foundEnt, foundEnt->m_next);
					offsetMap.erase(foundEnt->m_offsetItr);
					freePages.erase(prev->m_freeItr);
					m_entryBank.Free(foundEnt);

					foundEnt = prev;
				}
			}
			if (foundEnt->m_next != nullptr) {
				Entry* next = foundEnt->m_next;
				if (next->m_realUsed == 0) {
					// merege with next. remove next.
					foundEnt->m_nbPages += next->m_nbPages;

					PickMiddle(foundEnt, next, next->m_next);
					offsetMap.erase(next->m_offsetItr);
					freePages.erase(next->m_freeItr);
					m_entryBank.Free(next);
				}
			}

			// register (update) free entry;
			foundEnt->m_freeItr = freePages.insert({ foundEnt->m_nbPages, foundEnt });
			foundBlock->m_largestFreeInPages = std::max(foundBlock->m_largestFreeInPages, foundEnt->m_nbPages);

			return true;
		}

		bool RemoveUnusedBlocks(const std::vector<uint32_t> &blockIDsToRemove)
		{
			for (auto&& id : blockIDsToRemove) {
				auto itr = m_blockContainer.m_blockMap.find(id);
				if (itr == m_blockContainer.m_blockMap.end()) {
					// invalid block ID detected.
					assert(false);
					return false;
				}

				Block* b = itr->second;
				if (b->m_allocatedInPages > 0) {
					// This block is still active.
					assert(false);
					return false;
				}

				// top entry must be empty and hold entire block.
				Entry* topE = b->m_entries;
				if (topE == nullptr ||
					topE->m_nbPages != m_pagesInBlock ||
					topE->m_realUsed != 0 ||
					topE->m_next != nullptr ||
					topE->m_previous != nullptr) {
					assert(false);
					return false;
				}

				if (b->m_offsetMap.size() != 1 ||
					b->m_freePages.size() != 1) {
					assert(false);
					return false;
				}

				m_entryBank.Free(topE);
				b->m_entries = nullptr;
			}

			return m_blockContainer.Remove(blockIDsToRemove);
		}

		size_t NumberOfBlocks() const
		{
			return m_blockContainer.m_blockVec.size();
		}

		void BlockStatus(uint32_t *retIDs, uint32_t* retOccupancy) const
		{
			for (size_t i = 0; i < m_blockContainer.m_blockVec.size(); ++i) {
				retIDs[i] = m_blockContainer.m_blockVec[i]->m_id;

				if (m_blockContainer.m_blockVec[i]->m_allocatedInPages > 0)
					retOccupancy[i] = 1; // used block
				else
					retOccupancy[i] = 0;
			}
		}

#if defined(VIRTUAL_ALLOCATOR_ENALBLE_STRING_DUMP)
		std::string Dump(bool dumpEntry, bool dumpFreed, bool dumpVis) const
		{
			std::stringstream ss;

			for (auto&& b : m_blockContainer.m_blockVec) {
				if (dumpEntry || dumpVis || dumpFreed)
					ss << "BlockID:" << b->m_id << std::endl;

				if (dumpEntry) {
					ss << "Entry Dump" << std::endl;

					Entry* ent = b->m_entries;
					while (ent != nullptr) {
						ss << "U: " << ent->m_realUsed << " O: " << ent->m_offset << " S:" << ent->m_nbPages << std::endl;
						ent = ent->m_next;
					}
				}

				if (dumpVis) {
					ss << "Visualized Dump" << std::endl;
					Entry* ent = b->m_entries;
					std::array<char, 2> chArr = { '*', '+' };
					size_t chIdx = 0;
					size_t chCnt = 0;
					while (ent != nullptr) {
						if (ent->m_realUsed > 0) {
							for (size_t i = 0; i < ent->m_nbPages; i++) {
								ss << chArr[chIdx % chArr.size()];
								if (++chCnt % 64 == 0)
									ss << std::endl;
							}
							++chIdx;
						}
						else {
							for (size_t i = 0; i < ent->m_nbPages; i++) {
								ss << " ";
								if (++chCnt % 64 == 0)
									ss << std::endl;
							}
						}
						ent = ent->m_next;
					}
					ss << std::endl;
				}

				if (dumpEntry) {
					ss << "Offset Map" << std::endl;
					for (auto&& itr : b->m_offsetMap) {
						Entry* ent = itr.second;
						ss << "Key: " << itr.first << " U: " << ent->m_realUsed << " O: " << ent->m_offset << " S:" << ent->m_nbPages << std::endl;
					}
				}

				if (dumpFreed) {
					ss << "Freed Map" << std::endl;
					for (auto&& itr : b->m_freePages) {
						Entry* ent = itr.second;
						ss << "Key: " << itr.first << " U: " << ent->m_realUsed << " O: " << ent->m_offset << " S:" << ent->m_nbPages << std::endl;
					}
				}

				ss << "LargestFreeBlockInPages: " << b->m_largestFreeInPages << std::endl;
			}

			if (dumpEntry && dumpFreed && dumpVis) {
				// in case of full dump, Entry Banks is also get dumped.
				ss << "EntryBank" << std::endl;
				ss << m_entryBank.Dump();
			}

			size_t nbBlocks = m_blockContainer.m_blockVec.size();
			if (nbBlocks > 0) {
				size_t totalAllocatedBlocksInByte = m_blockSizeInBytes * nbBlocks;
				size_t totalAllocatedBlocksInPages = m_pagesInBlock * nbBlocks;
				size_t totalAllocatedPages = 0;
				for (auto&& b : m_blockContainer.m_blockVec)
					totalAllocatedPages += b->m_allocatedInPages;

				if (totalAllocatedBlocksInByte) {
					ss << "TotalAllocatedInBytes: " << m_totalAllocatedSizeInBytes << " : " << (double)m_totalAllocatedSizeInBytes * 100. / totalAllocatedBlocksInByte << "%" << std::endl;
					ss << "TotalAllocatedInPages: " << totalAllocatedPages << " : " << (double)totalAllocatedPages * 100. / totalAllocatedBlocksInPages << "%" << std::endl;
				}
				else {
					ss << "TotalAllocatedInBytes: " << m_totalAllocatedSizeInBytes << std::endl;
					ss << "TotalAllocatedInPages: " << totalAllocatedPages << std::endl;
				}
			}

			return ss.str();
		}
#endif
	};

	template<>
	class Allocator<Type::Buddy> final
	{
		bool	m_allowMultipleBlocks = false;
		size_t	m_pageSizeInBytes = 0;
		size_t	m_blockSizeInBytes = 0;

		size_t	m_totalAllocatedSizeInBytes = 0;

		struct Block {
			uint32_t										m_id = (uint32_t)-1;
			std::vector<std::set<size_t>>					m_freeList;
			std::map<size_t, std::tuple<size_t, size_t>>	m_usedMap;
			size_t											m_largestOrderP1 = 0; // zero means no free chunck.
			size_t											m_totalAllocatedPagesInBytes = 0;
		};

		struct BlockContainer {
			uint32_t					m_nextID = 0;
			std::vector<Block*>			m_blockVec; // it's for traversing all blocks.
			std::map<uint32_t, Block*>	m_blockMap;	// it's for retrieving a block by its ID.

			Block* Find(uint32_t blockID) const
			{
				auto itr = m_blockMap.find(blockID);
				if (itr == m_blockMap.end()) {
					assert(false);
					return nullptr;
				}
				return itr->second;
			}

			Block* AddNew()
			{
				Block* newBlock = new Block();
				newBlock->m_id = m_nextID++;
				m_blockVec.push_back(newBlock);
				m_blockMap.insert({ newBlock->m_id, newBlock });

				return newBlock;
			}

			bool Remove(uint32_t blockIDToRemove)
			{
				std::vector<Block*> newVec;
				newVec.reserve(m_blockVec.size() - 1);

				bool removed = false;
				for (auto itr = m_blockMap.begin(); itr != m_blockMap.end();) {
					if (itr->first == blockIDToRemove) {
						itr = m_blockMap.erase(itr);
						removed = true;
						continue;
					}
					newVec.push_back(itr->second);
					++itr;
				}
				std::swap(m_blockVec, newVec);

				return removed;
			}

			bool Remove(const std::vector<uint32_t>& blockIDsToRemove)
			{
				std::vector<Block*> newVec;
				newVec.reserve(m_blockVec.size());

				uint32_t removedCnt = 0;
				for (auto itr = m_blockMap.begin(); itr != m_blockMap.end();) {
					bool removed = false;
					for (auto&& bID : blockIDsToRemove) {
						if (itr->first == bID) {
							itr = m_blockMap.erase(itr);
							removed = true;
							++removedCnt;
							break;
						}
					}
					if (!removed) {
						newVec.push_back(itr->second);
						++itr;
					}
				}
				std::swap(m_blockVec, newVec);

				return removedCnt == blockIDsToRemove.size();
			}
		};

		BlockContainer								m_blockContainer;
		std::vector<size_t>							m_orderList;

		size_t Order(size_t requestedSize) const
		{
			for (size_t o = 0; o < m_orderList.size(); ++o) {
				if (m_orderList[o] >= requestedSize)
					return o;
			}

			return m_orderList.size();
		}

	public:
		bool Init(bool allowMultipleBlocks, size_t blockSizeInBytes, size_t allocationPageSizeInBytes)
		{
			// both block and page sizes need to be power of two.
			if (std::bitset<64>(blockSizeInBytes).count() != 1 ||
				std::bitset<64>(allocationPageSizeInBytes).count() != 1)
				return false;

			m_allowMultipleBlocks = allowMultipleBlocks;
			m_blockSizeInBytes = blockSizeInBytes;
			m_pageSizeInBytes = allocationPageSizeInBytes;

			size_t order = 0;
			{
				size_t pageSize = blockSizeInBytes;
				while (pageSize > 0) {
					++order;

					pageSize /= 2;
					if (pageSize < allocationPageSizeInBytes)
						break;
				};
			}

			assert(order > 0);

			// initialize order list.
			m_orderList.resize(order);
			{
				size_t pageSize = blockSizeInBytes;
				for (size_t i = order - 1;; --i) {
					m_orderList[i] = pageSize;
					pageSize /= 2;
					if (i == 0)
						break;
				}
			}

			return true;
		}

		bool Alloc(size_t siz, size_t* offset)
		{
			*offset = 0xFFFF'FFFF'FFFF'FFFF;

			// Calculate order to search first.
			size_t o = Order(siz);

			// requested size doesn't fit any order.
			if (o >= m_orderList.size())
				return false;

			Block* foundBlock = nullptr;
			for (auto&& b : m_blockContainer.m_blockVec) {
				if (b->m_largestOrderP1 < o+1)
					continue;
				foundBlock = b;
				break;
			}

			if (foundBlock == nullptr) {
				if (!m_allowMultipleBlocks && m_blockContainer.m_blockVec.size() > 0) {
					// allocation failed.
					return false;
				}

				// add new block.
				Block* newBlock = m_blockContainer.AddNew();
				size_t topOrder = m_orderList.size() - 1;
				newBlock->m_freeList.resize(m_orderList.size());
				newBlock->m_freeList[topOrder].insert(m_blockSizeInBytes * newBlock->m_id); // insert top block.
				newBlock->m_largestOrderP1 = topOrder+1;

				foundBlock = newBlock;
			}

			auto CheckTheLargestOrderP1 = [](Block* b) {
				size_t i = b->m_freeList.size();
				while (i > 0) {
					if (b->m_freeList[--i].size() > 0) {
						b->m_largestOrderP1 = i+1;
						return;
					}
				}
				b->m_largestOrderP1 = 0;
			};

			if (foundBlock->m_freeList[o].size() > 0)
			{
				// free list for the order is available. simply assign it.
				*offset = *foundBlock->m_freeList[o].begin();
				foundBlock->m_freeList[o].erase(foundBlock->m_freeList[o].begin());

				// Insert map with the start address (offset) key.
				foundBlock->m_usedMap.insert(std::make_pair(*offset, std::make_tuple(m_orderList[o], siz)));
				m_totalAllocatedSizeInBytes += siz;
				foundBlock->m_totalAllocatedPagesInBytes += m_orderList[o];

				CheckTheLargestOrderP1(foundBlock);

				return true;;
			}

			// Thre is no free block for the requested order, so split a larger block.
			size_t targetOrder;
			for (targetOrder = o + 1; targetOrder < m_orderList.size(); ++targetOrder) {
				if (foundBlock->m_freeList[targetOrder].size() > 0)
					break;
			}
			if (targetOrder == m_orderList.size()) {
				// There is no block to split. Shouldn't be here.
				assert(false);
				return false;
			}
			bool needToCheckTheLargest = targetOrder + 1 == foundBlock->m_largestOrderP1 ? true : false;

			size_t temp = *foundBlock->m_freeList[targetOrder].begin();
			foundBlock->m_freeList[targetOrder].erase(foundBlock->m_freeList[targetOrder].begin());

			// split until the requested order.
			for (size_t i = targetOrder - 1; i >= o; --i) {
				size_t ofs1 = temp;
				size_t ofs2 = temp + m_orderList[i];

				// keep the first part  in the free list.
				foundBlock->m_freeList[i].insert(ofs1);

				// slipt the latter part in the next loop;
				temp = ofs2;

				if (i == 0)
					break;
			}

			// temp should hold the requested order.
			foundBlock->m_usedMap.insert(std::make_pair(temp, std::make_tuple(m_orderList[o], siz)));
			m_totalAllocatedSizeInBytes += siz;
			foundBlock->m_totalAllocatedPagesInBytes += m_orderList[o];

			if (needToCheckTheLargest)
				CheckTheLargestOrderP1(foundBlock);

			*offset = temp;
			return true;;
		};

		bool Free(size_t offset)
		{
			uint32_t blockID = (uint32_t)(offset / m_blockSizeInBytes);
			Block* b = m_blockContainer.Find(blockID);
			if (b == nullptr) {
				assert(false);
				return false;
			}

			auto itr = b->m_usedMap.find(offset);
			assert(itr != b->m_usedMap.end());

			size_t allocatedOffset = itr->first;
			size_t allocatedSize, requestedSize;
			std::tie(allocatedSize, requestedSize) = itr->second;
			b->m_usedMap.erase(itr);
			b->m_totalAllocatedPagesInBytes -= allocatedSize;
			m_totalAllocatedSizeInBytes -= requestedSize;

			size_t startOrder = Order(allocatedSize);

			// merge free list until the top.
			size_t o = startOrder;
			for (; o < m_orderList.size(); ++o) {
				bool found = false;

				if (o < m_orderList.size() - 1) {
					size_t buddyOffset, topOffset;
					if (allocatedOffset / allocatedSize % 2 == 0) {
						// even block. Try checking odd block.
						buddyOffset = allocatedOffset + allocatedSize;
						topOffset = allocatedOffset;
					}
					else {
						// odd block. Try checking even block.
						buddyOffset = allocatedOffset - allocatedSize;
						topOffset = buddyOffset;
					}

					// Search in free list to find it's buddy
					auto freeItr  = b->m_freeList[o].find(buddyOffset);
					if (freeItr != b->m_freeList[o].end()) {
						// found free buddy in the list.
						b->m_freeList[o].erase(freeItr);

						found = true;
						allocatedOffset = topOffset;
						allocatedSize *= 2;;
					}
				}

				if (!found) {
					// add to the free list and exit.
					b->m_freeList[o].insert(allocatedOffset);
					break;
				}
			}
			assert(o < m_orderList.size());
			b->m_largestOrderP1 = std::max(o+1, b->m_largestOrderP1);

			return true;
		}

		bool RemoveUnusedBlocks(const std::vector<uint32_t>& blockIDsToRemove)
		{
			for (auto&& id : blockIDsToRemove) {
				auto itr = m_blockContainer.m_blockMap.find(id);
				if (itr == m_blockContainer.m_blockMap.end()) {
					// invalid block ID detected.
					assert(false);
					return false;
				}

				Block* b = itr->second;
				if (b->m_largestOrderP1 < m_orderList.size()) {
					assert(false);
					return false;
				}

				// there should be a free block in the top order.
				if (b->m_freeList[m_orderList.size() - 1].size() != 1) {
					assert(false);
					return false;
				}
			}

			return m_blockContainer.Remove(blockIDsToRemove);
		}

		size_t NumberOfBlocks() const
		{
			return m_blockContainer.m_blockVec.size();
		}

		void BlockStatus(uint32_t* retIDs, uint32_t* retOccupancy) const
		{
			for (size_t i = 0; i < m_blockContainer.m_blockVec.size(); ++i) {
				retIDs[i] = m_blockContainer.m_blockVec[i]->m_id;

				if (m_blockContainer.m_blockVec[i]->m_totalAllocatedPagesInBytes > 0)
					retOccupancy[i] = 1; // used block
				else
					retOccupancy[i] = 0;
			}
		}

#if defined(VIRTUAL_ALLOCATOR_ENALBLE_STRING_DUMP)
		std::string Dump(bool dumpEntry, bool dumpFreed, bool dumpVis) const
		{
			std::stringstream ss;

			for (size_t blockIdx = 0; blockIdx < m_blockContainer.m_blockVec.size(); ++blockIdx) {
				const Block* b = m_blockContainer.m_blockVec[blockIdx];

				if (dumpEntry || dumpVis || dumpFreed)
					ss << "Block:" << b->m_id << std::endl;
				size_t blockBegin = m_blockSizeInBytes * blockIdx;

				if (dumpEntry) {
					ss << "Used Map" << std::endl;
					for (const auto& itr : b->m_usedMap) {
						ss << " O: " << itr.first << " S:" << std::get<0>(itr.second) << std::endl;
					}
				}

				if (dumpVis) {
					ss << "Visualized Dump" << std::endl;

					size_t nbPages = m_blockSizeInBytes / m_pageSizeInBytes;

					std::array<char, 2> chArr = { '*', '+' };
					size_t chIdx = 0;
					size_t chCnt = 0;
					for (size_t i = 0; i < nbPages; i++) {
						size_t curOfs = blockBegin + i * m_pageSizeInBytes;
						auto itr = b->m_usedMap.find(curOfs);
						if (itr != b->m_usedMap.end()) {
							size_t usedPages = std::get<0>(itr->second) / m_pageSizeInBytes;
							i += usedPages - 1;
							while (usedPages > 0) {
								ss << chArr[chIdx % chArr.size()];
								if (++chCnt % 64 == 0)
									ss << std::endl;
								--usedPages;
							}
							++chIdx;
							continue;
						}
						ss << " ";
						if (++chCnt % 64 == 0)
							ss << std::endl;
					}
					ss << std::endl;
				}

				if (dumpFreed) {
					ss << "Freed Map" << std::endl;
					size_t i = 0;
					for (const auto& freeSet : b->m_freeList) {
						ss << "Order: " << i << "  Size: " << m_orderList[i] << std::endl;
						for (const auto& itr : freeSet) {
							ss << "Ofs: " << itr << std::endl;
						}
						++i;
					}
					ss << "LargestFreeOrder + 1:" << b->m_largestOrderP1 << std::endl;
				}
			}

			if (m_blockContainer.m_blockVec.size() > 0) {
				size_t totalAllocatedPagesInBytes = 0;
				for (const auto& b : m_blockContainer.m_blockVec) {
					totalAllocatedPagesInBytes += b->m_totalAllocatedPagesInBytes;
				}
				size_t totalBlockSizeInBytes = m_blockSizeInBytes * m_blockContainer.m_blockVec.size();

				ss << "TotalAllocatedInBytes: " << m_totalAllocatedSizeInBytes << " : " << (double)m_totalAllocatedSizeInBytes * 100. / totalBlockSizeInBytes << "%" << std::endl;
				ss << "TotalAllocatedPagesInBytes: " << totalAllocatedPagesInBytes << " : " << (double)totalAllocatedPagesInBytes * 100. / totalBlockSizeInBytes << "%" << std::endl;
			}
			else {
				ss << "TotalAllocatedInBytes: " << m_totalAllocatedSizeInBytes << std::endl;
			}

			return ss.str();
		}
#endif
	};

	using FixedPageAllocator = Allocator<Type::FixedPage>;
	using BuddyAllocator = Allocator<Type::Buddy>;

};
