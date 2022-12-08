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

#include "../Shared/Binding.hlsli"
#include "../Shared/Shared.hlsli"

#if !defined(KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE)
#error "KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE has to be defined."
#endif

#if !defined(KICKSTARTRT_USE_BYTEADDRESSBUFFER_FOR_DLC)
#error "KICKSTARTRT_USE_BYTEADDRESSBUFFER_FOR_DLC has to be defined."
#endif

// Indirection Table 4 DWORD for a TLAS instance.
// [BufferBlockUAV index for a TLC index buffer][offst for a TLC index buffer][BufferBlockUAV index for a TLC buffer][offst for a TLC buffer]....
//[[vk::binding(1, 1)]]
KS_VK_BINDING(1, 1)
RWBuffer<uint4>   u_directLightingCacheIndirectionTable : register(u0, space1);

// A entry consists of a pair of indexBuffer and Buffer [DirectLightingCache Index for TLASInstance:0][DirectLightingCache Buffer for TLAS Instance:0]....
// If KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE is enabled, the UAV array will be.. [UAV for zero View][UAV for null View][UAV for shared buffer block:0 for DirectLightingCache Buffer]...
// unbounded size.
//[[vk::binding(2, 1)]]
KS_VK_BINDING(2, 1)
DLCBufferType   u_directLightingCacheBuffer[]       : register(u1, space1);

/* This didn't work well with VK. Seems "out RWBuffer<uint>" wasn't treated properly.
void GetTileIndexBuffer(uint instanceIdx,
    out RWBuffer<uint> tileIndexBuf, out uint offsetInTileIndexBuf,
    out RWBuffer<uint> tileBuf, out uint offsetInTileBuf)
{
    uint4 tableEntry = u_tileTable[instanceIdx];
    //   tileIndexBuf = u_directLightingCacheBuffer[tableEntry.x];
    tileIndexBuf = u_directLightingCacheBuffer[instanceIdx*2];
    //offsetInTileIndexBuf = tableEntry.y;
    offsetInTileIndexBuf = 0xFFFFFFFF;
    //   tileBuf = u_directLightingCacheBuffer[tableEntry.z];
    tileBuf = u_directLightingCacheBuffer[instanceIdx*2+1];
    //offsetInTileBuf = tableEntry.w;
    offsetInTileBuf = 0;
}
*/

// ---[ Functions ]---
float3 fromYCoCgToRGB(uint2 data, out bool hasClearTag)
{
    hasClearTag = (data.x & 0x80000000) == 0x80000000;
    float y = asfloat(data.x & 0x7FFFFFFF);
    float co = f16tof32(data.y >> 16);
    float cg = f16tof32(data.y & 0x0000FFFF);

    float3 yCoCg = float3(y, co, cg);

    float3 rgb;
    rgb.x = dot(yCoCg, float3(1, 1, -1));
    rgb.y = dot(yCoCg, float3(1, 0, 1));
    rgb.z = dot(yCoCg, float3(1, -1, -1));

    return rgb;
}

uint2 fromRGBToYCoCg(float3 rgb, bool setClearTag)
{
    float3 yCoCg;

    yCoCg.x = dot(rgb, float3(0.25, 0.5, 0.25));
    yCoCg.y = dot(rgb, float3(0.5, 0, -0.5));
    yCoCg.z = dot(rgb, float3(-0.25, 0.5, -0.25));

    uint2 data;
    data.x = asuint(yCoCg.x); //  & 0x7FFFFFFF it always positive as far as input is valid.
    if (setClearTag)
        data.x |= 0x80000000;
    data.y = (f32tof16(yCoCg.y) << 16) | f32tof16(yCoCg.z);

    return data;
}

// Map triangle BC to unit square UV.
float2 BCToUV(in float2 bc)
{
    float2 uv;
    if (bc.y > bc.x)
    {
        uv.x = bc.x * 2;
        uv.y = bc.x + bc.y;
    }
    else {
        uv.x = bc.x + bc.y;
        uv.y = bc.y * 2;
    }
    return uv;
}

namespace LightCache
{
    enum class DebugMode : uint {
        None,
        RandomColor,
        MeshColorClassification,
    };

