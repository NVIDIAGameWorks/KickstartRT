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
    uint    m_Viewport_TopLeftX;
    uint    m_Viewport_TopLeftY;
    uint    m_Viewport_Width;
    uint    m_Viewport_Height;

    float   m_Viewport_MinDepth;
    float   m_Viewport_MaxDepth;
    uint	m_CTA_Swizzle_GroupDimension_X;
    uint	m_CTA_Swizzle_GroupDimension_Y;

    float3  m_rayOrigin;
    uint    m_depthType;

	float		m_averageWindow;
	uint32_t    m_pad0;
	float		m_subPixelJitterOffsetX;
	float		m_subPixelJitterOffsetY;

	uint32_t    m_strideX;
	uint32_t    m_strideY;
	uint32_t    m_strideOffsetX;
	uint32_t    m_strideOffsetY;

    float4x4	m_clipToViewMatrix;
    float4x4	m_viewToWorldMatrix;
};

struct Payload
{
    float HitT : HIT_T;
    float3 Col : COLOR;
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
Texture2D<float4> t_DepthTex : register(t0);

//[[vk::binding(2, 0)]]
KS_VK_BINDING(2, 0)
Texture2D<float4> t_LightingTex : register(t1);

#include "Shared/Shared.hlsli"

#define GBUFFER_DEPTH_TEXTURE_AVAILABLE
#include "GBuffer_functions.hlsli"
#undef GBUFFER_DEPTH_TEXTURE_AVAILABLE

#define ENABLE_CTA_SWIZZLING
#include "CTA_Swizzle.hlsli"
#undef ENABLE_CTA_SWIZZLING

// ---[ This corresponds to CHS or a function of Compute for inline raytracing. ]---
void hit(uint primitiveIndex, float2 barycentrics, uint instanceIndex, float rayTCurrent, inout Payload payload)
{
    if (abs(rayTCurrent - payload.HitT) / payload.HitT < 0.1) {

		LightCache::Query query;
		query.Init();
		query.instanceIndex			= instanceIndex;
		query.primitiveIndex		= primitiveIndex;
		query.bc					= barycentrics;
		query.instanceIndex			= instanceIndex;
		query.bilinearSampling		= false;

		LightCache::Result res = LightCache::QueryCache(query);

		float EMARatio = res.hasClearTag ? 1.f : 2.0 / (1.0 + CB.m_averageWindow);
		float3 newData = lerp(res.tileData, payload.Col, EMARatio);

		LightCache::Store(
			res.buffer,
			res.bufferIndex,
            newData,
			false /*hasClearTag*/);
    }
}

float2 GetJitteredLaunchIndex(uint2 samplePos)
{
	return samplePos + uint2(CB.m_subPixelJitterOffsetX, CB.m_subPixelJitterOffsetY);
}

float3 GetLightInjectionValueForSamplePos(uint2 samplePos)
{
	return t_LightingTex[samplePos].xyz;
}

uint2 GetTracePixelFromSamplePos(uint2 samplePos)
{
	//return samplePos * int2(CB.m_strideX, CB.m_strideY) + int2(CB.m_strideOffsetX, CB.m_strideOffsetY);
	// Make sure we're wrapping samples when trying to sample outside of the screen.

	const int2 viewport			= int2(CB.m_Viewport_Width, CB.m_Viewport_Height);

	const int2 AABB_start		= samplePos * int2(CB.m_strideX, CB.m_strideY);
	const int2 AABB_end			= (samplePos + 1) * int2(CB.m_strideX, CB.m_strideY);
	const int2 AABB_end_clamped	= min(AABB_end, viewport);

	const int2 offsetRect				= (AABB_end_clamped - AABB_start);
	const int2 strideOffsetWrapped		= int2(CB.m_strideOffsetX, CB.m_strideOffsetY) % offsetRect;

	uint2 tracePixel					= AABB_start + strideOffsetWrapped;
	return tracePixel;
}

// ---[ This corresponds to RGS or Compute for inline raytracing. ]---
void rgs(uint2 LaunchThread)
{
	const uint2 samplePos = ToSamplePos(LaunchThread);

	// Upscale pixel according to the sub-sampling options.
	uint2 tracePixel					= GetTracePixelFromSamplePos(samplePos);

	const float2 jitteredLaunchIndex	= GetJitteredLaunchIndex(tracePixel);
	const float3 color					= GetLightInjectionValueForSamplePos(tracePixel);

	float4 origin = float4(CB.m_rayOrigin.xyz, 1.0);

	bool   depthIsValid;
	float3 destination = GetWorldPositionFromDepth(jitteredLaunchIndex, tracePixel, depthIsValid);

	float3 viewDir = destination.xyz - origin.xyz;
	float  lengthFromViewOrigin = length(viewDir);
	float3 direction = viewDir / lengthFromViewOrigin;

	// Setup the ray
	RayDesc ray;
	ray.Origin = origin.xyz;
	ray.Direction = direction;
	ray.TMin = lengthFromViewOrigin * 0.98;
	ray.TMax = lengthFromViewOrigin * 1.02;

	// Trace the ray
	Payload payload;
	payload.HitT = lengthFromViewOrigin;
	payload.Col = color;

	if ((!depthIsValid) || any(isnan(payload.Col)) || any(isinf(payload.Col))) {
		// depth texture holds an invalid value.
		// lighting texture holds nan or inf.
	}
	else {
		uint RT_RayFlags = RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;
#if INLINE_RAY_TRACING
		// Trace the ray
		RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
		rayQuery.TraceRayInline(
			t_SceneBVH,
			RT_RayFlags, //ray flags
			(uint)InstancePropertyMask::DirectLightInjectionTarget,
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
			(uint)InstancePropertyMask::DirectLightInjectionTarget,
			0, //RayContributionToHitGroupIndex
			0, //MultiplierForGeometryContributionToHitGroupIndex
			0, //MissShaderIndex
			ray,
			payload);
#endif
	}
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
