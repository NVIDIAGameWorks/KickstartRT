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
#include <Geometry.h>
#include <DenoisingContext.h>

#include <memory>
#include <deque>
#include <unordered_map>
#include <list>
#include <mutex>

namespace KickstartRT_NativeLayer
{
	class SceneContainer {
		friend class Scene;

		// mutex for all operations.
		std::mutex		m_mutex;

		// all registered geometry, not removed yet, should be in this map.
		std::unordered_map<GeometryHandle, std::unique_ptr<BVHTask::Geometry>>		m_geometries;
		// Removed geometries that are hidden from external APIs, but still alive. Once it's m_refCount become '0'. Geometry is moved to ready to destruct.
		std::unordered_map<GeometryHandle, std::unique_ptr<BVHTask::Geometry>>		m_removedGeometries;
		// This list is cleared every frame after actual destruction process.
		std::list<std::unique_ptr<BVHTask::Geometry>>						m_readyToDestructGeometries;

		// This list acts as a task queue for building BVH. Mostry comming from added geometries.
		std::deque<GeometryHandle>							m_buildBVHQueue;

		// This lists geometries after transformations and before tile allocation due to readback latency.
		std::deque<std::pair<uint64_t, GeometryHandle>>		m_waitingForTileAllocationGeometries;

		// This lists geometries after building BVH and before compaction due to readback latency.
		std::deque<std::pair<uint64_t, GeometryHandle>>		m_waitingForBVHCompactionGeometries;

		// all registered instances, not removed yet, should be in this map.
		std::unordered_map<InstanceHandle, std::unique_ptr<BVHTask::Instance>>		m_instances;

		// Valid instance list for TLAS and desctable. Updated during TLAS build process, and referred during desc table update.
		std::list<InstanceHandle>								m_TLASInstanceList;

		// This list is cleared every frame after actual destruction process.
		std::list<std::unique_ptr<BVHTask::Instance>>			m_readyToDestructInstances;

		// need to update direct lighting cache, requested from referencing geometry. This list is created/deleted every frame during BVH build process.
		std::deque<InstanceHandle>						m_needToUpdateDirectLightingCache;

		// All the currently alive denoising contexts.
		std::deque<std::unique_ptr<DenoisingContext> > m_denoisingContexts;

		using GeomMapIterator = typename std::unordered_map<GeometryHandle, std::unique_ptr<BVHTask::Geometry>>::iterator;
		using InsMapIterator = typename std::unordered_map<InstanceHandle, std::unique_ptr<BVHTask::Instance>>::iterator;

		~SceneContainer();
	};
};
