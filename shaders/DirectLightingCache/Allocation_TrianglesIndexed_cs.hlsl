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
#pragma pack_matrix(row_major)

#include "Shared/Binding.hlsli"
#include "Shared/Shared.hlsli"

// Must match CPP enum
#define BUILD_OP_TILE_CACHE          (0)
#define BUILD_OP_MESH_COLORS         (1)
#define BUILD_OP_MESH_COLORS_POST    (2)
#define BUILD_OP_VERTEX_UPDATE       (3)

struct CB_Allocation_TrianglesIndexed
{
    uint    m_vertexStride;
    uint    m_nbVertices;
    uint    m_nbIndices;
    uint    m_dstVertexBufferOffsetIdx; // Dest indices and vertices buffers are now unified. It needs the offset.

    uint    m_indexRangeMin;
    uint    m_indexRangeMax;
    uint    m_tileResolutionLimit;
    float   m_tileUnitLength;

    uint    m_enableTransformation;
    uint    m_nbDispatchThreads;
    uint    m_nbHashTableElemsNum;
    uint    m_allocationOffset;

    uint    m_vtxSRVOffsetElm;
    uint    m_idxSRVOffsetElm;
    uint    m_padd_0;
    uint    m_padd_1;

    float4x4    m_transformationMatrix;
};

//[[vk::binding(0, 0)]]
KS_VK_BINDING(0, 0)
ConstantBuffer<CB_Allocation_TrianglesIndexed> CB : register(b0);

//[[vk::binding(1, 0)]]
KS_VK_BINDING(1, 0)
Buffer<float> t_vertexBuffer : register(t0);

// It's fine to use <uint> type for loading UINT16 and UINT32 typed buffer in both VK and D3D12.
// (Implicit) sampler converts the format.
//[[vk::binding(2, 0)]]
KS_VK_BINDING(2, 0)
Buffer<uint>  t_indexBuffer : register(t1);

//[[vk::binding(3, 0)]]
KS_VK_BINDING(3, 0)
RWBuffer<uint> u_edgeTable : register(u0); 

//[[vk::binding(4, 0)]]
KS_VK_BINDING(4, 0)
RWBuffer<uint> u_index_vertexBuffer : register(u1);   // transformed vertex buffer. need to be defined as uint since it will be bound with direct lighting cache.

//[[vk::binding(5, 0)]]
KS_VK_BINDING(5, 0)
RWBuffer<uint> u_tileCounter : register(u2);    // for atomic counter to calculate offsets and total number of the tiles.

//[[vk::binding(6, 0)]]
KS_VK_BINDING(6, 0)
DLCBufferType u_tileIndex : register(u3);

#define GROUP_SIZE 96 // need to be multiple of 3

groupshared float3  s_vPos[GROUP_SIZE];
groupshared uint    s_idcs[GROUP_SIZE];
groupshared uint    s_edgeSize[GROUP_SIZE];

float3 SampleVertexBuffer(uint offset)
{
    return float3(
        t_vertexBuffer[offset + CB.m_vtxSRVOffsetElm + 0],
        t_vertexBuffer[offset + CB.m_vtxSRVOffsetElm + 1],
        t_vertexBuffer[offset + CB.m_vtxSRVOffsetElm + 2]);
}

uint GetVertexIndex(uint index)
{
    return t_indexBuffer[index + CB.m_idxSRVOffsetElm];
}

uint AllocateFromBuffer(RWBuffer<uint> buffer, uint allocSize) {
    const uint totalAllocAcrossWave = WaveActiveSum(allocSize);

    uint globalOffset;
    if (WaveIsFirstLane()) {
        InterlockedAdd(buffer[0], totalAllocAcrossWave, globalOffset);
    }

    const uint baseAllocationOffset = CB.m_allocationOffset;
    const uint localOffset = WavePrefixSum(allocSize);
    return baseAllocationOffset + localOffset + WaveReadLaneFirst(globalOffset);
}

struct Edge {