    struct Query {
        uint        instanceIndex;
        uint        primitiveIndex;
        float2      bc;
        bool        bilinearSampling;

        DebugMode   debugMode;

        float3 GetBc3() {
            return float3(bc.x, bc.y, 1.f - bc.x - bc.y);
        }

        void Init() {
            instanceIndex       = 0;
            primitiveIndex      = 0;
            bc                  = 0;
            bilinearSampling    = 0;
            debugMode           = DebugMode::None;
        }
    };

    struct Result {
        DLCBufferIndex   buffer;
        uint            bufferIndex;

        float3      tileData;
        bool        hasClearTag;

        void Init() {
            bufferIndex = 0;
            tileData = 0;
            hasClearTag = 0;
        }
    };

    float3 GenerateRandomColor(uint instanceIndex, uint primitiveIndex) {
        float3 color;
        uint tiHash = murmurUint2(instanceIndex, primitiveIndex, 1337);
        color.r = uintToFloat(tiHash);
        tiHash = xorshift(tiHash);
        color.g = uintToFloat(tiHash);
        tiHash = xorshift(tiHash);
        color.b = uintToFloat(tiHash);
        return color;
    }

    DLCBufferIndex SampleTileCache(Query query, out uint tileIndex, out float3 tileData, out bool hasClearTag)
    {
#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
        uint indexBufferSlot, indexBufferBaseOffset, DLCBufferSlot, DLCBufferBaseOffset;
        {
            uint4 u4 = u_directLightingCacheIndirectionTable[query.instanceIndex];
            indexBufferSlot = u4.x;
            indexBufferBaseOffset = u4.y;
            DLCBufferSlot = u4.z;
            DLCBufferBaseOffset = u4.w;
        }
        DLCBufferType tileIndexBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(indexBufferSlot)];
        DLCBufferIndex tileBufferIndex = DLCBufferSlot;
        DLCBufferType tileBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(tileBufferIndex)];
#else
        DLCBufferType tileIndexBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(query.instanceIndex * 2 + 0)];
        DLCBufferIndex tileBufferIndex = query.instanceIndex * 2 + 1;
        DLCBufferType tileBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(tileBufferIndex)];
        uint indexBufferBaseOffset = 0;
        uint DLCBufferBaseOffset = 0;
#endif

        {
            // if a valid offset for tileIndexBuffer is assigned, load index buffer and calc tileIndex.
            uint baseOffset;
            uint2 tileResolutions;

            TileCache::LoadTileCacheEntry(tileIndexBuffer, indexBufferBaseOffset, query.primitiveIndex, baseOffset, tileResolutions);

            // bc.xy give weights for 2nd and 3rd vertices.
            float2 uv = min(BCToUV(query.bc), float2(0.999, 0.999));
            uint uIdx = (uint)(uv.x * (float)tileResolutions.x);
            uint vIdx = (uint)(uv.y * (float)tileResolutions.y);

            tileIndex = baseOffset + vIdx * tileResolutions.x + uIdx;

            if (tileResolutions.x == 0 && tileResolutions.y == 0) {
                // if a valid tileIndexBuffer is assigned, packedTielResolutions will always be nonzero value (e.g. at least 1x1 tile should be assigned).
                // it means tileIndexBuffer is unbound, and it should be direct tile mapping mode.
                // There is a case that tileBuffer is also unbound, but D3D12 safely access null UAV which is set in a desc table entry.
                tileIndex = query.primitiveIndex;
            }
        }

        tileIndex *= 2;
        tileIndex += DLCBufferBaseOffset;

        if (query.debugMode == DebugMode::RandomColor || query.debugMode == DebugMode::MeshColorClassification) {
            tileData = GenerateRandomColor(query.instanceIndex, tileIndex);
        }
        else //  if (query.debugMode == DebugMode::None) 
        {
            tileData = fromYCoCgToRGB(DLCBuffer::Load2(tileBuffer, tileIndex + 0), hasClearTag);
        }
        return tileBufferIndex;
    }

    DLCBufferIndex SampleMeshColor(MeshColors::MeshColorPrimInfo primInfo, Query query, float3 bc3, float2 bc, out uint index, out float3 color, out bool hasClearTag) {

        const uint2 ij = primInfo.face.GetR() * bc.xy;
        const uint k = primInfo.face.GetR() - ij.x - ij.y;

#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
        uint sampleBufferSlot, sampleBufferBaseOffset;
        {
            uint2 u2 = u_directLightingCacheIndirectionTable[query.instanceIndex].zw;
            sampleBufferSlot = u2.x;
            sampleBufferBaseOffset = u2.y;
        }
        DLCBufferIndex sampleBufferIndex = sampleBufferSlot;
        DLCBufferType sampleBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(sampleBufferIndex)];
