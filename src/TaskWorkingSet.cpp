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
#include <TaskWorkingSet.h>
#include <PersistentWorkingSet.h>
#include <Log.h>

#include <string>

namespace KickstartRT_NativeLayer
{
	Status TaskWorkingSet::VolatileConstantBuffer::BeginMapping(GraphicsAPI::Device* dev)
	{
		m_currentOffsetInBytes = 0;

		if (m_cpuPtr != 0) {
			Log::Fatal(L"Failed to Begin Mapping since m_cpuPtr != 0");
			return Status::ERROR_INTERNAL;
		}

		m_cpuPtr = (intptr_t)m_cb.Map(dev, GraphicsAPI::Buffer::MapType::WriteDiscard, 0, 0, m_cb.m_sizeInBytes);
		if (m_cpuPtr == 0) {
			Log::Fatal(L"Failed to map volatile constant buffer, device removal state is suspected.");
			return Status::ERROR_INTERNAL;
		}

		return Status::OK;
	}

	void TaskWorkingSet::VolatileConstantBuffer::EndMapping(GraphicsAPI::Device* dev)
	{
		m_cb.Unmap(dev, 0, 0, m_cb.m_sizeInBytes);
		m_cpuPtr = 0;
	}

	Status TaskWorkingSet::VolatileConstantBuffer::Allocate(uint32_t allocationSizeInByte,	GraphicsAPI::ConstantBufferView *cbv, void** ptrForWrite)
	{
		if (m_cpuPtr == 0) {
			Log::Fatal(L"Failed to Allocate because the volatile constant buffer is unmapped.");
			return Status::ERROR_INTERNAL;
		}

		allocationSizeInByte = GraphicsAPI::Buffer::ConstantBufferPlacementAlignment(allocationSizeInByte);
		*ptrForWrite = nullptr;

		if (m_cb.m_sizeInBytes < m_currentOffsetInBytes + (uint64_t)allocationSizeInByte) {
			Log::Fatal(L"Failed to allocate volatile constant buffer. BufferSize:%d CurrentOffset:%d TriedToAllocate:%d", m_cb.m_sizeInBytes, m_currentOffsetInBytes, allocationSizeInByte);
			return Status::ERROR_INTERNAL;
		}
		if (!cbv->Init(&m_cb, (uint32_t)m_currentOffsetInBytes, allocationSizeInByte)) {
			Log::Fatal(L"Failed to init CBV");
			return Status::ERROR_INTERNAL;
		}
		*ptrForWrite = reinterpret_cast<void*>(m_cpuPtr + m_currentOffsetInBytes);
		m_currentOffsetInBytes += allocationSizeInByte;

		return Status::OK;
	}

