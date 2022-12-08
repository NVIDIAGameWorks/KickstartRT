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
#include <RenderPass_DirectLightingCacheAllocation.h>
#include <Utils.h>
#include <Log.h>
#include <Platform.h>
#include <ShaderFactory.h>
#include <PersistentWorkingSet.h>
#include <Geometry.h>
#include <TaskWorkingSet.h>
#include <Scene.h>
#include <WinResFS.h>

#include <inttypes.h>
#include <cstring>

namespace KickstartRT_NativeLayer
{
	Status RenderPass_DirectLightingCacheAllocation::Init(GraphicsAPI::Device* dev, ShaderFactory::Factory *sf)
	{
		auto RegisterShader = [&sf](
			const std::wstring& fileName, const std::wstring& entryName, const std::wstring& shaderName,
			ShaderFactory::ShaderType::Enum type, const std::vector<ShaderFactory::ShaderMacro>& shaderMacro, GraphicsAPI::RootSignature& rootSig)
			-> std::pair<Status, ShaderFactory::ShaderDictEntry *>
		{
			std::unique_ptr<ShaderFactory::ShaderDictEntry> dictEnt = std::make_unique< ShaderFactory::ShaderDictEntry>();
			dictEnt->m_fileName = fileName;
			dictEnt->m_entryName = entryName;
			dictEnt->m_shaderName = shaderName;
			dictEnt->m_type = type;
			dictEnt->m_shaderMacro_CRC = sf->GetShaderMacroCRC(&shaderMacro);
			dictEnt->m_rootSig = &rootSig;

			auto ofsSize = sf->FindShaderOffset(dictEnt->m_fileName.c_str(), dictEnt->m_entryName.c_str(), dictEnt->m_shaderMacro_CRC, dictEnt->m_type);
			if (!ofsSize.has_value()) {
				Log::Fatal(L"Failed to find a binary entry for shader:%s", dictEnt->m_fileName.c_str());
				return { Status::ERROR_FAILED_TO_INIT_RENDER_PASS, nullptr};
			}
			dictEnt->m_offset = ofsSize.value().first;
			dictEnt->m_size = ofsSize.value().second;

			dictEnt->CalcCRC();

			return sf->RegisterShader(std::move(dictEnt));
		};

		// CBV/SRV/UAV descriptor table
		{
			// set [CB, SRV, SRV, UAV, UAV, UAV, UAV]
			m_descTableLayout.AddRange(GraphicsAPI::DescriptorHeap::Type::Cbv, 0, 1, 0); // b0, CB, baseRegIdx, cnt, regSpace
			m_descTableLayout.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferSrv, 0, 1, 0); // t0, vertexBuf
			m_descTableLayout.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferSrv, 1, 1, 0); // t1, indexBuf
			m_descTableLayout.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 0, 1, 0); // u0 meshColorHashTable
			m_descTableLayout.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 1, 1, 0); // u1 meshColorHeader
			m_descTableLayout.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 2, 1, 0); // u2 sorted index and transformed vertex buffer
			m_descTableLayout.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 3, 1, 0); // u3 tileCounter

			if (!m_descTableLayout.SetAPIData(dev)) {
				Log::Fatal(L"Failed to set apiData for descriptor table layout.");
				return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
			}

			std::vector<GraphicsAPI::DescriptorTableLayout*> tableLayouts = { &m_descTableLayout };
			if (!m_rootSignature.Init(dev, tableLayouts)) {
				Log::Fatal(L"Failed to create rootSignature");
				return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
			}
			m_rootSignature.SetName(DebugName(L"RP_DirectLightingCacheAllocation"));
		}

		{
			std::filesystem::path csPath(L"DirectLightingCache/Allocation_TrianglesIndexed_cs.hlsl");
			std::array<std::shared_ptr<ShaderBlob::Blob::IBlob>, (size_t)AllocationShaderPermutationBits::e_NumberOfPermutations> cs_blob_allocation;
			std::shared_ptr<ShaderBlob::Blob::IBlob> cs_blob_update;

			std::vector<ShaderFactory::ShaderMacro> defines;
			defines.push_back({ "BUILD_OP", "" });
			defines.push_back({ "USE_VERTEX_INDEX_INPUTS", "" });

			constexpr const char* defArr[4] = { "0", "1" , "2" , "3" };
			for (uint32_t i = 0; i < (uint32_t)AllocationShaderPermutationBits::e_NumberOfPermutations; ++i) {
				defines[0].definition = defArr[i & (uint32_t)AllocationShaderPermutationBits::e_BUILD_OP];
				defines[1].definition = defArr[i & (uint32_t)AllocationShaderPermutationBits::e_USE_VERTEX_INDEX_INPUTS ? 1 : 0];

				auto [sts, ptr] = RegisterShader(csPath.wstring(), L"main", DebugName(L"RP_DirectLightingCacheAllocation[%d] - Allocate", i),
					ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, m_rootSignature);
				if (sts != Status::OK)
					return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

				m_pso_allocate_itr[i] = ptr;
			}

			defines.clear();
		}

		return Status::OK;
	};

	Status RenderPass_DirectLightingCacheAllocation::CheckInputs(const BVHTask::GeometryInput& input)
	{
		Status sts = Status::OK;

		for (;;) {
			if (input.components.size() == 0) {
				Log::Error(L"There is no geometry compont.");
				sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
				break;
			}
		
			if (input.forceDirectTileMapping && input.surfelType == BVHTask::GeometryInput::SurfelType::MeshColors) {
				Log::Error(L"forceDirectTileMapping is not compatible with MeshColors.");
				sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
				break;
			}

			for (size_t i = 0; i < input.components.size(); ++i) {
				const auto& cmp(input.components[i]);
				const auto& vb(cmp.vertexBuffer);

#if defined(GRAPHICS_API_D3D12)
				if (vb.format != DXGI_FORMAT_R32G32B32_FLOAT) {
					Log::Error(L"Unsupported vertex buffer format detected.");
					sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
					break;
				}
#elif defined(GRAPHICS_API_VK)
				if (vb.format != VK_FORMAT_R32G32B32_SFLOAT) {
					Log::Error(L"Unsupported vertex buffer format detected.");
					sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
					break;
				}
#endif

				if (vb.offsetInBytes % sizeof(float) != 0 ||
					vb.strideInBytes % sizeof(float) != 0) {
					Log::Error(L"Vertex offset and strides didn't meet the alignment requirement.");
					sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
					break;
				}

				if (input.type == BVHTask::GeometryInput::Type::Triangles) {
					if (vb.count % 3 != 0) {
						Log::Error(L"Number of vertices must be multiple of 3 since it's a triangles %d", vb.count);
						sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
						break;
					}
				}
				else if (input.type == BVHTask::GeometryInput::Type::TrianglesIndexed) {
					const auto& ib(cmp.indexBuffer);

					if (ib.count % 3 != 0) {
						Log::Error(L"Number of indices must be multiple of 3 since it's a triangle list %d", ib.count);
						sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
						break;
					}

#if defined(GRAPHICS_API_D3D12)
					if (ib.format != DXGI_FORMAT_R32_UINT &&
						ib.format != DXGI_FORMAT_R16_UINT) {
						Log::Error(L"Unsupported index buffer format detected.");
						sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
						break;
					}
					if ((ib.format == DXGI_FORMAT_R32_UINT && ib.offsetInBytes % sizeof(uint32_t) != 0) ||
						(ib.format == DXGI_FORMAT_R16_UINT && ib.offsetInBytes % sizeof(uint16_t) != 0)) {
						Log::Error(L"Index offset didn't meet the alignment requirement.");
						sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
						break;
					}
#elif defined(GRAPHICS_API_VK)
					if (ib.format != VK_FORMAT_R32_UINT &&
						ib.format != VK_FORMAT_R16_UINT) {
						Log::Error(L"Unsupported index buffer format detected.");
						sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
						break;
					}
					if ((ib.format == VK_FORMAT_R32_UINT && ib.offsetInBytes % sizeof(uint32_t) != 0) ||
						(ib.format == VK_FORMAT_R16_UINT && ib.offsetInBytes % sizeof(uint16_t) != 0)) {
						Log::Error(L"Index offset didn't meet the alignment requirement.");
						sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
						break;
					}
#endif

					if (cmp.indexRange.isEnabled) {
						if (cmp.indexRange.maxIndex < cmp.indexRange.minIndex) {
							Log::Error(L"Invalid index range detected.");
							sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
							break;
						}
						if (cmp.indexRange.maxIndex >= vb.count) {
							Log::Error(L"Index range exceeded vertex buffer size.");
							sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
							break;
						}
					}
				}
				else {
					Log::Error(L"Unsupported input geometry type detected.");
					sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
					break;
				}
			}

			break;
		}

		if (sts != Status::OK) {
			// Output some more info for invalid geometry inputs.
			Log::Info(L"---- Invalid inputs for RegisterGeometry ----");
			Utils::LogGeometryInput(&input);
		}

		return sts;
	};

	Status RenderPass_DirectLightingCacheAllocation::CheckUpdateInputs(const BVHTask::GeometryInput& oldInput, const BVHTask::GeometryInput& input)
	{
		Status sts = Status::OK;

		for (;;) {
			if (!oldInput.allowUpdate) {
				Log::Error(L"Geometry handle was not created with allow update flag.");
				sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
				break;
			}

			if (oldInput.type != input.type) {
				Log::Error(L"Different geometry input type was set for update.");
				sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
				break;
			}

			if (oldInput.components.size() != input.components.size()) {
				Log::Error(L"Different number of geometry component was set for update.");
				sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
				break;
			}

			for (size_t i= 0; i < oldInput.components.size(); ++i) {
				const auto& oldCmp(oldInput.components[i]);
				const auto& oldVb(oldCmp.vertexBuffer);
				const auto& cmp(input.components[i]);
				const auto& vb(cmp.vertexBuffer);

				if (oldVb.count != vb.count) {
					Log::Error(L"Vertex count didn't match when updating a geometry.");
					sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
					break;
				}
				if (oldCmp.indexRange.isEnabled != cmp.indexRange.isEnabled ||
					oldCmp.indexRange.minIndex != cmp.indexRange.minIndex ||
					oldCmp.indexRange.maxIndex != cmp.indexRange.maxIndex) {
					Log::Error(L"IndexRange didn't match when updating a geometry.");
					sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
					break;
				}
			}

			break;
		}

		if (sts != Status::OK) {
			// Output some more info for invalid geometry inputs.
			Log::Info(L"---- Invalid inputs for updating a geometry (original inputs) ----");
			Utils::LogGeometryInput(&oldInput);
			Log::Info(L"---- Invalid inputs for updating a geometry (inputs for updating) ----");
			Utils::LogGeometryInput(&input);
		}

		return sts;
	}

	Status RenderPass_DirectLightingCacheAllocation::AllocateResourcesForGeometry(TaskWorkingSet* fws, std::deque<BVHTask::Geometry *>& addedGeometries)
	{
		PersistentWorkingSet* pws(fws->m_persistentWorkingSet);
		auto& dev(pws->m_device);
		(void)dev;

		// allocate vertexbuffer/indexbuffer/directLightingCacheIndex/directLightingCacheCounter
		for (auto gp : addedGeometries) {
			auto& input(gp->m_input);

			gp->m_totalNbIndices = 0;
			gp->m_totalNbVertices = 0;
			gp->m_vertexOffsets.clear();
			gp->m_indexOffsets.clear();
			for (auto&& cmp : input.components) {
				auto& vIn(cmp.vertexBuffer);
				auto& iIn(cmp.indexBuffer);

				gp->m_indexOffsets.push_back(gp->m_totalNbIndices);
				gp->m_vertexOffsets.push_back(gp->m_totalNbVertices);

				if (input.type == BVHTask::GeometryInput::Type::Triangles) {
					// Triangles will have a flatten index buffer for supporting update geometry with reordering edges by those lengths.
					gp->m_totalNbIndices += vIn.count;
					iIn.offsetInBytes = 0;
#if defined(GRAPHICS_API_D3D12)
					iIn.format = DXGI_FORMAT_R32_UINT;
					iIn.resource = nullptr;
#elif defined(GRAPHICS_API_VK)
					iIn.format = VK_FORMAT_R32_UINT;
					iIn.typedBuffer = nullptr;
#endif
					gp->m_totalNbVertices += vIn.count;
				}
				else {
					gp->m_totalNbIndices += iIn.count;

					if (cmp.indexRange.isEnabled)
						gp->m_totalNbVertices += cmp.indexRange.maxIndex - cmp.indexRange.minIndex + 1;
					else
						gp->m_totalNbVertices += vIn.count;
				}
			}

			// Create an unified buffer for index and vertx arrays.
			{
				size_t idxSizeInBytes = GraphicsAPI::ALIGN((size_t)16, (size_t)gp->m_totalNbIndices * sizeof(uint32_t));
				size_t vtxSizeInBytes = GraphicsAPI::ALIGN((size_t)16, (size_t)gp->m_totalNbVertices * 3 * sizeof(float));

				const uint32_t faceCount = gp->m_totalNbIndices / 3;
				const uint32_t maxEdgeCount = gp->m_totalNbIndices;

				// use persistent allocator if allow update is enabled.
				decltype(pws->m_sharedBufferForVertexTemporal)::element_type* allocator = nullptr;
				if (gp->m_input.allowUpdate) {
					allocator = pws->m_sharedBufferForVertexPersistent.get();
				}
				else {
					allocator = pws->m_sharedBufferForVertexTemporal.get();
				}
				gp->m_index_vertexBuffer = allocator->Allocate(pws, idxSizeInBytes+ vtxSizeInBytes, true);
				if (!gp->m_index_vertexBuffer) {
					Log::Fatal(L"Failed to allocate a index_vertex buffer NvIdcs:%d, NbVerts:%d", gp->m_totalNbIndices, gp->m_totalNbVertices);
					Log::Info(L"---- Inputs for the geometry ----");
					Utils::LogGeometryInput(&input);
					return (Status::ERROR_INTERNAL);
				}
			
				gp->m_vertexBufferOffsetInBytes = idxSizeInBytes;

				// Allocate edge table buffer and DLC cahce indices here for MeshColors.
				if (gp->m_input.surfelType == BVHTask::GeometryInput::SurfelType::MeshColors) {
					// 
					// Increasing to reduce hash-conflicts but increase memory footprint.
					const float hashMapLoadFactor = 0.75f;
					// edgeCount is (exactly) the expected number of keys.*/;
					const uint32_t hashTableMaxKeyCount = maxEdgeCount;
					const uint32_t hashTableBucketSize = sizeof(uint32_t) * 4; // key + value + log2 + allocOffset
					const uint32_t hashTableAllocationSize = uint32_t((hashTableMaxKeyCount * hashTableBucketSize) / hashMapLoadFactor);

					const uint32_t meshColorHeaderSize = sizeof(uint32_t) * 8 + sizeof(uint32_t) * 8 * faceCount;

					// Use temporal buffer for hash table.
					gp->m_edgeTableBuffer = pws->m_sharedBufferForDirectLightingCacheTemp->Allocate(pws, hashTableAllocationSize, true);

					// Use persistent buffer for DLC indices.
					gp->m_directLightingCacheIndices = pws->m_sharedBufferForDirectLightingCache->Allocate(pws, meshColorHeaderSize, true);
				}
			}

			// Don't calculate number of tiles in force direct tile mapping mode.
			if (! gp->m_input.forceDirectTileMapping) {
				uint32_t nbPrims = gp->m_totalNbIndices / 3;

				// allocate tile buffer index, offset
				// nbPrim * 64bit
				if (gp->m_input.surfelType == BVHTask::GeometryInput::SurfelType::WarpedBarycentricStorage) {
					gp->m_directLightingCacheIndices = pws->m_sharedBufferForDirectLightingCache->Allocate(
						pws, sizeof(uint32_t) * 8 + sizeof(uint32_t) * 2 * nbPrims, true);
					if (!gp->m_directLightingCacheIndices) {
						Log::Fatal(L"Failed to allocate a tiled lighting cache indices buffer NbPrims:%d", nbPrims);
						return (Status::ERROR_INTERNAL);
					}
				}

				gp->m_directLightingCacheCounter = pws->m_sharedBufferForCounter->Allocate(
					pws, sizeof(uint32_t) * 4, true);
				if (!gp->m_directLightingCacheCounter) {
					Log::Fatal(L"Failed to allocate a direct lighting cache counter buffer");
					return (Status::ERROR_INTERNAL);
				}

				gp->m_directLightingCacheCounter_Readback = pws->m_sharedBufferForReadback->Allocate(
					pws, sizeof(uint32_t) * 4, false);
				if (!gp->m_directLightingCacheCounter_Readback) {
					Log::Fatal(L"Failed to allocate a direct lighting cache counter (readback) buffer");
					return (Status::ERROR_INTERNAL);
				}
			}
		}

		return Status::OK;
	}

	Status RenderPass_DirectLightingCacheAllocation::BuildCommandListForAdd(TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList, std::deque<BVHTask::Geometry *>& addedGeometries)
	{
		PersistentWorkingSet* pws(fws->m_persistentWorkingSet);
		auto& dev(pws->m_device);

		GraphicsAPI::Utils::ScopedEventObject sce(cmdList, { 0, 128, 0 }, DebugName("Add Geometry"));

#if defined(GRAPHICS_API_D3D12)
		// Check input resource states with debug command list.
		if (cmdList->HasDebugCommandList() && addedGeometries.size() > 0) {
			std::vector<ID3D12Resource*>		resArr;
			std::vector<D3D12_RESOURCE_STATES>	stateArr;
			resArr.reserve(addedGeometries.size() * 4);
			stateArr.reserve(addedGeometries.size() * 4);
			constexpr D3D12_RESOURCE_STATES assertedState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			for (auto&& gp : addedGeometries) {

				for (auto&& cmp : gp->m_input.components) {
					if (gp->m_input.type == BVHTask::GeometryInput::Type::TrianglesIndexed) {
						resArr.push_back(cmp.indexBuffer.resource);
						stateArr.push_back(assertedState);
					}
					resArr.push_back(cmp.vertexBuffer.resource);
					stateArr.push_back(assertedState);
				}
			}
			if (!cmdList->AssertResourceStates(resArr.data(), resArr.size(), stateArr.data())) {
				Log::Fatal(L"Invalid resource state deteted while registering geometries. Expected state is: %d", assertedState);
				return (Status::ERROR_INTERNAL);
			}
		}
#endif

		// clear counter buffer with zero.
		for (auto&& gp : addedGeometries) {
			if (gp->m_directLightingCacheCounter) {
				// force direct mapping doesn't allocate counter buffer.
				gp->m_directLightingCacheCounter->RegisterClear();
			}
			if (gp->m_edgeTableBuffer) {
				gp->m_edgeTableBuffer->RegisterClear();
			}
		}
		if (pws->m_sharedBufferForCounter->DoClear(&dev, cmdList, fws->m_CBVSRVUAVHeap.get()) != Status::OK) {
			Log::Fatal(L"Failed to clear shared counter buffer.");
			return (Status::ERROR_INTERNAL);
		}
		if (pws->m_sharedBufferForDirectLightingCacheTemp->DoClear(&dev, cmdList, fws->m_CBVSRVUAVHeap.get()) != Status::OK) {
			Log::Fatal(L"Failed to clear shared mesh color buffer.");
			return (Status::ERROR_INTERNAL);
		}

		cmdList->SetComputeRootSignature(&m_rootSignature);

		// Build verex_index buffer.
		// Build tile cahce indices or edgeTable and caclucate the size of DLC buffer.
		{
			for (auto gp : addedGeometries) {
				if (gp->m_edgeTableBuffer) gp->m_edgeTableBuffer->RegisterBarrier();
				if (gp->m_directLightingCacheIndices) gp->m_directLightingCacheIndices->RegisterBarrier();
			}

			if (pws->m_sharedBufferForDirectLightingCache->UAVBarrier(cmdList) != Status::OK) {
				Log::Fatal(L"Failed to clear shared mesh color buffer.");
				return (Status::ERROR_INTERNAL);
			}

			if (pws->m_sharedBufferForDirectLightingCacheTemp->UAVBarrier(cmdList) != Status::OK) {
				Log::Fatal(L"Failed to clear shared mesh color buffer.");
				return (Status::ERROR_INTERNAL);
			}

			GraphicsAPI::ComputePipelineState* currentPSO = nullptr;
			for (auto gp : addedGeometries) {
				BuildOp op = gp->m_input.surfelType == BVHTask::GeometryInput::SurfelType::MeshColors ? BuildOp::MeshColorBuild : BuildOp::TileCacheBuild;
				RETURN_IF_STATUS_FAILED(BuildCommandList(op, fws, cmdList, &currentPSO, gp));
			}

			// SurfelType::MeshColors needs 2nd pass to build DLC.
			bool meshColorFound = false;
			for (auto&& gp : addedGeometries) {
				if (gp->m_input.surfelType == BVHTask::GeometryInput::SurfelType::MeshColors) {
					gp->m_edgeTableBuffer->RegisterBarrier();
					gp->m_directLightingCacheIndices->RegisterBarrier();
					meshColorFound = true;
				}
			}
			if (meshColorFound) {
				pws->m_sharedBufferForDirectLightingCache->UAVBarrier(cmdList);
				pws->m_sharedBufferForDirectLightingCacheTemp->UAVBarrier(cmdList);

				for (auto gp : addedGeometries) {
					if (gp->m_input.surfelType == BVHTask::GeometryInput::SurfelType::MeshColors) {
						RETURN_IF_STATUS_FAILED(BuildCommandList(BuildOp::MeshColorPostBuild, fws, cmdList, &currentPSO, gp));
					}
				}
			}
		}

		// Set resource barrier
		{
			for (auto&& gp : addedGeometries) {
				gp->m_index_vertexBuffer->RegisterBarrier();

				if (gp->m_edgeTableBuffer) {
					gp->m_edgeTableBuffer->RegisterBarrier();
				}
				if (gp->m_directLightingCacheIndices) {
					gp->m_directLightingCacheIndices->RegisterBarrier();
				}
			}

			// These resources will be read only after the dispatch above,
			// Resouce state need to be SR before building BLAS in D3D12, otherwise you'll hit an error.
			pws->m_sharedBufferForVertexTemporal->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::ShaderResource);
			pws->m_sharedBufferForVertexPersistent->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::ShaderResource);

			// UAV barrier for DLCs
			pws->m_sharedBufferForDirectLightingCache->UAVBarrier(cmdList);
			pws->m_sharedBufferForDirectLightingCacheTemp->UAVBarrier(cmdList);
		}

		// set resource barrier for counter buffers.
		for (auto&& gp : addedGeometries) {
			if (gp->m_directLightingCacheCounter) {
				gp->m_directLightingCacheCounter->RegisterBarrier();
			}
		}
		pws->m_sharedBufferForCounter->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::CopySource);

		// copy tile counter to readback
		for (auto&& gp : addedGeometries) {
			if (gp->m_directLightingCacheCounter) {
				auto dst = gp->m_directLightingCacheCounter_Readback.get();
				auto src = gp->m_directLightingCacheCounter.get();
				if (dst == nullptr || src == nullptr) {
					Log::Fatal(L"Failed to set a copy command for readback.");
					return (Status::ERROR_INTERNAL);
				}
				cmdList->CopyBufferRegion(
					dst->m_block->m_buffer.get(), dst->m_offset,
					src->m_block->m_buffer.get(), src->m_offset,
					sizeof(uint32_t) * 4);
			}
		}

		for (auto&& gp : addedGeometries) {
			if (gp->m_directLightingCacheCounter) {
				gp->m_directLightingCacheCounter->RegisterBarrier();
			}
			if (gp->m_directLightingCacheCounter_Readback) {
				gp->m_directLightingCacheCounter_Readback->RegisterBarrier();
			}
		}
		pws->m_sharedBufferForCounter->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::UnorderedAccess);

		// CopyDest -> CopyDest.
		// This is hacky but D3D12 doesn't need any barrier for host read. Nothing will happen in D3D12.
		// VK need some pipeline barrier to read data from host side after copy OP.
		pws->m_sharedBufferForReadback->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::CopyDest);

		return Status::OK;
	}

	Status RenderPass_DirectLightingCacheAllocation::BuildCommandListForUpdate(TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList, std::deque<BVHTask::Geometry*>& updatedGeometries)
	{
		GraphicsAPI::Utils::ScopedEventObject sce(cmdList, { 0, 128, 0 }, DebugName("Update Geometry"));
		PersistentWorkingSet* pws(fws->m_persistentWorkingSet);

#if defined(GRAPHICS_API_D3D12)
		// Check input resource states with debug command list.
		if (cmdList->HasDebugCommandList() && updatedGeometries.size() > 0) {
			std::vector<ID3D12Resource*>		resArr;
			std::vector<D3D12_RESOURCE_STATES>	stateArr;
			resArr.reserve(updatedGeometries.size() * 2);
			stateArr.reserve(updatedGeometries.size() * 2);
			constexpr D3D12_RESOURCE_STATES assertedState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			for (auto&& gp : updatedGeometries) {
				for (auto&& cmp : gp->m_input.components) {
					resArr.push_back(cmp.vertexBuffer.resource);
					stateArr.push_back(assertedState);
				}
			}
			if (!cmdList->AssertResourceStates(resArr.data(), resArr.size(), stateArr.data())) {
				Log::Fatal(L"Invalid resource state deteted while registering geometries. Expected state is: %d", assertedState);
				return (Status::ERROR_INTERNAL);
			}

		}
#endif

		for (auto&& gp : updatedGeometries) {
			gp->m_index_vertexBuffer->RegisterBarrier();
		}
		pws->m_sharedBufferForVertexTemporal->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::UnorderedAccess);
		pws->m_sharedBufferForVertexPersistent->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::UnorderedAccess);

		cmdList->SetComputeRootSignature(&m_rootSignature);

		{
			GraphicsAPI::ComputePipelineState* currentPSO = nullptr;
			for (auto&& gp : updatedGeometries) {
				RETURN_IF_STATUS_FAILED(BuildCommandList(BuildOp::VertexUpdate, fws, cmdList, &currentPSO, gp)); // Update
			}
		}

		// these resources will be read only after the dispatch above,
		// resouce state need to be SR before building BLAS.
		for (auto&& gp : updatedGeometries) {
			gp->m_index_vertexBuffer->RegisterBarrier();
		}
		pws->m_sharedBufferForVertexTemporal->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::ShaderResource);
		pws->m_sharedBufferForVertexPersistent->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::ShaderResource);

		return Status::OK;
	}

	Status RenderPass_DirectLightingCacheAllocation::BuildCommandList(BuildOp op, TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList, GraphicsAPI::ComputePipelineState **currentPSO, BVHTask::Geometry* gp)
	{
		PersistentWorkingSet* pws(fws->m_persistentWorkingSet);
		auto& dev(pws->m_device);

		for (size_t cmpIdx = 0; cmpIdx < gp->m_input.components.size(); ++cmpIdx) {
			const auto& cmp(gp->m_input.components[cmpIdx]);

			GraphicsAPI::DescriptorTable	descTable;
			if (!descTable.Allocate(fws->m_CBVSRVUAVHeap.get(), &m_descTableLayout)) {
				Log::Fatal(L"Faild to allocate a portion of desc heap.");
				return Status::ERROR_INTERNAL;
			}

			GraphicsAPI::ConstantBufferView cbv;
			void* cbPtrForWrite;
			RETURN_IF_STATUS_FAILED(fws->m_volatileConstantBuffer.Allocate(sizeof(CB), &cbv, &cbPtrForWrite));

			uint32_t nbDispatchThreadGroups = 0;
			if (gp->m_input.type == BVHTask::GeometryInput::Type::TrianglesIndexed)
				nbDispatchThreadGroups = GraphicsAPI::ROUND_UP(cmp.indexBuffer.count, m_threadDim_X);
			else if (gp->m_input.type == BVHTask::GeometryInput::Type::Triangles)
				nbDispatchThreadGroups = GraphicsAPI::ROUND_UP(cmp.vertexBuffer.count, m_threadDim_X);
			else {
				Log::Fatal(L"Unsupported input type detected.");
				return Status::ERROR_INTERNAL;
			}

			if (op == BuildOp::VertexUpdate) {
				uint32_t vertexCnt = cmp.vertexBuffer.count;

				if (cmp.indexRange.isEnabled)
					vertexCnt = cmp.indexRange.maxIndex - cmp.indexRange.minIndex + 1;

				nbDispatchThreadGroups = GraphicsAPI::ROUND_UP(vertexCnt, m_threadDim_X);
			}

#if defined(GRAPHICS_API_D3D12)
			bool is32BitIdcs = cmp.indexBuffer.format == DXGI_FORMAT_R32_UINT ? true : false;
#elif defined(GRAPHICS_API_VK)
			bool is32BitIdcs = cmp.indexBuffer.format == VK_FORMAT_R32_UINT ? true : false;
#endif

			//  TileCacheBuild,
			// 	MeshColorBuild,
			// 	MeshColorPostBuild,
			// 	VertexUpdate,
			GraphicsAPI::ComputePipelineState* psoPtr = nullptr;
			{
				// allocation
				size_t permutationIdx = uint32_t(op);
				permutationIdx += gp->m_input.type == BVHTask::GeometryInput::Type::TrianglesIndexed ? (size_t)AllocationShaderPermutationBits::e_USE_VERTEX_INDEX_INPUTS : 0;
				psoPtr = m_pso_allocate_itr[permutationIdx]->GetCSPSO(pws);
			}

			if (psoPtr != *currentPSO) {
				cmdList->SetComputePipelineState(psoPtr);
				*currentPSO = psoPtr;
			}

			// input vertex buffer
			uint32_t	vtxSRV_OffsetElm = 0; // VK needs 16 byte alignment for SRV offset;
			{
				std::unique_ptr<GraphicsAPI::ShaderResourceView> srv = std::make_unique<GraphicsAPI::ShaderResourceView>();
				uint64_t totalOffsetInBytes = cmp.vertexBuffer.offsetInBytes;
				uint64_t totalSizeInBytes = (uint64_t)cmp.vertexBuffer.count * cmp.vertexBuffer.strideInBytes;

				// adjust SRV range if indexRange is enabled.
				if (cmp.indexRange.isEnabled) {
					totalOffsetInBytes += cmp.indexRange.minIndex * cmp.vertexBuffer.strideInBytes;
					totalSizeInBytes = (uint64_t)(cmp.indexRange.maxIndex - cmp.indexRange.minIndex + 1) * cmp.vertexBuffer.strideInBytes;
				}

#if defined(GRAPHICS_API_D3D12)
				{
					D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc =
						GraphicsAPI::Utils::BufferResourceViewDesc_R32F(
							totalOffsetInBytes / sizeof(float),
							(uint32_t)(totalSizeInBytes / sizeof(float)));

					srv->InitFromApiData(cmp.vertexBuffer.resource, &srvDesc);
				}
#elif defined(GRAPHICS_API_VK)
				{
					// SRV offset adjustment.
					assert(totalOffsetInBytes % 4 == 0);
					uint64_t negOffsetInBytes = totalOffsetInBytes % 16;

					vtxSRV_OffsetElm = (uint32_t)negOffsetInBytes / sizeof(float);
					totalOffsetInBytes -= negOffsetInBytes;
					totalSizeInBytes += negOffsetInBytes;

					if (!srv->InitFromApiData(&dev, cmp.vertexBuffer.typedBuffer, VK_FORMAT_R32_SFLOAT,
						totalOffsetInBytes, totalSizeInBytes)) {
						Log::Fatal(L"Failed to create a SRV");
						return Status::ERROR_INTERNAL;
					}
				}
#endif
				descTable.SetSrv(&dev, 1, 0, srv.get()); // t0, heap offset: 1, tableLayout(1, 0)
				pws->DeferredRelease(std::move(srv)); // This object will be destrcuted after finishing the GPU task.
			}

			// input index buffer
			uint32_t	idxSRV_OffsetElm = 0; // VK needs 16 byte alignment for SRV offset;
			if (gp->m_input.type == BVHTask::GeometryInput::Type::TrianglesIndexed && (op != BuildOp::VertexUpdate)) {
				std::unique_ptr<GraphicsAPI::ShaderResourceView> srv = std::make_unique<GraphicsAPI::ShaderResourceView>();

#if defined(GRAPHICS_API_D3D12)
				{
					if (is32BitIdcs) {
						D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc =
							GraphicsAPI::Utils::BufferResourceViewDesc_R32U(
								cmp.indexBuffer.offsetInBytes / sizeof(uint32_t),
								cmp.indexBuffer.count);
						srv->InitFromApiData(cmp.indexBuffer.resource, &srvDesc);
					}
					else {
						D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc =
							GraphicsAPI::Utils::BufferResourceViewDesc_R16U(
								cmp.indexBuffer.offsetInBytes / sizeof(uint16_t),
								cmp.indexBuffer.count);
						srv->InitFromApiData(cmp.indexBuffer.resource, &srvDesc);
					}
				}
#elif defined(GRAPHICS_API_VK)
				{
					if (is32BitIdcs) {
						// SRV offset adjustment.
						uint64_t totalOffsetInBytes = cmp.indexBuffer.offsetInBytes;
						uint64_t totalSizeInBytes = (uint64_t)cmp.indexBuffer.count * sizeof(uint32_t);
						assert(totalOffsetInBytes % 4 == 0);
						uint64_t negOffsetInBytes = totalOffsetInBytes % 16;

						idxSRV_OffsetElm = (uint32_t)negOffsetInBytes / sizeof(uint32_t);
						totalOffsetInBytes -= negOffsetInBytes;
						totalSizeInBytes += negOffsetInBytes;

						if (!srv->InitFromApiData(&dev, cmp.indexBuffer.typedBuffer,
							VK_FORMAT_R32_UINT, totalOffsetInBytes, totalSizeInBytes)) {
							Log::Fatal(L"Failed to create a SRV");
							return Status::ERROR_INTERNAL;
						}
					}
					else {
						// SRV offset adjustment.
						uint64_t totalOffsetInBytes = cmp.indexBuffer.offsetInBytes;
						uint64_t totalSizeInBytes = (uint64_t)cmp.indexBuffer.count * sizeof(uint16_t);
						assert(totalOffsetInBytes % 2 == 0);
						uint64_t negOffsetInBytes = totalOffsetInBytes % 16;

						idxSRV_OffsetElm = (uint32_t)negOffsetInBytes / sizeof(uint16_t);
						totalOffsetInBytes -= negOffsetInBytes;
						totalSizeInBytes += negOffsetInBytes;

						if (!srv->InitFromApiData(&dev, cmp.indexBuffer.typedBuffer,
							VK_FORMAT_R16_UINT, totalOffsetInBytes, totalSizeInBytes)) {
							Log::Fatal(L"Failed to create a SRV");
							return Status::ERROR_INTERNAL;
						}
					}
				}
#endif

				descTable.SetSrv(&dev, 2, 0, srv.get()); // t1, heap offset: 2, tableLayout(1, 1)
				pws->DeferredRelease(std::move(srv)); // This object will be destrcuted after finishing the GPU task.
			}
			else {
				// null idx buffer view.
				descTable.SetSrv(&dev, 2, 0, pws->m_nullBufferSRV.get());	// t1, heap offset: 2, tableLayout(1, 1)
			}

			// CB
			{
				CB cb = {};

				cb.m_vertexStride = cmp.vertexBuffer.strideInBytes / sizeof(float);
				cb.m_nbVertices = cmp.vertexBuffer.count;
				if (cmp.indexRange.isEnabled)
					cb.m_nbVertices = cmp.indexRange.maxIndex - cmp.indexRange.minIndex + 1;

				cb.m_nbIndices = cmp.indexBuffer.count;
				cb.m_dstVertexBufferOffsetIdx = (uint32_t)(gp->m_vertexBufferOffsetInBytes / sizeof(uint32_t));

				cb.m_indexRangeMin = cmp.indexRange.isEnabled ? cmp.indexRange.minIndex : 0u;
				cb.m_indexRangeMax = cmp.indexRange.isEnabled ? cmp.indexRange.maxIndex : 0xFFFF'FFFFu;
				cb.m_tileResolutionLimit = gp->m_input.tileResolutionLimit;
				cb.m_tileUnitLength = gp->m_input.tileUnitLength;

				cb.m_enableTransformation = cmp.useTransform;
				cb.m_nbDispatchThreads = nbDispatchThreadGroups * m_threadDim_X;
				if (op == BuildOp::MeshColorBuild || op == BuildOp::MeshColorPostBuild) {
					cb.m_nbHashTableElemsNum = (uint32_t)gp->m_edgeTableBuffer->m_size / (sizeof(uint32_t));
				}
				else {
					cb.m_nbHashTableElemsNum = 0;
				}

				cb.m_allocationOffset = gp->m_input.surfelType == BVHTask::GeometryInput::SurfelType::MeshColors ? 2 * (uint32_t)gp->m_totalNbIndices : 0;

				cb.m_vtxSRVOffsetElm = vtxSRV_OffsetElm;
				cb.m_idxSRVOffsetElm = idxSRV_OffsetElm;
				cb.m_idxComponentOffset = gp->m_indexOffsets[cmpIdx];
				cb.m_vtxComponentOffset = gp->m_vertexOffsets[cmpIdx];

				cb.m_transformationMatrix = cmp.transform;
				memcpy(cbPtrForWrite, &cb, sizeof(cb));

				descTable.SetCbv(&dev, 0, 0, &cbv); // b0, heap offset:0, tableLayout(0, 0)
			}

			if (gp->m_edgeTableBuffer)
				descTable.SetUav(&dev, 3, 0, gp->m_edgeTableBuffer->m_uav.get());
			else
				descTable.SetUav(&dev, 3, 0, pws->m_nullBufferUAV.get());

			descTable.SetUav(&dev, 4, 0, gp->m_index_vertexBuffer->m_uav.get());

			if (op != BuildOp::VertexUpdate) {
				// Allocation task.
				if (gp->m_input.forceDirectTileMapping) {
					// null view for directLightingcache counter and its index.
					descTable.SetUav(&dev, 5, 0, pws->m_nullBufferUAV.get()); // u1, heap offset: 4, talbeLayout(2, 1)
					descTable.SetUav(&dev, 6, 0, pws->m_nullBufferUAV.get()); // u2, heap offset: 5, talbeLayout(2, 2)
				}
				else {
					descTable.SetUav(&dev, 5, 0, gp->m_directLightingCacheCounter->m_uav.get()); // u1, heap offset: 4, talbeLayout(2, 1)

					if (gp->m_directLightingCacheIndices)
						descTable.SetUav(&dev, 6, 0, gp->m_directLightingCacheIndices->m_uav.get()); // u2, heap offset: 5, talbeLayout(2, 2)
					else
						descTable.SetUav(&dev, 6, 0, pws->m_nullBufferUAV.get());
				}
			}
			else {
				// Update task.
				// vertex index, tile counter, tile index are null uav views.
				descTable.SetUav(&dev, 5, 0, pws->m_nullBufferUAV.get()); // u1, heap offset: 4, talbeLayout(2, 1)
				descTable.SetUav(&dev, 6, 0, pws->m_nullBufferUAV.get()); // u2, heap offset: 5, talbeLayout(2, 2)
			}

			{
				std::vector<GraphicsAPI::DescriptorTable*> descTables = { &descTable };
				cmdList->SetComputeRootDescriptorTable(&m_rootSignature, 0, descTables.data(), descTables.size());
			}

			cmdList->Dispatch(nbDispatchThreadGroups, 1, 1);
		}

		return Status::OK;
	};
};