#else
        DLCBufferIndex sampleBufferIndex = query.instanceIndex * 2 + 1;
        DLCBufferType sampleBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(sampleBufferIndex)];
        uint sampleBufferBaseOffset = 0;
#endif

        bool isVertex = false;
        uint edgeIndex = 0xFFFFFFFF;

        // [vertex color]
        if ((ij.x == 0 && ij.y == 0) ||
            (ij.x == 0 && ij.y == primInfo.face.GetR()) ||
            (ij.x == primInfo.face.GetR() && ij.y == 0)) {
            uint idx = 0;
            if (ij.x == 0 && ij.y == 0)
                idx = 0;
            if (ij.x == 0 && ij.y == primInfo.face.GetR())
                idx = 2;
            if (ij.x == primInfo.face.GetR() && ij.y == 0)
                idx = 1;

            isVertex = true;
            index = 2 * primInfo.vertex[idx].offset;
        }
        // [edge color]
        else if (ij.x == 0 || ij.y == 0 || k == 0)
        {
            uint edgeIdx = 0;
            uint u = 0;
            if (ij.x == 0) {
                edgeIdx = 2;
                u = ij.y;
            }
            if (ij.y == 0) {
                edgeIdx = 0;
                u = k;
            }
            if (k == 0) {
                edgeIdx = 1;
                u = ij.x;
            }


            MeshColors::EdgeColor edge = primInfo.edges[edgeIdx];

            if (edge.IsInvalid()) {
                index = MeshColors::kInvalidOffset;
                color = float3(1, 0, 0);
#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
                return 0; // null view
#else
                return sampleBufferIndex;
#endif
            }

            if (!edge.isReversed)
                u = primInfo.face.GetR() - u;

            // face.log2R is guaranteed to be greater than or equal edge.log2R
            const uint frac = 1u << (primInfo.face.log2R - edge.log2R); // == primInfo.face.GetR() / edge.GetR();
            uint i_e = u / frac;
            i_e = clamp(i_e, 0, edge.GetR() - 2);

            edgeIndex = edge.offset;

            index = 2 * i_e + edge.offset;
        }
        // [face color]
        else
        {
            // Offset within the face
            // This a triangle row-major format, for face with large number of samples a swizzling strategy might make more sense.
            const uint subIndex = MeshColors::ComputeSubTriID(uint2(ij.x - 1, ij.y + 1), primInfo.face.GetR() - 3);
            index = primInfo.face.offset + 2 * subIndex;
        }

        index += sampleBufferBaseOffset;

        if (query.debugMode == DebugMode::RandomColor) {
            color = GenerateRandomColor(query.instanceIndex, index);
        }
        else if (query.debugMode == DebugMode::MeshColorClassification)
        {
            color = edgeIndex != 0xFFFFFFFF ? GenerateRandomColor(edgeIndex, 0) : (isVertex ? float3(0, 0, 1) : float3(1, 0, 0));
            color = edgeIndex != 0xFFFFFFFF ? float3(0, 1, 0) : (isVertex ? float3(0, 0, 1) : float3(1, 0, 0));

            const float R = 1.f / primInfo.face.GetR();
            const float dist = distance(query.GetBc3(), float3(bc.x, bc.y, 1.f - bc.x - bc.y));
            if (dist > R * 0.5) {
                color = dist;
            }
        }
        else //  if (query.debugMode == DebugMode::None) 
        {
            color = fromYCoCgToRGB(DLCBuffer::Load2(sampleBuffer, index), hasClearTag);
        }

        return sampleBufferIndex;
    }

    Result QueryCache(Query query)
    {
        Result result;
        result.Init();

#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
        uint surfelInfoBufferSlot, surfelInfoBufferBaseOffset;
        {
            uint2 u2 = u_directLightingCacheIndirectionTable[query.instanceIndex].xy;
            surfelInfoBufferSlot = u2.x;
            surfelInfoBufferBaseOffset = u2.y;
        }
        DLCBufferType surfelInfo = u_directLightingCacheBuffer[NonUniformResourceIndex(surfelInfoBufferSlot)];
#else
        DLCBufferType surfelInfo = u_directLightingCacheBuffer[NonUniformResourceIndex(query.instanceIndex * 2 + 0)];
        uint surfelInfoBufferBaseOffset = 0;
#endif

        SurfelCache::Header header = SurfelCache::LoadHeader(surfelInfo, surfelInfoBufferBaseOffset);

        if ((TileMode)header.format == TileMode::MeshColors) {

            MeshColors::MeshColorPrimInfo primInfo = MeshColors::LoadMeshColorPrimInfo(surfelInfo, surfelInfoBufferBaseOffset, query.primitiveIndex);

            const float3 bc3 = float3(query.bc.x, query.bc.y, 1.f - query.bc.x - query.bc.y);
            const float invR = 1.f / (primInfo.face.GetR());
            float3 w = frac(primInfo.face.GetR() * bc3);
            const float w_sum = w.x + w.y + w.z;

            float2 bc = uint2(query.bc * primInfo.face.GetR()) / float(primInfo.face.GetR());

            float2 bc0, bc1, bc2;
            if (w_sum > 0.99 && w_sum < 1.01) {
                bc0 = bc + float2(invR, 0);
                bc1 = bc + float2(0, invR);
                bc2 = bc + float2(0, 0);
            }
            else if (w_sum > 1.99 && w_sum < 2.01) {
                w = 1.f - w;
                bc0 = bc + float2(0, invR);
                bc1 = bc + float2(invR, 0);
                bc2 = bc + float2(invR, invR);
            }
            else {
                result.tileData = float3(1, 0, 0);
                result.bufferIndex = MeshColors::kInvalidOffset;
#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
                retult.buffer = 0; // null view
#else
                result.buffer = query.instanceIndex * 2 + 1;
#endif
                return result;
            }

            if (query.bilinearSampling)
            {
                float3 tileData0;
                SampleMeshColor(primInfo, query, bc3, bc0, result.bufferIndex, tileData0, result.hasClearTag);

                float3 tileData1;
                SampleMeshColor(primInfo, query, bc3, bc1, result.bufferIndex, tileData1, result.hasClearTag);

                float3 tileData2;
                result.buffer = SampleMeshColor(primInfo, query, bc3, bc2, result.bufferIndex, tileData2, result.hasClearTag);

                result.tileData = w.x * tileData0 + w.y * tileData1 + w.z * tileData2;
                return result;
            }
            else {
                bc = bc2;
                if (w.x > w.y && w.x > w.z) {
                    bc = bc0;
                }
                else if (w.y > w.x && w.y > w.z) {
                    bc = bc1;
                }

                result.buffer = SampleMeshColor(primInfo, query, bc3, bc, result.bufferIndex, result.tileData, result.hasClearTag);
                return result;
            }
        }
        else {
            result.buffer = SampleTileCache(query, result.bufferIndex, result.tileData, result.hasClearTag);
            return result;
        }
    }

    // Write to light cache.
    void Store(DLCBufferIndex bufferIndex, uint index, float3 tileData, bool hasClearTag)
    {
        if (index == MeshColors::kInvalidOffset)
            return;

        uint2 data = fromRGBToYCoCg(tileData, hasClearTag);

        // NV HW store data atomically by 32bit, so, under the race condition, it would get false intencity but no false chroma.
        DLCBuffer::Store2(u_directLightingCacheBuffer[NonUniformResourceIndex(bufferIndex)], index + 0, data);
    }

    void StoreFaceMeshColors(MeshColors::MeshColorPrimInfo primInfo, uint instanceIndex, uint primitiveIndex, uint2 data) {
        // Currently not supported.
    }

    void StoreFaceTileCache(uint instanceIndex, uint primitiveIndex, uint2 data) {
#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
        uint indexBufferSlot, indexBufferBaseOffset, DLCBufferSlot, DLCBufferBaseOffset;
        {
            uint4 u4 = u_directLightingCacheIndirectionTable[instanceIndex];
            indexBufferSlot = u4.x;
            indexBufferBaseOffset = u4.y;
            DLCBufferSlot = u4.z;
            DLCBufferBaseOffset = u4.w;
        }
        DLCBufferType tileIndexBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(indexBufferSlot)];
        DLCBufferIndex tileBufferIndex = DLCBufferSlot;
        DLCBufferType tileBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(tileBufferIndex)];