	Status TaskWorkingSet::Init(const ExecuteContext_InitSettings * const settings)
	{
		// CBV/SRV/UAV descriptor heap
		{
			// VK needs a distinct desc heap budget.
			// SDK should able to do any render task with 2 samplers, 10 tex SRVs, 5 tex UAVs, 3 CBVs and 1 AS.
			constexpr uint32_t descHeapBudgetForARenderTask[7] = { 2, 10, 5, 0, 0, 3, 1 };
			constexpr uint32_t supportedRenderTaskNum = 20;

			// Buffer SRVs are used for adding geometry
			// Buffer UAVs are used for adding geometry and direct lighting cache array.
			using dh = GraphicsAPI::DescriptorHeap;
			dh::Desc	desc = {};
			uint32_t totalDescs = 0;
			desc.m_descCount[dh::value(dh::Type::Sampler)] = descHeapBudgetForARenderTask[0] * supportedRenderTaskNum;
			totalDescs += desc.m_descCount[dh::value(dh::Type::Sampler)];
			desc.m_descCount[dh::value(dh::Type::TextureSrv)] = descHeapBudgetForARenderTask[1] * supportedRenderTaskNum;
			totalDescs += desc.m_descCount[dh::value(dh::Type::TextureSrv)];
			desc.m_descCount[dh::value(dh::Type::TextureUav)] = descHeapBudgetForARenderTask[2] * supportedRenderTaskNum;
			totalDescs += desc.m_descCount[dh::value(dh::Type::TextureUav)];
			desc.m_descCount[dh::value(dh::Type::TypedBufferSrv)] = descHeapBudgetForARenderTask[3] * supportedRenderTaskNum + settings->descHeapSize / 4;
			totalDescs += desc.m_descCount[dh::value(dh::Type::TypedBufferSrv)];
			desc.m_descCount[dh::value(dh::Type::TypedBufferUav)] = descHeapBudgetForARenderTask[4] * supportedRenderTaskNum + settings->descHeapSize / 4 * 3;
			totalDescs += desc.m_descCount[dh::value(dh::Type::TypedBufferUav)];
			desc.m_descCount[dh::value(dh::Type::Cbv)] = descHeapBudgetForARenderTask[5] * supportedRenderTaskNum;
			totalDescs += desc.m_descCount[dh::value(dh::Type::Cbv)];
			desc.m_descCount[dh::value(dh::Type::AccelerationStructureSrv)] = descHeapBudgetForARenderTask[6] * supportedRenderTaskNum;
			totalDescs += desc.m_descCount[dh::value(dh::Type::AccelerationStructureSrv)];
			desc.m_totalDescCount = totalDescs;

#if defined(GRAPHICS_API_D3D12)
			if (m_persistentWorkingSet->m_descHeaps.size() == 0) {
				// Allocate a desc heap whic is going to be shared with all task working set.
				dh::Desc	descForAll = desc;
				for (auto&& ent : descForAll.m_descCount) {
					ent *= settings->supportedWorkingsets;
				}
				descForAll.m_totalDescCount *= settings->supportedWorkingsets;

				std::unique_ptr<GraphicsAPI::DescriptorHeap>	heap = std::make_unique<GraphicsAPI::DescriptorHeap>();
				if (!heap->Create(&m_persistentWorkingSet->m_device, descForAll, true)) {
					Log::Fatal(L"Failed to create descriptor heap");
					return Status::ERROR_FAILED_TO_INIT_TASK_WORKING_SET;
				}
				heap->SetName(DebugName(L"TaskWorkingSet"));

				m_persistentWorkingSet->m_descHeaps.push_back(std::move(heap));
			}
			{
				std::unique_ptr<GraphicsAPI::DescriptorSubHeap>	h = std::make_unique<GraphicsAPI::DescriptorSubHeap>();
				// Suballocate desc heap for each task working set.

				if (!h->Init(m_persistentWorkingSet->m_descHeaps[0].get(), desc)) {
					Log::Fatal(L"Failed to suballocate descriptor heap");
					return Status::ERROR_FAILED_TO_INIT_TASK_WORKING_SET;
				}

				m_CBVSRVUAVHeap = std::move(h);
			}
#elif  defined(GRAPHICS_API_VK)
			{
				std::unique_ptr<GraphicsAPI::DescriptorHeap>	h = std::make_unique<GraphicsAPI::DescriptorHeap>();
				if (! h->Create(&m_persistentWorkingSet->m_device, desc, true)) {
					Log::Fatal(L"Failed to create descriptor heap");
					return Status::ERROR_FAILED_TO_INIT_TASK_WORKING_SET;
				}
				h->SetName(DebugName(L"TaskWorkingSet"));

				m_CBVSRVUAVHeap = std::move(h);
			}
#endif
		}

		// volatile constant buffer
		{
			uint64_t volatileConstantBufferSizeInBytes = GraphicsAPI::Resource::DefaultResourcePlacementAlignment((uint64_t)settings->uploadHeapSizeForVolatileConstantBuffers);

			if (!m_volatileConstantBuffer.m_cb.Create(&m_persistentWorkingSet->m_device,
				volatileConstantBufferSizeInBytes, GraphicsAPI::Resource::Format::Unknown,
				GraphicsAPI::Resource::BindFlags::Constant, GraphicsAPI::Buffer::CpuAccess::Write)) {
				Log::Fatal(L"Failed to allocate volatile constant buffer");
				return (Status::ERROR_INTERNAL);
			}
			m_volatileConstantBuffer.m_cb.SetName(DebugName(L"TaskWorkingSet - VolatileConstantBuffer"));
		}

		m_TLASUploadBuffer = std::make_unique<GraphicsAPI::Buffer>();
#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
		m_directLightingCacheIndirectionTableUploadBuffer = std::make_unique<GraphicsAPI::Buffer>();
#endif

		return Status::OK;
	};

	Status TaskWorkingSet::Begin()
	{
		// reset desc heap allocation.
		m_CBVSRVUAVHeap->ResetAllocation();

		// reset upload heap for volatile constant buffer.
		m_volatileConstantBuffer.BeginMapping(&m_persistentWorkingSet->m_device);

		return Status::OK;
	}

	Status TaskWorkingSet::End()
	{
		// make sure it's unmapped.
		m_volatileConstantBuffer.EndMapping(&m_persistentWorkingSet->m_device);

		return Status::OK;
	};
};