    void Init(uint i0, uint i1) {
        _isReversed = i0 < i1;
        _i0 = min(i0, i1);
        _i1 = max(i0, i1);

        _p0.xyz = SampleVertexBuffer(_i0 * CB.m_vertexStride);
        _p1.xyz = SampleVertexBuffer(_i1 * CB.m_vertexStride);
    }

    bool IsReversed() {
        return _isReversed;
    }

    uint GetHash() {
#if 0
        return murmurUint2(_i0, _i1, 1337);
#else
        return murmurUint6(
            asuint(_p0.x), asuint(_p0.y), asuint(_p0.z),
            asuint(_p1.x), asuint(_p1.y), asuint(_p1.z),
            1337);
#endif
    }

    float GetSize() {
		if (any(isnan(_p0)) || any(isinf(_p0)) || any(isnan(_p1)) || any(isinf(_p1))) {
			return 0.01f;
		}
        return distance(_p0, _p1);
    }

    bool _isReversed;
    uint _i0;
    uint _i1;

    float3 _p0;
    float3 _p1;
};

MeshColors::UnresolvedEdgeColor GetEdge(Edge e, uint halfEdgeId, uint log2R) {
    const uint kBucketSize = 4; // key | primitiveA | offset | min(R)

    const uint hash = e.GetHash();
    const uint hashTableEntryCount = CB.m_nbHashTableElemsNum / kBucketSize;
    uint idx = (hash % hashTableEntryCount) * kBucketSize;

    const uint kMaxIter = 512;
    uint iterCnt = 0;
    uint out_hash = 0;

    [allow_uav_condition]
    while ((iterCnt != kMaxIter)) {

        InterlockedCompareExchange(
            u_edgeTable[idx],
            0,
            hash,
            out_hash
        );
        iterCnt++;

        // Either the first (=0) or second (out_hash == hash)
        if (out_hash == hash || out_hash == 0)
            break;

        idx = (idx + kBucketSize) % CB.m_nbHashTableElemsNum;
    } 

    [branch]
    if (iterCnt == kMaxIter) {
        // This means we had over kMaxIter hash conflits in our table,
        // and our attempts to find a matching edge failed.
        // this is not expected to happen.
        MeshColors::UnresolvedEdgeColor uNullEdge;
        uNullEdge.hashTableIndex    = MeshColors::kInvalidOffset;
        uNullEdge.isReversed        = false;
        return uNullEdge;
    }

    // Match found!
    uint sharedEdgeId = 0;
    {
        uint potentialEdgeId = halfEdgeId + 1;

        InterlockedCompareExchange(
            u_edgeTable[idx + 1],
            0,
            potentialEdgeId,
            sharedEdgeId
        );
    }

    // If sharedEdgeId == 0, we're the first one to request this edge, hence the responsibility to
    // allocate edge samples falls on this thread.
    // We're going to allocate for R number of samples, we may end up using 
    // fewer samples along the edge, but it's guaranteed to never be more than that.
    if (sharedEdgeId == 0)
    {
        // This thread will allocatate this many samples.
        uint R = 1u << log2R;
        uint samplesPerEdge = R - 1;
        uint allocSize = 2 * samplesPerEdge;

        u_edgeTable[idx + 2] = AllocateFromBuffer(u_tileCounter, allocSize);
    }

    {
        // Goal:    We want to store the min(log2R) if this edge.
        // The buffer is initialized to zero intialized, doing InterlockedMin(log2R) will always result in zero.
        // Instead we reverse the bits and run InterlockedMax(~log2R).
        InterlockedMax( u_edgeTable[idx + 3], ~log2R);
    }

    MeshColors::UnresolvedEdgeColor uEdge;
    uEdge.hashTableIndex    = idx;
    uEdge.isReversed        = e.IsReversed();
    return uEdge;
}

