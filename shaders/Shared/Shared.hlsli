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

#include "../../src/common/SDKDefines.h"

#ifndef __SHARED_SHARED_HLSL__
#define __SHARED_SHARED_HLSL__

enum class InstancePropertyMask
{
    // Instances with this property may get 
    DirectLightInjectionTarget  = 1u << 0u,

    // Instances with this mask may be queried during light transfer operation.
    LightTransferSource          = 1u << 1u,

    // These instances may be invisible during tracing operations
    Visible                      = 1u << 2u,
};

#if KICKSTARTRT_USE_BYTEADDRESSBUFFER_FOR_DLC
typedef RWByteAddressBuffer DLCBufferType;
typedef uint DLCBufferIndex;

namespace DLCBuffer {
    uint Load(DLCBufferType tileBuffer, uint index)
    {
        return tileBuffer.Load(index * 4);
    }
    uint2 Load2(DLCBufferType tileBuffer, uint index)
    {
        return tileBuffer.Load2(index * 4);
    }
    void Store(DLCBufferType tileBuffer, uint index, uint value)
    {
        return tileBuffer.Store(index * 4, value);
    }
    void Store2(DLCBufferType tileBuffer, uint index, uint2 value)
    {
        return tileBuffer.Store2(index * 4, value);
    }
};

#else
typedef RWBuffer<uint> DLCBufferType;
typedef uint DLCBufferIndex;

namespace DLCBuffer {
    uint Load(DLCBufferType tileBuffer, uint index)
    {
        return tileBuffer[index];
    }
    uint2 Load2(DLCBufferType tileBuffer, uint index)
    {
        return uint2(tileBuffer[index], tileBuffer[index + 1]);
    }
    void Store(DLCBufferType tileBuffer, uint index, uint value)
    {
        tileBuffer[index] = value;
    }
    void Store2(DLCBufferType tileBuffer, uint index, uint2 value)
    {
        tileBuffer[index + 0] = value.x;
        tileBuffer[index + 1] = value.y;
    }
};
#endif

enum class DepthType : uint {
	RGB_WorldSpace,
	R_ClipSpace
};

enum class NormalType : uint {
	RGB_Vector = 0,
	RG_BA_Octahedron = 1
};

enum class EnvMapType : uint {
	Latitude_Longitude = 0,
};

enum class LightType : uint {
	Directional = 0,
	Spot = 1,
	Point = 2,
};

enum class TileMode : uint {
	TileCache,
	MeshColors
};

enum class Debug : uint {
	DirectLightingCache_PrimaryRays = 100,
	RandomTileColor_PrimaryRays = 101,
    MeshColorClassification_PrimaryRays = 102,
	HitT_PrimaryRays = 103,
	Barycentrics_PrimaryRays = 104,
};


float2 oct_wrap(float2 v)
{
	return (1.f - abs(v.yx)) * (v.xy >= 0.f ? 1.f : -1.f);
}

float3 oct_to_ndir_snorm(float2 p)
{
	float3 n = float3(p.xy, 1.0 - abs(p.x) - abs(p.y));
	n.xy = (n.z < 0.0) ? oct_wrap(n.xy) : n.xy;
	return normalize(n);
}


/// Unrolled version of murmur hash that takes N integers as input (up to 8)
uint murmur_32_scramble(uint k)
{
    k *= 0xcc9e2d51;
    k = (k << 15) | (k >> 17);
    k *= 0x1b873593;
    return k;
};

uint murmur_32_process(uint k, uint h)
{
    h ^= murmur_32_scramble(k);
    h = (h << 13) | (h >> 19);
    h = h * 5 + 0xe6546b64;
    return h;
};

uint murmurUint2(
    uint key0,
    uint key1,
    uint SEED)
{
    uint h = SEED;
    h = murmur_32_process(key0, h);
    h = murmur_32_process(key1, h);

    // A swap is *not* necessary here because the preceding loop already
    // places the low bytes in the low places according to whatever endianness
    // we use. Swaps only apply when the memory is copied in a chunk.
    h ^= murmur_32_scramble(0);
    /* Finalize. */
    h ^= 24; // len = 6 * sizeof(float)
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
};

