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

// Shared declarations and utilities
#include "Shared/Shared.hlsli"

// bindings and functions for direct lighting cache
#include "DirectLightingCache.hlsli"

// bindings for reflection_rt
#include "Reflection_Bindings.hlsli"

#include "rng.hlsl"
#include "brdf.h"

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
};

bool generateAORay(float2 u, float3 shadingNormal, float3 geometryNormal, OUT_PARAMETER(float3) rayDirection) {

	// Sample a direction within hemisphere (cosine weighted)
	float3 rayDirectionLocal = sampleHemisphere(u);

	// Transform sampled direction Llocal back to V vector space
	float4 qRotationFromZ = getRotationFromZAxis(shadingNormal);
	rayDirection = normalize(rotatePoint(qRotationFromZ, rayDirectionLocal));

	// Prevent tracing direction "under" the hemisphere (behind the triangle)
	if (dot(geometryNormal, rayDirection) <= 0.0f) return false;

	return true;
}

void hit(uint primitiveIndex, float2 barycentrics, uint instanceIndex, float rayTCurrent, inout Payload payload)
{
	payload.HitT = rayTCurrent;
}

void rgs(uint2 LaunchIndex)
{
	uint2 LaunchDimensions = uint2(CB.m_Viewport_Width, CB.m_Viewport_Height);

#if ENABLE_INPUT_MASK
	if (!t_InputMaskTex.Load(uint3(LaunchIndex, 0))) return;
#endif
	
	uint2 samplePos = ToSamplePos(LaunchIndex);

	float3 origin = CB.m_rayOrigin.xyz;

	bool isSurfacePosValid;
	bool isSurfaceNormalValid;
	bool isRayValid;

	// Modify samplePos and LaunchIndex based on setting of half-resolution rendering
#if ENABLE_HALF_RESOLUTION
	const uint2 inputLaunchIndex = getCheckerboardCoordinates(LaunchIndex, CB.m_invertHalfResCheckerboard);
	const uint2 inputSamplePos = ToSamplePos(inputLaunchIndex);
#else		
	const uint2 inputLaunchIndex = LaunchIndex;
	const uint2 inputSamplePos = samplePos;
#endif

	float3 surfacePos = GetWorldPositionFromDepth(inputLaunchIndex, inputSamplePos, isSurfacePosValid);
	float3 normal = GetNormal(inputSamplePos, isSurfaceNormalValid);

	// surface validation and early out
	if ((isSurfacePosValid && isSurfaceNormalValid) == false) {
		u_OutputTex[samplePos] = 0;
		return;
	}

	float distanceFromCamera = length(surfacePos.xyz - origin.xyz);
	float3 viewDir = (surfacePos.xyz - origin.xyz) / distanceFromCamera;

	float3 direction;
	{
		// Initialize random numbers generator
		RNGState rngState = initRNG(LaunchIndex, LaunchDimensions, CB.m_frameIndex, CB.m_randomNumberGeneratorType, CB.m_globalRandom_F, CB.m_globalRandom_U);

		float2 rndUV = randUV(rngState);

		// Do ambient occlusion
		isRayValid = generateAORay(rndUV, normal, normal, direction);
	}

	// ray validation and early out
	if (isRayValid == false) {
		u_OutputTex[samplePos] = 0;
		return;
	}

	// Setup the ray
	RayDesc ray;
	ray.Direction = direction;
	ray.Origin = offset_ray(surfacePos, normal - viewDir, distanceFromCamera);
	ray.TMin = 0.0;
	ray.TMax = CB.m_aoRadius;

	// Trace the ray
	Payload payload;
	payload.HitT = -1;

	uint RT_RayFlags = RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES;
#if INLINE_RAY_TRACING
	{
		// Trace the ray
		RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
		rayQuery.TraceRayInline(t_SceneBVH,
			RT_RayFlags, // ray flags
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
			RT_RayFlags, // ray flags
			0xFF, // instanceInclusionMask
			0, //RayContributionToHitGroupIndex
			0, //MultiplierForGeometryContributionToHitGroupIndex
			0, //MissShaderIndex
			ray,
			payload);
	}
#endif

	float ao = 1.0f;
	if (payload.HitT >= 0.0) {
		ao = saturate(payload.HitT / CB.m_aoRadius);
	}

	u_OutputTex[samplePos] = float4(ao, ao, ao, payload.HitT);
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