// 
#if BUILD_OP == BUILD_OP_VERTEX_UPDATE
// just update vertex posistions
[numthreads(GROUP_SIZE, 1, 1)]
void main(
    uint2 groupIdx : SV_GroupID,
    uint2 globalIdx : SV_DispatchThreadID,
    uint2 threadIdx : SV_GroupThreadID)
{
    [loop]
    for (int vIdx = globalIdx.x; vIdx < CB.m_nbVertices; vIdx += CB.m_nbDispatchThreads) {
        uint    vBaseOffset = vIdx * CB.m_vertexStride;
        float4  vPos;

        vPos.xyz = SampleVertexBuffer(vBaseOffset);
        vPos.w = 1.0;

        if (CB.m_enableTransformation != 0) {
            vPos = mul(vPos, CB.m_transformationMatrix);
            vPos.xyz /= vPos.w;
        }

        u_index_vertexBuffer[CB.m_dstVertexBufferOffsetIdx + vIdx * 3 + 0] = asuint(vPos.x);
        u_index_vertexBuffer[CB.m_dstVertexBufferOffsetIdx + vIdx * 3 + 1] = asuint(vPos.y);
        u_index_vertexBuffer[CB.m_dstVertexBufferOffsetIdx + vIdx * 3 + 2] = asuint(vPos.z);
    }
}
#endif

#if BUILD_OP == BUILD_OP_MESH_COLORS_POST
[numthreads(GROUP_SIZE, 1, 1)]
void main(
    uint2 groupIdx : SV_GroupID,
    uint2 globalIdx : SV_DispatchThreadID,
    uint2 threadIdx : SV_GroupThreadID)
{
    if (globalIdx.x == 0) {
        SurfelCache::Header header;
        header.format = 1;
        SurfelCache::StoreHeader(u_tileIndex, 0, header);
    }

    s_edgeSize[threadIdx.x] = 0;

    GroupMemoryBarrierWithGroupSync();

    if (globalIdx.x < CB.m_nbIndices)
    { 
        const uint halfEdgeId   = globalIdx.x;
        
        // Read the unresolved edge from previous pass...
        uint slotIndex                          = MeshColors::GetHalfEdgeBufferLocation(halfEdgeId);
        const uint uEdgeData                    = MeshColors::Load(u_tileIndex, 0, slotIndex);
        MeshColors::UnresolvedEdgeColor uEdge   = MeshColors::UnresolvedEdgeUnpack(uEdgeData);

        uint edgeOffset = MeshColors::kInvalidOffset;
        uint log2R      = 0;

        [flatten]
        if (uEdge.hashTableIndex != MeshColors::kInvalidOffset) {
            edgeOffset  =  u_edgeTable[uEdge.hashTableIndex + 2];
            log2R       = ~u_edgeTable[uEdge.hashTableIndex + 3]; // Flip back bits, as explained above.
        }

        // Store the max edge length...
        InterlockedMax(s_edgeSize[threadIdx.x / 3], log2R);

        // Resolve R and edge offset and store.
        MeshColors::EdgeColor edge;
        edge.log2R          = log2R;
        edge.offset         = edgeOffset;
        edge.isReversed     = uEdge.isReversed;

        const uint packedEdge = MeshColors::EdgePack(edge);
        MeshColors::Store(u_tileIndex, 0, slotIndex, packedEdge);
    }

    GroupMemoryBarrierWithGroupSync();

    // Face
    if (globalIdx.x < CB.m_nbIndices && globalIdx.x % 3 == 0)
    {
        uint log2R = s_edgeSize[(threadIdx.x / 3)];

        uint R = 1u << log2R;
        uint samplesPerFace = (R - 1) * (R - 2) / 2;

        uint offset = AllocateFromBuffer(u_tileCounter, 2 * samplesPerFace);

        uint primitiveIndex = globalIdx.x / 3;

        MeshColors::FaceColor face;
        face.offset = offset;
        face.log2R = log2R;

        MeshColors::Store(
            u_tileIndex,
            0,
            MeshColors::GetFaceSlotBufferLocation(primitiveIndex),
            MeshColors::FacePack(face));
    }
}
#endif