uint murmurUint(
    uint key0,
    uint SEED)
{
    uint h = SEED;
    h = murmur_32_process(key0, h);

    // A swap is *not* necessary here because the preceding loop already
    // places the low bytes in the low places according to whatever endianness
    // we use. Swaps only apply when the memory is copied in a chunk.
    h ^= murmur_32_scramble(0);
    /* Finalize. */
    h ^= 24; // len = 6 * sizeof(float)
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
};

uint murmurUint6(
    uint key0,
    uint key1,
    uint key2,
    uint key3,
    uint key4,
    uint key5,
    uint SEED)
{
    uint h = SEED;
    h = murmur_32_process(key0, h);
    h = murmur_32_process(key1, h);
    h = murmur_32_process(key2, h);
    h = murmur_32_process(key3, h);
    h = murmur_32_process(key4, h);
    h = murmur_32_process(key5, h);

    // A swap is *not* necessary here because the preceding loop already
    // places the low bytes in the low places according to whatever endianness
    // we use. Swaps only apply when the memory is copied in a chunk.
    h ^= murmur_32_scramble(0);
    /* Finalize. */
    h ^= 24; // len = 6 * sizeof(float)
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
};

// Converts unsigned integer into float int range <0; 1) by using 23 most significant bits for mantissa
float uintToFloat(uint x) {
    return asfloat(0x3f800000 | (x >> 9)) - 1.0f;
}


// 32-bit Xorshift random number generator
uint xorshift(inout uint rngState)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

float3 GetWorldNormal(
	float4 rawNormal,	// Loaded from tex
	NormalType normalType,
	float2 normalNormalizationFactor,
	float4 normalChMask1, 
	float4 normalChMask2,
	float3x3 normalToWorldMatrix,
	out bool isValid)
{
	float3 normal = float3(0, 0, 0);
	isValid = false;

	// do normalization if needed. (XYZW) * 2 + 1 or (XYZW) * 1 + 0
	rawNormal = rawNormal * normalNormalizationFactor.x + normalNormalizationFactor.y;

	if (normalType == NormalType::RGB_Vector) {
		// nothing to do.
		normal = rawNormal.xyz;

		float l = length(normal);
		normal /= l;
		if (l > 0.95 && l < 1.05)
			isValid = true;
	}
	if (normalType == NormalType::RG_BA_Octahedron) {
		float2 xy = float2(dot(rawNormal, normalChMask1), dot(rawNormal, normalChMask2));
		normal = oct_to_ndir_snorm(xy);
		// There is no invalid region in octahedral mapping so all pixel should be valid.
		isValid = true;
	}

	// apply normal transformation matrix here
	return mul(normal, normalToWorldMatrix);
}

float RemapRoughness(
	float roughness,
	float roughnessMultiplier, 
	float minRoughness,
	float maxRoughness) {
	return clamp((roughness * roughnessMultiplier) + minRoughness, minRoughness, maxRoughness);
}

float LoadRoughness(
	Texture2D<float4> t_Roughness, 
	uint2 pos, 
	float4 roughnessMask) {
	return dot(t_Roughness.Load(uint3(pos, 0)).xyzw, roughnessMask);
}

float GetViewZ(
	float4 rawDepthOrWorldPos,
	DepthType depthType,
	float2 depthScale,			// Scale factors (If depthFormat is clip space)
	float4x4 worldToViewMatrix,
	float4x4 clipToViewMatrix) 
{
	if (depthType == DepthType::R_ClipSpace) {
		float clipZ = rawDepthOrWorldPos.x;
		clipZ = ((depthScale.x - depthScale.y) * clipZ) + depthScale.y;
		float4 clipPos = float4(0, 0, clipZ, 1.0);
		float4 viewPos = mul(clipPos, clipToViewMatrix);
		return viewPos.z / viewPos.w;
	}
	else if (depthType == DepthType::RGB_WorldSpace) {
		const float3 worldPos = rawDepthOrWorldPos.xyz;
		return mul(float4(worldPos, 1), worldToViewMatrix).z;
	}
	else {
		return 0.f;
	}
}

namespace SurfelCache
{
    static const uint kHeaderDWSize = 8u;

    // Currently only stores the type of 
    // surface cache.
    // e.g MeshColors or TileCahe.
    struct Header {
        uint format;
    };

    Header LoadHeader(DLCBufferType buffer, uint baseOffset) {
        Header header;
        header.format = DLCBuffer::Load(buffer, baseOffset + 0);
        return header;
    }

