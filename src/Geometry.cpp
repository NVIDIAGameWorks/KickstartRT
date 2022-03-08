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
#include <Platform.h>
#include <Utils.h>
#include <Log.h>
#include <PersistentWorkingSet.h>
#include <Geometry.h>

namespace KickstartRT_NativeLayer
{
	namespace BVHTask {
		Geometry::~Geometry()
		{
			if (m_instances.size() > 0) {
				Log::Warning(L"~GeometryHandle called but it was referenced from (%d) instances.", m_instances.size());
				//assert(false);
			}
		};

		void Geometry::DeferredRelease(PersistentWorkingSet* pws)
		{
			pws->DeferredRelease(std::move(m_index_vertexBuffer));

			pws->DeferredRelease(std::move(m_directLightingCacheCounter));
			pws->DeferredRelease(std::move(m_directLightingCacheCounter_Readback));
			pws->DeferredRelease(std::move(m_directLightingCacheIndices));

			pws->DeferredRelease(std::move(m_BLASScratchBuffer));
			pws->DeferredRelease(std::move(m_BLASBuffer));

#if defined(GRAPHICS_API_D3D12)
			pws->DeferredRelease(std::move(m_BLASCompactionSizeBuffer));
			pws->DeferredRelease(std::move(m_BLASCompactionSizeBuffer_Readback));
#elif defined(GRAPHICS_API_VK)
			pws->DeferredRelease(std::move(m_BLASCompactionSizeQueryPool));
#endif

			pws->DeferredRelease(std::move(m_edgeTableBuffer));
		}

		// calling dtor with valid geometry handle is prohibited.
		Instance::~Instance()
		{
			if (m_geometry != nullptr) {
				Log::Fatal(L"Geometry handle was not null when destructing an InstanceHandle.");
				assert(false);
			}
		};

		void Instance::DeferredRelease(PersistentWorkingSet* pws)
		{
			// remove reference from GeometryHandle here.
			if (m_geometry != nullptr) {
				Log::Fatal(L"Relation between instance and geometry shoudl have been removed before deferred release.");
				assert(false);
			}

#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
#else
			// allocated CPU desc heap is released here immediately.
			m_cpuDescTableAllocation.reset();
#endif

			pws->DeferredRelease(std::move(m_dynamicTileBuffer));
		}
	};
};

