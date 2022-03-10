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

// bindings and functions for tiled lighting cache
#include "DirectLightingCache.hlsli"

// ---[ Structures ]---
struct CB_Clear
{
	uint	m_instanceIndex;
	uint    m_numberOfTiles;
	uint    m_resourceOffset;
	uint	m_pad_u1;

	float3  m_clearColor;
	float   m_pad_f0;
};

// ---[ Resources ]---
//[[vk::binding(0, 0)]]
KS_VK_BINDING(0, 0)
ConstantBuffer<CB_Clear> CB : register(b0);

[numthreads(64, 1, 1)]
void main(
	uint2 groupIdx : SV_GroupID,
	uint2 globalIdx : SV_DispatchThreadID,
	uint2 threadIdx : SV_GroupThreadID)
{
	uint sampleBufferSlot, sampleBufferBaseOffset;
#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
	{
		uint2 u2 = u_directLightingCacheIndirectionTable[CB.m_instanceIndex].zw;
		sampleBufferSlot = u2.x;
		sampleBufferBaseOffset = u2.y;
	}
#else
	sampleBufferSlot = CB.m_instanceIndex * 2 + CB.m_resourceOffset;
	sampleBufferBaseOffset = 0;
#endif

	DLCBufferType tileBuffer = u_directLightingCacheBuffer[sampleBufferSlot];

	uint2 clearColor = fromRGBToYCoCg(CB.m_clearColor, true /*SetClearTag*/);

	// fill 4 tiless per thread.
	uint tileOffset = globalIdx.x * 4;
	if (tileOffset < CB.m_numberOfTiles) {
		DLCBuffer::Store2(tileBuffer, sampleBufferBaseOffset + tileOffset * 2, clearColor);
	}
	tileOffset++;
	if (tileOffset < CB.m_numberOfTiles) {
		DLCBuffer::Store2(tileBuffer, sampleBufferBaseOffset + tileOffset * 2, clearColor);
	}
	tileOffset++;
	if (tileOffset < CB.m_numberOfTiles) {
		DLCBuffer::Store2(tileBuffer, sampleBufferBaseOffset + tileOffset * 2, clearColor);
	}
	tileOffset++;
	if (tileOffset < CB.m_numberOfTiles) {
		DLCBuffer::Store2(tileBuffer, sampleBufferBaseOffset + tileOffset * 2, clearColor);
	}
}
