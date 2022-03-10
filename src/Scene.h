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
#include <Log.h>
#include <ExecuteContext.h>
#include <PersistentWorkingSet.h>
#include <TaskWorkingSet.h>
#include <TaskTracker.h>
#include <TaskContainer.h>
#include <SceneContainer.h>

#include <assert.h>
#include <memory>
#include <vector>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <optional>

namespace KickstartRT_NativeLayer
{
	class Scene {
	public:
	protected:
		const bool	m_enableInfoLog = false;

		SceneContainer	m_container;

		bool														m_TLASisDrity = false;
		std::unique_ptr<GraphicsAPI::Buffer>						m_TLASScratchBuffer;
		std::unique_ptr<GraphicsAPI::Buffer>						m_TLASBuffer;
		std::unique_ptr<GraphicsAPI::ShaderResourceView>			m_TLASBufferSrv;

#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
		std::unique_ptr<GraphicsAPI::Buffer>						m_directLightingCacheIndirectionTableBuffer;
		std::unique_ptr<GraphicsAPI::UnorderedAccessView>			m_directLightingCacheIndirectionTableBufferUAV;
		std::deque<SharedBuffer::BufferBlock*>						m_directLightingCacheIndirectionTableSharedBlockEntries;
#else
		struct CPULightCacheDescs {
			size_t													m_allocatedDescTableSize = 0;
			std::unique_ptr<GraphicsAPI::DescriptorTableLayout>		m_descLayout;
			std::unique_ptr<GraphicsAPI::DescriptorHeap>			m_descHeap;
			std::unique_ptr<GraphicsAPI::DescriptorTable>			m_descTable;
			std::vector<InstanceHandle>				m_instanceList;
		};
		CPULightCacheDescs m_cpuLightCacheDescs;
#endif

		Status UpdateScenegraphFromBVHTask(PersistentWorkingSet* pws, BVHTasks *bvhTasks,
			std::deque<BVHTask::Geometry*>& addedGeometryPtrs,
			std::deque<BVHTask::Geometry*>& updatedGeometryPtrs,
			std::deque<BVHTask::Instance*>& addedInstancePtrs,
			std::deque<BVHTask::Instance*>& updatedInstancePtrs,
			bool& isSceneChanged);

		Status DoReadbackAndTileAllocation(PersistentWorkingSet* pws, bool& AllocationHappened);
		Status DoAllocationForAddedInstances(PersistentWorkingSet* pws, std::deque<BVHTask::Instance*>& addedInstancePtrs, bool& AllocationHappened);

		Status DoReadbackAndCompactBLASBuffers(PersistentWorkingSet* pws, GraphicsAPI::CommandList* cmdList, bool& BLASChanged);

		Status BuildTransformAndTileAllocationCommands(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList,
			std::deque<BVHTask::Geometry*>& addedGeometries,
			std::deque<BVHTask::Geometry*>& updatedGeometries);

		Status BuildBLASCommands(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList,
			std::deque<BVHTask::Geometry*>& updatedGeometryPtrs,
			uint32_t maxBlasBuildTasks, bool& BLASChanged);

		Status BuildTLASCommands(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList);

		Status BuildDirectLightingCacheDescriptorTable(TaskWorkingSet* tws, GraphicsAPI::DescriptorTableLayout* srcLayout, GraphicsAPI::DescriptorTable* destDescTable, std::deque<BVHTask::Instance*>& retInstances);

		Status UpdateDenoisingContext(PersistentWorkingSet* pws, UpdateFromExecuteContext* updateFromExc);
		Status UpdateScenegraphFromExecuteContext(PersistentWorkingSet* pws, UpdateFromExecuteContext* updateFromExc, bool isSceneChanged);

	public:
		Status BuildTask(GPUTaskHandle *retHandle, TaskTracker *taskTracker, PersistentWorkingSet* pws, TaskContainer_impl* arg_taskContainer, UpdateFromExecuteContext *updateFromExc, const BuildGPUTaskInput *input);
		Status ReleaseDeviceResourcesImmediately(TaskTracker* taskTracker, PersistentWorkingSet* pws, UpdateFromExecuteContext* updateFromExc);
	};
};