    void StoreHeader(DLCBufferType buffer, uint baseOffset, Header header) {
        DLCBuffer::Store(buffer, baseOffset + 0, header.format);
    }
}

namespace TileCache {
    static const uint kTileCacheEntryStartOffset = SurfelCache::kHeaderDWSize;

    void StoreTileCacheEntry(DLCBufferType buffer, uint baseOffset, uint primIdx, uint tileOffset, uint2 tileResolutions) {
        uint ofs = baseOffset + kTileCacheEntryStartOffset + primIdx * 2;
        DLCBuffer::Store2(buffer, ofs + 0, uint2(tileOffset, tileResolutions.y << 16 | tileResolutions.x));
    }

    void LoadTileCacheEntry(DLCBufferType buffer, uint baseOffset, uint primIdx, out uint tileOffset, out uint2 tileResolutions) {
        uint ofs = baseOffset + kTileCacheEntryStartOffset + primIdx * 2;
        uint2 u2 = DLCBuffer::Load2(buffer, ofs + 0);
        tileOffset = u2.x;
        uint packedTileResolutions = u2.y;
        tileResolutions = uint2(packedTileResolutions & 0xFFFF, packedTileResolutions >> 16);
    }
}

namespace MeshColors
{
    static const uint kMeshColorEntryStride = 8u;
    static const uint kMeshColorEntryStartOffset = SurfelCache::kHeaderDWSize;

    static const uint kInvalidOffset = (0xFFFFFFFFu >> 5u);

    struct FaceColor {
        // This is the faceColorBuffer offset to read or write the color data.
        uint offset;

        // The resolution for this edge.
        // Important properties that makes mixing face resolutions possible and edge free:
        // 1. R is always a power of 2. For this reason we store log2(R).
        // 2. R is _guaranteed_ to be less than or equal to the R of the FaceColor.
        uint log2R;
        uint GetR() { return 1u << log2R; }
    };

    struct EdgeColor {
        // This is the offst to the edgeColorBuffer
        uint offset;

        // The resolution for this edge.
        // Important properties that makes mixing face resolutions possible and edge free:
        // 1. R is always a power of 2. For this reason we store log2(R).
        // 2. R is _guaranteed_ to be less than or equal to the R of the FaceColor.
        uint log2R;
        uint GetR() { return 1u << log2R; }

        // The edge orientation from the primitives perspective.
        // This is important to make edge colors consistent between faces.
        bool isReversed;

        bool IsInvalid() {
            // We don't really expect this to happen. Mainly for debug purposes.
            return offset == kInvalidOffset;
        }
    };

    // This Will be resolved to the EdgeColor in a second compute pass.
    // What can't be determined in the first pass is the:
    // 1. min(R) of the all primitives sharing the edge
    // 2. offset - the allocation order can't be guaranteed.
    struct UnresolvedEdgeColor {
        // This is the index of this edge to the hashTable
        uint hashTableIndex;

        // The edge orientation from the primitives perspective.
        // This is important to make edge colors consistent between faces.
        bool isReversed;
    };

    struct VertexColor {
        // This is the offst to the vertex color data. 
        // Normally this is just a copy of the index buffer. 
        uint offset;
    };

    // A single LoadMeshColorPrimInfo is stored per primitive, 
    // and is curently occupying to 8DW per entry.
    // (It can be more agressively compressed if needed...)
    struct MeshColorPrimInfo {
        FaceColor   face;
        EdgeColor   edges[3];
        VertexColor vertex[3];
    };

    FaceColor FaceUnpack(uint packed) {
        FaceColor faceColor;
        faceColor.offset    = packed >> 4u;
        faceColor.log2R     = 0x0000000F & (packed);
        return faceColor;
    }
    uint FacePack(FaceColor faceColor) {
        uint packed = 0;
        packed |= faceColor.offset << 4u;
        packed |= 0x0000000F & faceColor.log2R;
        return packed;
    }

    EdgeColor EdgeUnpack(uint packed) {
        EdgeColor edgeColor;
        edgeColor.offset = packed >> 5u;
        edgeColor.log2R = 0x0000000F & (packed >> 1u);
        edgeColor.isReversed = 1u & packed;
        return edgeColor;
    }
    uint EdgePack(EdgeColor edge) {
        uint packed = 0;
        packed |= edge.offset << 5u;
        packed |= (0x0000000F & edge.log2R) << 1u;
        packed |= 0x1 & edge.isReversed;
        return packed;
    }

