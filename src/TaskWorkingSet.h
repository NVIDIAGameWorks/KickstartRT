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
#include <Utils.h>
#include <PersistentWorkingSet.h>

#include <vector>
#include <list>
#include <memory>

namespace KickstartRT_NativeLayer
{
	class PersistentWorkingSet;

	class TaskWorkingSet
	{
	public:
		struct VolatileConstantBuffer
		{
			GraphicsAPI::Buffer					m_cb;
			uint64_t							m_currentOffsetInBytes;
			intptr_t							m_cpuPtr;

			VolatileConstantBuffer()
			{
				m_currentOffsetInBytes = 0;
				m_cpuPtr = 0;
			};
			Status BeginMapping(GraphicsAPI::Device* dev);
			void EndMapping(GraphicsAPI::Device* dev);

			Status Allocate(uint32_t allocationSizeInByte, GraphicsAPI::ConstantBufferView* cbv, void** ptrForWrite);
		};

		PersistentWorkingSet* const			m_persistentWorkingSet;

		GraphicsAPI::DescriptorHeap			m_CBVSRVUAVHeap;
		VolatileConstantBuffer				m_volatileConstantBuffer;

	public:
		std::unique_ptr<GraphicsAPI::Buffer>		m_TLASUploadBuffer;
#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
		std::unique_ptr<GraphicsAPI::Buffer>		m_directLightingCacheIndirectionTableUploadBuffer;
#endif

		TaskWorkingSet(PersistentWorkingSet* pws) :
			m_persistentWorkingSet(pws)
		{
		};

		Status Init(const KickstartRT_NativeLayer::ExecuteContext_InitSettings* const settings);
		Status Begin();
		Status End();
	};

	class TaskWorkingSetCommandList {
	public:
		Status						m_sts;
		TaskWorkingSet*				m_set;
		GraphicsAPI::CommandList*	m_commandList = nullptr;

		TaskWorkingSetCommandList(TaskWorkingSet* set, GraphicsAPI::CommandList *userPorvidedCmdList) :
			m_set(set)
		{
			m_sts = m_set->Begin();
			if (m_sts != Status::OK) {
				Log::Fatal(L"TaskWorkignSet::Begin() failed.");
				return;
			}
			m_commandList = userPorvidedCmdList;

			m_sts = m_set->m_persistentWorkingSet->InitWithCommandList(m_commandList);
			if (m_sts != Status::OK) {
				Log::Fatal(L"Failed to do init with command list.");
				return;
			}

			// Setting fws desc heap.
			m_commandList->SetDescriptorHeap(&m_set->m_CBVSRVUAVHeap);
		};

		~TaskWorkingSetCommandList()
		{
			if (m_set->End() != Status::OK) {
				Log::Fatal(L"TaskWorkignSet::End() failed.");
				return;
			}
			m_commandList = nullptr;
		}
	};

};