#else
        DLCBufferType tileIndexBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(instanceIndex * 2 + 0)];
        DLCBufferIndex tileBufferIndex = instanceIndex * 2 + 1;
        DLCBufferType tileBuffer = u_directLightingCacheBuffer[NonUniformResourceIndex(tileBufferIndex)];
        uint indexBufferBaseOffset = 0;
        uint DLCBufferBaseOffset = 0;
#endif

        // if a valid offset for tileIndexBuffer is assigned, load index buffer and calc tileIndex.
        uint baseOffset;
        uint2 tileResolutions;

        TileCache::LoadTileCacheEntry(tileIndexBuffer, indexBufferBaseOffset, primitiveIndex, baseOffset, tileResolutions);

        if (tileResolutions.x == 0 && tileResolutions.y == 0) {
            // if a valid tileIndexBuffer is assigned, packedTielResolutions will always be nonzero value (e.g. at least 1x1 tile should be assigned).
            // it means tileIndexBuffer is unbound, and it should be direct tile mapping mode.
            // There is a case that tileBuffer is also unbound, but D3D12 safely access null UAV which is set in a desc table entry.
            uint tileIndex = primitiveIndex;

            tileIndex *= 2;
            tileIndex += DLCBufferBaseOffset;

            DLCBuffer::Store2(tileBuffer, tileIndex + 0, data);
        }
        else
        {
            // We could attempt to address the tile resolution assymetry by allocating a warp per primitive and thus (partially) parallelize this loop.
            for (uint i = 0; i < min(tileResolutions.x, 16u); ++i)
            {
                for (uint j = 0; j < min(tileResolutions.y, 16u); ++j)
                {
                    // bc.xy give weights for 2nd and 3rd vertices.
                    uint uIdx = i;
                    uint vIdx = j;

                    uint tileIndex = baseOffset + vIdx * tileResolutions.x + uIdx;
                    tileIndex *= 2;
                    tileIndex += DLCBufferBaseOffset;

                    DLCBuffer::Store2(tileBuffer, tileIndex + 0, data);
                }
            }
        }
    }

    // Write to every light cache element on a primitive.
    // Can be expensive, possibly.
    void Store(uint instanceIndex, uint primitiveIndex, float3 tileData)
    {
        uint2 data = fromRGBToYCoCg(tileData, /*hasClearTag*/ false);

#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
        uint surfelInfoBufferSlot, surfelInfoBufferBaseOffset;
        {
            uint2 u2 = u_directLightingCacheIndirectionTable[NonUniformResourceIndex(instanceIndex)].xy;
            surfelInfoBufferSlot = u2.x;
            surfelInfoBufferBaseOffset = u2.y;
        }
        DLCBufferType surfelInfo = u_directLightingCacheBuffer[NonUniformResourceIndex(surfelInfoBufferSlot)];
#else
        DLCBufferType surfelInfo = u_directLightingCacheBuffer[NonUniformResourceIndex(instanceIndex * 2 + 0)];
        uint surfelInfoBufferBaseOffset = 0;
#endif

        SurfelCache::Header header = SurfelCache::LoadHeader(surfelInfo, surfelInfoBufferBaseOffset);

        if ((TileMode)header.format == TileMode::MeshColors) {
            MeshColors::MeshColorPrimInfo primInfo = MeshColors::LoadMeshColorPrimInfo(surfelInfo, surfelInfoBufferBaseOffset, primitiveIndex);
            StoreFaceMeshColors(primInfo, instanceIndex, primitiveIndex, data);
        }
        else
        {
            StoreFaceTileCache(instanceIndex, primitiveIndex, data);
        }
    }
}