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
#include <ShaderFactory.h>

#include <memory>
#include <deque>
#include <optional>

namespace KickstartRT_NativeLayer
{
	namespace BVHTask {
		struct Geometry;
	};
	class TaskWorkingSet;
	
	struct RenderPass_DirectLightingCacheAllocation
    {
        static constexpr uint32_t     m_threadDim_X = 96;

		enum class AllocationShaderPermutationBits : uint32_t {
			e_BUILD_OP				  = 0b0000'0011,
			e_USE_VERTEX_INDEX_INPUTS = 0b0000'0100,
			e_NumberOfPermutations    = 0b0000'1000,
		};

		enum DescTableLayout : uint32_t {
			e_CB_CBV = 0,
			e_VertexBuffer_SRV,
			e_IndexBuffer_SRV,
			e_Index_VertexBuffer_UAV,
			e_TileCounterBuffer_UAV,
			e_TileIndexBuffer_UAV,

			e_DescTableSize,
		};

		struct CB {
			uint32_t    m_vertexStride;
			uint32_t    m_nbVertices;
			uint32_t    m_nbIndices;
			uint32_t	m_dstVertexBufferOffsetIdx;

			uint32_t	m_indexRangeMin;
			uint32_t	m_indexRangeMax;
			uint32_t    m_tileResolutionLimit;
			float       m_tileUnitLength;

			uint32_t    m_enableTransformation;
			uint32_t    m_nbDispatchThreads;
			uint32_t	m_nbHashTableElemsNum;
			uint32_t	m_allocationOffset;

			uint32_t	m_vtxSRVOffsetElm;
			uint32_t	m_idxSRVOffsetElm;
			uint32_t	m_padd_0;
			uint32_t	m_padd_1;

			Math::Float_4x4   m_transformationMatrix;
		};

		GraphicsAPI::DescriptorTableLayout	m_descTableLayout;

		GraphicsAPI::RootSignature		m_rootSignature;
		std::array<ShaderFactory::ShaderDictEntry*, (size_t)AllocationShaderPermutationBits::e_NumberOfPermutations>	m_pso_allocate_itr;

    protected:
		// Must match .hlsl
		enum class BuildOp {
			TileCacheBuild,

			MeshColorBuild,
			MeshColorPostBuild,
			// 
			VertexUpdate,
		};
        Status BuildCommandList(BuildOp op, TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList, GraphicsAPI::ComputePipelineState **currentPSO, BVHTask::Geometry* gp);

    public:
        Status Init(GraphicsAPI::Device *dev, ShaderFactory::Factory *sf);
        Status BuildCommandListForAdd(TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList, std::deque<BVHTask::Geometry *>& addedGeometries);
        Status BuildCommandListForUpdate(TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList, std::deque<BVHTask::Geometry *>& updatedGeometries);

        static Status CheckInputs(const BVHTask::GeometryInput& input);
        static Status CheckUpdateInputs(const BVHTask::GeometryInput& oldInputs, const BVHTask::GeometryInput& input);
        static Status AllocateResourcesForGeometry(TaskWorkingSet* fws, std::deque<BVHTask::Geometry *>& addedGeometries);
    };
};