    UnresolvedEdgeColor UnresolvedEdgeUnpack(uint packed) {
        UnresolvedEdgeColor edge;
        edge.hashTableIndex = packed >> 1u;
        edge.isReversed = 1u & packed;
        return edge;
    }
    uint UnresolvedEdgePack(UnresolvedEdgeColor edge) {
        uint packed = 0;
        packed |= edge.hashTableIndex << 1u;
        packed |= 0x1 & edge.isReversed;
        return packed;
    }

    uint VertexPack(VertexColor vertex) {
        return vertex.offset;
    }
    VertexColor VertexUnpack(uint packed) {
        VertexColor vertex;
        vertex.offset = packed;
        return vertex;
    }

    uint ComputeSubTriID(uint2 ij, const int N)
    {
        return (N - ij.x) * ((N - ij.x) + 1) / 2 + ij.y;
    }

    uint GetSlotIndexForEdge(uint primitiveIndex, uint edgeIndex) {
        uint baseOffset = kMeshColorEntryStride * primitiveIndex + 1;// +1 [face|edge + edgeIndex |v0|v1|v2].
        return baseOffset + edgeIndex;
    }

    uint GetHalfEdgeBufferLocation(uint halfEdgeId) {
        uint primitiveIndex = halfEdgeId / 3;
        uint edgeIndex = halfEdgeId % 3;
        return GetSlotIndexForEdge(primitiveIndex, edgeIndex);
    }

    uint GetFaceSlotBufferLocation(uint primitiveIndex) {
        return kMeshColorEntryStride * primitiveIndex;
    }

    uint GetVertexSlotBufferLocation(uint primitiveIndex, uint vertexIndex) {
        return kMeshColorEntryStride * primitiveIndex + 4 + vertexIndex;  // +4 [face|edge0|edge1|edge2|v + vertexIndex].
    }

    void Store(DLCBufferType buffer, uint baseOffset, uint slotIndex, uint data) {
        DLCBuffer::Store(buffer, baseOffset + kMeshColorEntryStartOffset + slotIndex, data);
    }

    uint Load(DLCBufferType rwbuffer, uint baseOffset, uint slotIndex) {
        return DLCBuffer::Load(rwbuffer, baseOffset + kMeshColorEntryStartOffset + slotIndex);
    }

    MeshColorPrimInfo LoadMeshColorPrimInfo(DLCBufferType meshColorHeaderBuffer, uint baseOffset, uint primitiveIndex) {
        uint data0 = DLCBuffer::Load(meshColorHeaderBuffer, baseOffset + kMeshColorEntryStartOffset + kMeshColorEntryStride * primitiveIndex + 0);
        uint data1 = DLCBuffer::Load(meshColorHeaderBuffer, baseOffset + kMeshColorEntryStartOffset + kMeshColorEntryStride * primitiveIndex + 1);
        uint data2 = DLCBuffer::Load(meshColorHeaderBuffer, baseOffset + kMeshColorEntryStartOffset + kMeshColorEntryStride * primitiveIndex + 2);
        uint data3 = DLCBuffer::Load(meshColorHeaderBuffer, baseOffset + kMeshColorEntryStartOffset + kMeshColorEntryStride * primitiveIndex + 3);
        uint data4 = DLCBuffer::Load(meshColorHeaderBuffer, baseOffset + kMeshColorEntryStartOffset + kMeshColorEntryStride * primitiveIndex + 4);
        uint data5 = DLCBuffer::Load(meshColorHeaderBuffer, baseOffset + kMeshColorEntryStartOffset + kMeshColorEntryStride * primitiveIndex + 5);
        uint data6 = DLCBuffer::Load(meshColorHeaderBuffer, baseOffset + kMeshColorEntryStartOffset + kMeshColorEntryStride * primitiveIndex + 6);

        MeshColorPrimInfo header;
        header.face         = FaceUnpack(data0);
        header.edges[0]     = EdgeUnpack(data1);
        header.edges[1]     = EdgeUnpack(data2);
        header.edges[2]     = EdgeUnpack(data3);
        header.vertex[0]    = VertexUnpack(data4);
        header.vertex[1]    = VertexUnpack(data5);
        header.vertex[2]    = VertexUnpack(data6);
        
        return header;
    }
}
#endif // __SHARED_SHARED_HLSL__
