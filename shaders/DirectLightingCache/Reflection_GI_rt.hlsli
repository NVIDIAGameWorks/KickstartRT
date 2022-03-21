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

// Optionally select Frostbite's normalized version of disney diffuse BRDF before including brdf.h
#if USE_NORMALIZED_DIFFUSE
#define DIFFUSE_BRDF FROSTBITE
#endif

#include "rng.hlsl"
#include "brdf.h"

#define FLT_MAX 3.402823466e+38F

struct Payload
{
    float HitT : HIT_T;
    float3 Col : COLOR;
};

#define GBUFFER_DEPTH_TEXTURE_AVAILABLE
#define GBUFFER_NORMAL_TEXTURE_AVAILABLE
#include "GBuffer_functions.hlsli"
#undef GBUFFER_NORMAL_TEXTURE_AVAILABLE
#undef GBUFFER_DEPTH_TEXTURE_AVAILABLE

#define ENABLE_CTA_SWIZZLING
#include "CTA_Swizzle.hlsli"
#undef ENABLE_CTA_SWIZZLING

float2 dir_to_latlong_map(float3 dir)
{
	const float M_1_PI = 0.318309886183790671538; // 1/pi
	const float M_1_2PI = 0.159154943091895335769; // 1/2pi

	float3 p = normalize(dir);
	float2 uv;
	uv.x = atan2(p.x, -p.z) * M_1_2PI + 0.5f;
	uv.y = acos(p.y) * M_1_PI;
	return uv;
}

// Approximates the integral over full hemisphere for the microfacet specular BRDF with GG-X distribution
// Source: "Accurate Real-Time Specular Reflections with Radiance Caching" in Ray Tracing Gems by Shirley et al.
float3 approximateGGXIntegral(float3 specularF0, float alpha, float NdotV)
{
	const float2x2 A = float2x2(
		0.99044f, -1.28514f,
		1.29678f, -0.755907f
		);

	const float3x3 B = float3x3(
		1.0f, 2.92338f, 59.4188f,
		20.3225f, -27.0302f, 222.592f,
		121.563f, 626.13f, 316.627f
		);

	const float2x2 C = float2x2(
		0.0365463f, 3.32707f,
		9.0632f, -9.04756f
		);

	const float3x3 D = float3x3(
		1.0f, 3.59685f, -1.36772f,
		9.04401f, -16.3174f, 9.22949f,
		5.56589f, 19.7886f, -20.2123f
		);

	const float alpha2 = alpha * alpha;
	const float alpha3 = alpha * alpha2;
	const float NdotV2 = NdotV * NdotV;
	const float NdotV3 = NdotV * NdotV2;

	const float E = dot(mul(A, float2(1.0f, NdotV)), float2(1.0f, alpha));
	const float F = dot(mul(B, float3(1.0f, NdotV, NdotV3)), float3(1.0f, alpha, alpha3));

	const float G = dot(mul(C, float2(1.0f, NdotV)), float2(1.0f, alpha));
	const float H = dot(mul(D, float3(1.0f, NdotV2, NdotV3)), float3(1.0f, alpha, alpha3));

	// Turn the bias off for near-zero specular 
	const float biasModifier = saturate(dot(specularF0, float3(0.333333f, 0.333333f, 0.333333f)) * 50.0f);

	const float bias = max(0.0f, (E * rcp(F))) * biasModifier;
	const float scale = max(0.0f, (G * rcp(H)));

	return float3(bias, bias, bias) + float3(scale, scale, scale) * specularF0;
}

void hit(uint primitiveIndex, float2 barycentrics, uint instanceIndex, float rayTCurrent, inout Payload payload)
{
	uint tileIndex;
	float3 tileData;

	LightCache::Query query;
	query.Init();
	query.instanceIndex			= instanceIndex;
	query.primitiveIndex		= primitiveIndex;
	query.bc					= barycentrics;
	query.instanceIndex			= instanceIndex;
	query.bilinearSampling		= CB.m_enableBilinearSample;

	LightCache::Result res = LightCache::QueryCache(query);

	payload.HitT = rayTCurrent;
	payload.Col = res.tileData;
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
	if ((isSurfacePosValid & isSurfaceNormalValid) == false) {
		u_OutputTex[samplePos] = 0;
		return;
	}

	float distanceFromCamera = length(surfacePos.xyz - origin.xyz);
	float3 viewDir = (surfacePos.xyz - origin.xyz) / distanceFromCamera;

	float3 brdfWeight = float3(1, 1, 1);
	float3 direction;
	{
		// Initialize random numbers generator
		RNGState rngState = initRNG(LaunchIndex, LaunchDimensions, CB.m_frameIndex, CB.m_randomNumberGeneratorType, CB.m_globalRandom_F, CB.m_globalRandom_U);

		float2 rndUV = randUV(rngState);

		// Load material properties and vectors needed for rough reflections
		const float3 shadingNormal = normal;
		const float3 geometryNormal = normal;
		const float3 V = -viewDir;

		// Synthesize a diffuse material with white albedo
		// Because we don't pipe diffuse reflectance into this SDK, we use white albedo and will apply the correct one when composing the final image
		MaterialProperties diffuseMaterial;
		diffuseMaterial.baseColor = float3(1.0f, 1.0f, 1.0f);
		diffuseMaterial.metalness = 0.0f;
#if USE_NORMALIZED_DIFFUSE
#if ENABLE_ROUGHNESS_TEX
		diffuseMaterial.roughness = dot(t_RoughnessTex.Load(int3(inputSamplePos, 0)).xyzw, CB.m_roughnessMask) * CB.m_globalRoughness;
#else
		diffuseMaterial.roughness = CB.m_globalRoughness;
#endif
#endif
		
		// Sample cosine distribution distribution to generate reflecting ray direction
		isRayValid = evalIndirectCombinedBRDF(rndUV, shadingNormal, geometryNormal, V, diffuseMaterial, DIFFUSE_TYPE, direction, brdfWeight);
	}

	// Setup the ray
	RayDesc ray;
	ray.Direction = direction;
	ray.Origin = offset_ray(surfacePos, normal - viewDir, distanceFromCamera);
	ray.TMin = 0.0;
	ray.TMax = FLT_MAX;

	// ray validation and early out
	if (!isRayValid) {
		u_OutputTex[samplePos] = 0;
		return;
	}

	// Trace the ray
	Payload payload;
	payload.HitT = -1;
	payload.Col = float3(0, 0, 0);

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

#if ENABLE_ENV_MAP_TEX
	if (payload.HitT < 0.0) {
		// Miss
		float3 mapLocal = mul(direction, (float3x3)CB.m_worldToEnvMapMatrix);
		float2 uv = dir_to_latlong_map(mapLocal);
		payload.Col = t_EnvMapTex.SampleLevel(LinearClampSampler, uv, 0.f).xyz * CB.m_envMapIntensity;
	}
#endif

	// 4th component should containt HitT of the secondary ray (for denoising)
	u_OutputTex[samplePos] = float4(payload.Col * brdfWeight, payload.HitT);
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
