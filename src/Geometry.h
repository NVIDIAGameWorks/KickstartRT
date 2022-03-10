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
#include <OS.h>
#include <Handle.h>

#include <SharedBuffer.h>

#include <assert.h>
#include <vector>
#include <list>
#include <array>
#include <memory>
#include <optional>

namespace KickstartRT_NativeLayer
{
	class PersistentWorkingSet;

	static constexpr uint32_t kInvalidNumTiles = 0xFFFF'FFFF;

	namespace BVHTask {
		enum class RegisterStatus {
			NotRegistered,
			Registering,
			Registered
		};

		struct Instance;

		struct Geometry {
			const uint64_t					m_id;
			BVHTask::GeometryInput			m_input = {};

			RegisterStatus											m_registerStatus = RegisterStatus::NotRegistered;

			std::unique_ptr<SharedBuffer::BufferEntry>				m_edgeTableBuffer;

			std::unique_ptr<SharedBuffer::BufferEntry>				m_index_vertexBuffer;

			size_t													m_nbVertexIndices = 0;
			size_t													m_nbVertices = 0;
			size_t													m_vertexBufferOffsetInBytes = (size_t)-1;

			std::unique_ptr<SharedBuffer::BufferEntry>				m_directLightingCacheCounter;
			std::unique_ptr<SharedBuffer::BufferEntry>				m_directLightingCacheCounter_Readback;

			uint32_t												m_numberOfTiles = kInvalidNumTiles;

			std::unique_ptr<SharedBuffer::BufferEntry>				m_directLightingCacheIndices;		// store tileOffset and nbTiles of U and V direction for each triangle primitive. No packed format for now.

			std::unique_ptr<SharedBuffer::BufferEntry>	m_BLASScratchBuffer;
			std::unique_ptr<SharedBuffer::BufferEntry>	m_BLASBuffer;

#if defined(GRAPHICS_API_D3D12)
			std::unique_ptr<SharedBuffer::BufferEntry>	m_BLASCompactionSizeBuffer;
			std::unique_ptr<SharedBuffer::BufferEntry>	m_BLASCompactionSizeBuffer_Readback;

#elif defined(GRAPHICS_API_VK)
			std::unique_ptr<GraphicsAPI::QueryPool_VK>	m_BLASCompactionSizeQueryPool;
#endif

			bool							m_directTileMapping = false;
			std::wstring					m_name;
			std::list<Instance*>		m_instances;	// weak reference of instances.

			static Geometry* ToPtr(GeometryHandle handle)
			{
				return ToPtr_s<Geometry, GeometryHandle>(handle);
			}

			GeometryHandle ToHandle()
			{
				return ToHandle_s<Geometry, GeometryHandle>(this);
			};

		public:
			Geometry(uint64_t id) : m_id(id) {};
			~Geometry();
			void DeferredRelease(PersistentWorkingSet* pws);

			// Returns the vertex count after transforming process in the SDK.
			inline uint32_t GetVertexCountAfterTransform()
			{
				if (m_input.indexRange.isEnabled)
					return m_input.indexRange.maxIndex - m_input.indexRange.minIndex + 1;

				return m_input.vertexBuffer.count;
			}
		};

		struct Instance {
			const uint64_t m_id;
			RegisterStatus							m_registerStatus = RegisterStatus::NotRegistered;

			Geometry* m_geometry = nullptr;
			BVHTask::InstanceInput	m_input = {};
			std::wstring							m_name;
			uint32_t								m_numberOfTiles = kInvalidNumTiles;

			// MeshColors: [vertex colors][edge/face colors]
			// TileCache:  tile samples per face.
			std::unique_ptr<SharedBuffer::BufferEntry>			m_dynamicTileBuffer;
			bool												m_tileIsCleared = false;

#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
#else
			std::unique_ptr<SharedCPUDescriptorHeap::SharedTableEntry>	m_cpuDescTableAllocation;
			bool														m_needToUpdateUAV = false;
#endif

			std::optional<std::list<InstanceHandle>::iterator>	m_TLASInstanceListItr;

			static Instance* ToPtr(InstanceHandle handle)
			{
				return ToPtr_s<Instance, InstanceHandle>(handle);
			}
			InstanceHandle ToHandle()
			{
				return ToHandle_s<Instance, InstanceHandle>(this);
			};

		public:
			Instance(uint64_t id) : m_id(id) {};
			~Instance();
			void DeferredRelease(PersistentWorkingSet* pws);
		};
	};
};