#if BUILD_OP == BUILD_OP_MESH_COLORS
[numthreads(GROUP_SIZE, 1, 1)]
void main(
    uint2 groupIdx : SV_GroupID,
    uint2 globalIdx : SV_DispatchThreadID,
    uint2 threadIdx : SV_GroupThreadID)
{
    // load vertx, transform and store it to vertexBuffer. vertex order is unchanged
    [loop]
    for (int vIdx = globalIdx.x; vIdx < CB.m_nbVertices; vIdx += CB.m_nbDispatchThreads) {
        uint    vBaseOffset = vIdx * CB.m_vertexStride;
        float4  vPos;

        vPos.xyz = SampleVertexBuffer(vBaseOffset);
        vPos.w = 1.0;

        if (CB.m_enableTransformation != 0) {
            vPos = mul(vPos, CB.m_transformationMatrix);
            vPos.xyz /= vPos.w;
        }

        u_index_vertexBuffer[CB.m_dstVertexBufferOffsetIdx + vIdx * 3 + 0] = asuint(vPos.x);
        u_index_vertexBuffer[CB.m_dstVertexBufferOffsetIdx + vIdx * 3 + 1] = asuint(vPos.y);
        u_index_vertexBuffer[CB.m_dstVertexBufferOffsetIdx + vIdx * 3 + 2] = asuint(vPos.z);
    }

    // load vtx[idx], transform and store it to shared mem.
    if (globalIdx.x < CB.m_nbIndices) {
        uint    vtxIdx = globalIdx.x; // flatten vtxIdx.

#if USE_VERTEX_INDEX_INPUTS
        {
            vtxIdx = clamp(GetVertexIndex(globalIdx.x), CB.m_indexRangeMin, CB.m_indexRangeMax);
            vtxIdx -= CB.m_indexRangeMin; // adjust index as SRV for the input VB is also adjusted its offset.
        }
#endif

        uint    vBaseOffset = vtxIdx * CB.m_vertexStride;
        float4  vPos;

        vPos.xyz = SampleVertexBuffer(vBaseOffset);
        vPos.w = 1.0;

        if (CB.m_enableTransformation != 0) {
            vPos = mul(vPos, CB.m_transformationMatrix);
            vPos.xyz /= vPos.w;
        }

        s_vPos[threadIdx.x] = vPos.xyz;
        s_idcs[threadIdx.x] = vtxIdx;
    }

    GroupMemoryBarrierWithGroupSync();

    // Shared mem should have 96 verts (32 pirms) with its indices.


    // (Half)Edge & Vertex
    uint i0 = 0;
	if (globalIdx.x < CB.m_nbIndices)
    {
        const bool threadIsValid = globalIdx.x < CB.m_nbIndices;

        // Do edge analysis + store in meshColorsHeader buffer.
        uint halfEdgeId = globalIdx.x;

        const uint baseIndex = 3 * (threadIdx.x / 3);
             i0 = threadIdx.x; // baseIndex + ((threadIdx.x + 0) % 3);
        uint i1 = baseIndex + ((threadIdx.x + 1) % 3);
        uint i2 = baseIndex + ((threadIdx.x + 2) % 3);

        const float3 v0 = s_vPos[i1] - s_vPos[i0];
        const float3 v1 = s_vPos[i2] - s_vPos[i0];

		// The number of desired samples per primitive is given by the tile unit length, and triangle area A.
        const float A = 0.5 * length(cross(v0, v1));
        const float TileUnitA = max(CB.m_tileUnitLength * CB.m_tileUnitLength, 0.000001f);
        float SamplesPerFace = A / TileUnitA;

		// Q: All good so far. But why the seemingly random 2 * term below?
		// A: It's to approximate the tile cache sample calculation,
		// the goal is that toggling between mesh colors and tile cache should produce the same number of samples (roughly).
        SamplesPerFace = 2 * clamp(SamplesPerFace, 0, CB.m_tileResolutionLimit * CB.m_tileResolutionLimit);

		uint log2R = 0;
		{
			// The code belove solves the quadratic equation for R, where samples per face is given by:
			// SamplesPerFace	= (R - 1) * (R - 2) / 2;
			// SamplesPerEdge	= (R - 1);
			// SamplesPerVertex = 1;
			// <=> SamplesPerPrimitive =  SamplesPerFace + 3 * SamplesPerEdge + 3 * SamplesPerVertex
			// <=> SamplesPerPrimitive = (R^2 + 3R + 2) / 2,
			// <=>					 0 =  R^2 + 3R + (2 - 2 * SamplesPerPrimitive)

			// const float a = 1.f;
			const float b       = 3.f;
			const float c       = 2.f - 2.f * SamplesPerFace;
			const float Rf      = (-b + sqrt(b * b - 4 * c)) * 0.5f;
			const uint R        = max(uint(Rf), 1);
			log2R               = firstbithigh(R);
		}

        {
            Edge e;
            e.Init(s_idcs[i0], s_idcs[i1]);

            MeshColors::UnresolvedEdgeColor uEdge = GetEdge(e, halfEdgeId, log2R);

            MeshColors::Store(
                u_tileIndex, 0,
                MeshColors::GetHalfEdgeBufferLocation(halfEdgeId),
                MeshColors::UnresolvedEdgePack(uEdge));
        }
    }

    if (globalIdx.x < CB.m_nbIndices)
    {
        {
            // Store index in meshColorsHeader buffer.
            uint primitiveIndex = globalIdx.x / 3;
            uint vertexOffset = globalIdx.x % 3;

            MeshColors::VertexColor vertex;
            vertex.offset = s_idcs[i0];

            MeshColors::Store(
                u_tileIndex, 0,
                MeshColors::GetVertexSlotBufferLocation(primitiveIndex, vertexOffset),
                MeshColors::VertexPack(vertex));
        }
    }

    // Face
    if (globalIdx.x < CB.m_nbIndices && globalIdx.x % 3 == 0)
    {
        [unroll]
        for (int i = 0; i < 3; ++i) {
            // store index array along the order.
            u_index_vertexBuffer[globalIdx.x + i] = s_idcs[threadIdx.x + i];
        }
    }
}
#endif

