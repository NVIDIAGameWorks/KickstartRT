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

// Shared declarations and utilities
#include "Shared/Shared.hlsli"

// bindings and functions for direct lighting cache
#include "DirectLightingCache.hlsli"

// bindings for reflection_rt
#include "Reflection_Bindings.hlsli"

#include "rng.hlsl"
#include "brdf.h"

#define FLT_MAX 3.402823466e+38F

#define GBUFFER_DEPTH_TEXTURE_AVAILABLE
#define GBUFFER_NORMAL_TEXTURE_AVAILABLE
#include "GBuffer_functions.hlsli"
#undef GBUFFER_NORMAL_TEXTURE_AVAILABLE
#undef GBUFFER_DEPTH_TEXTURE_AVAILABLE

#define ENABLE_CTA_SWIZZLING
#include "CTA_Swizzle.hlsli"
#undef ENABLE_CTA_SWIZZLING

struct Payload
{
	float HitT : HIT_T;
	float3 Col : COLOR;
};

void hit(uint primitiveIndex, float2 barycentrics, uint instanceIndex, float rayTCurrent, inout Payload payload)
{
	LightCache::Query query;
	query.Init();
	query.instanceIndex			= instanceIndex;
	query.primitiveIndex		= primitiveIndex;
	query.bc					= barycentrics;
	query.instanceIndex			= instanceIndex;
	query.bilinearSampling		= CB.m_enableBilinearSample;
	if ((Debug)CB.m_outputType == Debug::RandomTileColor_PrimaryRays)
		query.debugMode	= LightCache::DebugMode::RandomColor;
	else if ((Debug)CB.m_outputType == Debug::MeshColorClassification_PrimaryRays)
		query.debugMode = LightCache::DebugMode::MeshColorClassification;

	LightCache::Result res = LightCache::QueryCache(query);

	payload.HitT = rayTCurrent;
	payload.Col = res.tileData;

	if ((Debug)CB.m_outputType == Debug::HitT_PrimaryRays) {
		float t = rayTCurrent;
		float lt = log(t);
		payload.Col = float3(t, lt, lt / log(10));
	}
	else if ((Debug)CB.m_outputType == Debug::Barycentrics_PrimaryRays) {
		payload.Col.r = barycentrics.r;
		payload.Col.g = barycentrics.g;
		payload.Col.b = (float)(primitiveIndex % 16) / 15.0;
	}
}

void rgs(uint2 LaunchIndex)
{
	uint2 LaunchDimensions = uint2(CB.m_Viewport_Width, CB.m_Viewport_Height);

	uint2 samplePos = ToSamplePos(LaunchIndex);

	bool isSurfacePosValid;
	bool isSurfaceNormalValid;

	float3 origin = CB.m_rayOrigin.xyz;
	float3 surfacePos = GetWorldPositionFromDepth(LaunchIndex, samplePos, isSurfacePosValid);
	float3 normal = GetNormal(samplePos, isSurfaceNormalValid);

	// surface validation and early out
	if ((isSurfacePosValid && isSurfaceNormalValid) == false) {
		// Provided g-buffer didn't have a valid surface.
		float4 col = float4(0, 0, 0, 0);

		// devbug code path shows invalidate reason by color.
		if (!isSurfacePosValid)
			col.x = 1.0;
		if (!isSurfaceNormalValid)
			col.y = 1.0;
		u_OutputTex[samplePos] = col;
		return;
	}

	// Setup the ray
	// Cast primary rays to see tiled lighting cache values.
	RayDesc ray;
	ray.Origin = origin.xyz;
	ray.Direction = normalize(surfacePos.xyz - origin.xyz);
	ray.TMin = 0.0;
	ray.TMax = FLT_MAX;

	// Trace the ray
	Payload payload;
	payload.HitT = -1;
	payload.Col = float3(0, 0, 0);

#if INLINE_RAY_TRACING
	{
		// Trace the ray
		RayQuery<RAY_FLAG_CULL_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
		rayQuery.TraceRayInline(t_SceneBVH,
			RAY_FLAG_NONE, // RAY_FLAG_CULL_BACK_FACING_TRIANGLES, // ray flags
			0xFF, // instanceInclusionMask
			ray);
		rayQuery.Proceed();

		// Process result
		if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
		{
			// Hit something
			hit(
				rayQuery.CommittedPrimitiveIndex(),
				rayQuery.CommittedTriangleBarycentrics(),
				rayQuery.CommittedInstanceID(),
				rayQuery.CommittedRayT(),
				payload);
		}
	}
#else
	{
		TraceRay(
			t_SceneBVH,
			RAY_FLAG_CULL_BACK_FACING_TRIANGLES, // ray flags
			0xFF, // instanceInclusionMask
			0, //RayContributionToHitGroupIndex
			0, //MultiplierForGeometryContributionToHitGroupIndex
			0, //MissShaderIndex
			ray,
			payload);
	}
#endif

	// 4th component should containt HitT of the secondary ray (for denoising)
	u_OutputTex[samplePos] = float4(payload.Col, payload.HitT);
}

#if INLINE_RAY_TRACING
// ---[ Compute Shader Version ]---
[numthreads(8, 16, 1)]
void main(
	uint2 groupIdx : SV_GroupID,
	uint2 globalIdx : SV_DispatchThreadID,
	uint2 threadIdx : SV_GroupThreadID)
{
	uint2 LaunchIndex = CTASwizzle_GetPixelPosition(groupIdx, threadIdx, globalIdx);

	if (LaunchIndex.x < CB.m_Viewport_Width || LaunchIndex.y < CB.m_Viewport_Height) {
		rgs(LaunchIndex);
	}
}
#else
// ---[ RT shader Version ]---
[shader("raygeneration")]
void RayGen()
{
	uint2 LaunchIndex = DispatchRaysIndex().xy;

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
}
#endif
