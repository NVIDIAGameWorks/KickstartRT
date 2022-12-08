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

// bindings and functions for tiled lighting cache
#include "../Shared/Binding.hlsli"
#include "DirectLightingCache.hlsli"

// ---[ Structures ]---
struct CB_Injection
{
	uint		m_triangleCount;
	uint		m_targetInstanceIndex;
	uint		m_dstVertexBufferOffsetIdx; // Dest indices and vertices buffers are now unified. It needs the offset.
	uint		m_pad;

	float4x4	m_targetInstanceTransform;
};

struct Payload
{
    float HitT : HIT_T;
	float3 Col : Color;
};

// ---[ Resources ]---
//[[vk::binding(0, 1)]]
KS_VK_BINDING(0, 1)
RaytracingAccelerationStructure t_SceneBVH : register(t0, space1);

//[[vk::binding(0, 0)]]
KS_VK_BINDING(0, 0)
ConstantBuffer<CB_Injection> CB : register(b0);

//[[vk::binding(1, 0)]]
KS_VK_BINDING(1, 0)
RWBuffer<uint> u_index_vertexBuffer : register(u0);   // transformed vertex buffer. need to be defined as uint since it will be bound with direct lighting cache.

#include "Shared/Shared.hlsli"

uint GetVertexIndex(uint vIdx)
{
	return u_index_vertexBuffer[vIdx];
}

float3 GetVertexPosition(uint vIdx)
{
	uint ofs = CB.m_dstVertexBufferOffsetIdx + vIdx * 3;

	float3 vPos;
	vPos.x = asfloat(u_index_vertexBuffer[ofs + 0]);
	vPos.y = asfloat(u_index_vertexBuffer[ofs + 1]);
	vPos.z = asfloat(u_index_vertexBuffer[ofs + 2]);

	return vPos;
}

void GetFaceNormalAndCenter(uint primitiveIndex, out float3 outNormal, out float3 outCenter)
{
	const uint vIdx0 = GetVertexIndex(3 * primitiveIndex + 0);
	const uint vIdx1 = GetVertexIndex(3 * primitiveIndex + 1);
	const uint vIdx2 = GetVertexIndex(3 * primitiveIndex + 2);

	const float3 vPos0 = mul(float4(GetVertexPosition(vIdx0), 1), CB.m_targetInstanceTransform).xyz;
	const float3 vPos1 = mul(float4(GetVertexPosition(vIdx1), 1), CB.m_targetInstanceTransform).xyz;
	const float3 vPos2 = mul(float4(GetVertexPosition(vIdx2), 1), CB.m_targetInstanceTransform).xyz;

	const float3 v0 = vPos1 - vPos0;
	const float3 v1 = vPos2 - vPos0;

	outNormal = normalize(cross(v0, v1));
	outCenter = (vPos0 + vPos1 + vPos2) / 3.f;
}

// ---[ This corresponds to CHS or a function of Compute for inline raytracing. ]---
void hit(uint primitiveIndex, float2 barycentrics, uint instanceIndex, float rayTCurrent, inout Payload payload)
{
	payload.HitT = rayTCurrent;

	LightCache::Query query;
	query.Init();
	query.instanceIndex = instanceIndex;
	query.primitiveIndex = primitiveIndex;
	query.bc = barycentrics;
	query.instanceIndex = instanceIndex;
	query.bilinearSampling = true;

	LightCache::Result res = LightCache::QueryCache(query);

	payload.Col = res.tileData;
}

// ---[ This corresponds to RGS or Compute for inline raytracing. ]---
void rgs(uint LaunchThread)
{
	const uint primitiveIndex = LaunchThread;
	float3 normalWS;
	float3 faceCenterWS;
	GetFaceNormalAndCenter(primitiveIndex, normalWS, faceCenterWS);

	// Setup the ray

	float hitT = -1.f;
	float3 Col = float3(0.f, 0.f, 0.f);
	// Trace the ray
	[UNROLL]
	for (uint i = 0; i < 2; ++i)
	{
		RayDesc ray;
		ray.Origin = faceCenterWS;
		ray.Direction = i == 0 ? normalWS : -normalWS;
		ray.TMin = 0.f; // Self intersections are prevented via instance mask
		ray.TMax = 100.f; // :shrug:

		Payload payload;
		payload.HitT = -1.f;
		payload.Col = 0.f;

		uint RT_RayFlags = RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;
#if INLINE_RAY_TRACING
		// Trace the ray
		RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
		rayQuery.TraceRayInline(
			t_SceneBVH,
			RT_RayFlags, //ray flags
			(uint)InstancePropertyMask::LightTransferSource,
			ray);
		rayQuery.Proceed();

		// Process result
		if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			// Hit something, store lighting.
			hit(
				rayQuery.CommittedPrimitiveIndex(),
				rayQuery.CommittedTriangleBarycentrics(),
				rayQuery.CommittedInstanceID(),
				rayQuery.CommittedRayT(),
				payload);
		}
#else
		TraceRay(
			t_SceneBVH,
			RT_RayFlags, // ray flags
			(uint)InstancePropertyMask::LightTransferSource,
			0, //RayContributionToHitGroupIndex
			0, //MultiplierForGeometryContributionToHitGroupIndex
			0, //MissShaderIndex
			ray,
			payload);
#endif

		if (payload.HitT >= 0 && (hitT < 0 || hitT > payload.HitT))
		{
			hitT = payload.HitT;
			Col = payload.Col;
		}
	}

	if (hitT < 0.f)
		return;

	{ // Store in current instance

		LightCache::Store(
			CB.m_targetInstanceIndex,
			primitiveIndex,
			Col);
	}
}

#if INLINE_RAY_TRACING
// ---[ Compute Shader Version ]---
[numthreads(128, 1, 1)]
void main(
    uint2 groupIdx : SV_GroupID,
    uint2 globalIdx : SV_DispatchThreadID,
    uint2 threadIdx : SV_GroupThreadID)
{
	uint LaunchIndex = globalIdx.x;

    if (LaunchIndex.x < CB.m_triangleCount) {
        rgs(LaunchIndex);
    }
}
#else
// ---[ RT shader Version ]---
[shader("raygeneration")]
void RayGen()
{
    uint LaunchIndex = DispatchRaysIndex().x;

    rgs(LaunchIndex);
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attrib)
{
    hit(PrimitiveIndex(), attrib.barycentrics, InstanceIndex(), RayTCurrent(), payload);
}

[shader("miss")]
void Miss(inout Payload payload)
{
	payload.Col = float3(1.f, 0.f, 0.f);
}
#endif