#if BUILD_OP == BUILD_OP_TILE_CACHE
// transform, sort triangle indices and calculate tile budget.
[numthreads(GROUP_SIZE, 1, 1)]
void main(
    uint2 groupIdx : SV_GroupID,
    uint2 globalIdx : SV_DispatchThreadID,
    uint2 threadIdx : SV_GroupThreadID)
{
    if (globalIdx.x == 0) {
        SurfelCache::Header header;
        header.format = 0;
        SurfelCache::StoreHeader(u_tileIndex, 0, header);
    }

    // load vertx, transform and store it to vertexBuffer. vertex order is unchanged
    [loop]
    for (int vIdx = globalIdx.x; vIdx < CB.m_nbVertices; vIdx += CB.m_nbDispatchThreads) {
        uint    vBaseOffset = vIdx * CB.m_vertexStride;
        float4  vPos;

        vPos.xyz = SampleVertexBuffer(vBaseOffset);
        vPos.w = 1.0;

        if (CB.m_enableTransformation != 0) {
            vPos = mul(vPos, CB.m_transformationMatrix);
            vPos.xyz /= vPos.w;
        }

        u_index_vertexBuffer[CB.m_dstVertexBufferOffsetIdx + vIdx * 3 + 0] = asuint(vPos.x);
        u_index_vertexBuffer[CB.m_dstVertexBufferOffsetIdx + vIdx * 3 + 1] = asuint(vPos.y);
        u_index_vertexBuffer[CB.m_dstVertexBufferOffsetIdx + vIdx * 3 + 2] = asuint(vPos.z);
    }

    // load vtx[idx], transform and store it to shared mem.
    if (globalIdx.x < CB.m_nbIndices) {
        uint    vtxIdx = globalIdx.x; // flatten vtxIdx.

#if USE_VERTEX_INDEX_INPUTS
        {
            vtxIdx = clamp(GetVertexIndex(globalIdx.x), CB.m_indexRangeMin, CB.m_indexRangeMax);
            vtxIdx -= CB.m_indexRangeMin; // adjust index as SRV for the input VB is also adjusted its offset.
        }
#endif

        uint    vBaseOffset = vtxIdx * CB.m_vertexStride;
        float4  vPos;

        vPos.xyz = SampleVertexBuffer(vBaseOffset);
        vPos.w = 1.0;

        if (CB.m_enableTransformation != 0) {
            vPos = mul(vPos, CB.m_transformationMatrix);
            vPos.xyz /= vPos.w;
        }

        s_vPos[threadIdx.x] = vPos.xyz;
        s_idcs[threadIdx.x] = vtxIdx;
    }

    GroupMemoryBarrierWithGroupSync();

    // Shared mem should have 96 verts (32 pirms) with its indices.

    if (globalIdx.x < CB.m_nbIndices && globalIdx.x % 3 == 0) {
        // per triangle.
        int         i;
        float3      vPos[3];
        uint        idcs[3];

        [unroll]
        for (i = 0; i < 3; ++i) {
            vPos[i] = s_vPos[threadIdx.x + i];
            idcs[i] = s_idcs[threadIdx.x + i];
        }

        float3 edges[3];
        float eLenSq[3];

        edges[0] = vPos[1] - vPos[0];
        edges[1] = vPos[2] - vPos[1];
        edges[2] = vPos[0] - vPos[2];

        eLenSq[0] = dot(edges[0], edges[0]);
        eLenSq[1] = dot(edges[1], edges[1]);
        eLenSq[2] = dot(edges[2], edges[2]);

        uint triOffset = 1;

        // barycentrics.xy give weights for 2nd and 3rd vertices.

        if (eLenSq[0] > eLenSq[1] && eLenSq[0] > eLenSq[2]) {
            // e0 is the longest
            // vtx order = 2, 1, 0
            // bc: x y (1-x-y) = 1, 0, 2
            triOffset = 2;
        }
        else if (eLenSq[1] > eLenSq[0] && eLenSq[1] > eLenSq[2]) {
            // e1 is the longest
            // vtx order = 0, 1, 2
            // bc: x y (1-x-y) = 1, 2, 0
            triOffset = 0;
        }
        else {
            // e2 is the longest
            // vtx order = 1, 2, 0
            // bc: x y (1-x-y) = 2, 0, 1
        }

        [unroll]
        for (i = 0; i < 3; ++i) {
            vPos[i] = s_vPos[threadIdx.x + (i + triOffset) % 3];
            idcs[i] = s_idcs[threadIdx.x + (i + triOffset) % 3];
        }
        // now vPos and idcs were ordered so that the 2nd edge is the longest edge.
        //  1
        //  |   
        //  |        
        //  0-----------2
        //

        [unroll]
        for (i = 0; i < 3; ++i) {
            // store index array along the order.
            u_index_vertexBuffer[globalIdx.x + i] = idcs[i].x;
        }

        // calc tile budget with the edge length.
        {
            float3 edgeU = vPos[1] - vPos[0];
            float3 edgeV = vPos[2] - vPos[0];
            float lenU = length(edgeU);
            float lenV = length(edgeV);

            uint2 tileResolutions;
            tileResolutions.x = ceil(lenU / CB.m_tileUnitLength);
            tileResolutions.y = ceil(lenV / CB.m_tileUnitLength);
            tileResolutions = clamp(tileResolutions, 1, CB.m_tileResolutionLimit);

            uint nbTiles = tileResolutions.x * tileResolutions.y;
            uint primIdx = globalIdx.x / 3;

            uint tileOffset = AllocateFromBuffer(u_tileCounter, nbTiles);

            TileCache::StoreTileCacheEntry(u_tileIndex, 0, primIdx, tileOffset, tileResolutions);
        }
    }
}
#endif
