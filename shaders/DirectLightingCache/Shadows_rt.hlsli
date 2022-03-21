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

#if KickstartRT_SDK_WITH_NRD
#define COMPILER_DXC
#include "NRD.hlsli"
#endif

#define FLT_MAX 3.402823466e+38F

struct Payload
{
    float HitT : HIT_T;
    float3 Col : COLOR; // TODO: remove. Not needed for shadows.
};

#define GBUFFER_DEPTH_TEXTURE_AVAILABLE
#define GBUFFER_NORMAL_TEXTURE_AVAILABLE
#include "GBuffer_functions.hlsli"
#undef GBUFFER_NORMAL_TEXTURE_AVAILABLE
#undef GBUFFER_DEPTH_TEXTURE_AVAILABLE

#define ENABLE_CTA_SWIZZLING
#include "CTA_Swizzle.hlsli"
#undef ENABLE_CTA_SWIZZLING

#if ENABLE_ACCEPT_FIRST_HIT_AND_END_SEARCH
#define RAY_FLAGS (RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH)
#else
#define RAY_FLAGS (RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES)
#endif

static const float kHitTForInvalidRay	= 0.f;
static const float kHitTForMiss			= 65504.f;
static const uint  kMaxNumLights		= 128;

void hit(uint primitiveIndex, float2 barycentrics, uint instanceIndex, float rayTCurrent, inout Payload payload)
{
	payload.HitT = rayTCurrent;
}

void GetAngularExtent(CB_Light light, float3 origin, out float angularExtent, out float tanOfAngularExtent) {

	angularExtent = 0;
	tanOfAngularExtent = 0;

	if (light.m_type == LightType::Directional) {
		angularExtent = light.m_angularExtent;
		tanOfAngularExtent = light.m_tanOfAngularExtent;
	}
	else if (light.m_type == LightType::Spot || light.m_type == LightType::Point) {
		const float distToLight = max(length(light.m_pos - origin), 1e-6);
		angularExtent = 2 * atan(0.5 * light.m_radius / distToLight);
		tanOfAngularExtent = tan(angularExtent);
	}
}

float TraceShadowRay(CB_Light light, float3 origin, float3 normal, inout RNGState rngState) {

	RayDesc ray;
	if (light.m_type == LightType::Directional) {
		float3 dirVec = normalize(light.m_dirVec.xyz);
		const float NdotL = dot(normal, dirVec);

		bool isRayInvalid = NdotL <= 0;
		// ray invalid, early out
		if (isRayInvalid) {
			return kHitTForInvalidRay;
		}

		float angularExtent;
		float _unused;
		GetAngularExtent(light, origin, angularExtent, _unused);

		float3 rayDir = 0;
		{
			const float angularSize = angularExtent;
			const float2 randPhiTheta = randUV(rngState) * angularSize;

			float2 dirSpherical = light.m_dir.xy;
			const float theta = dirSpherical.x + randPhiTheta.x;
			const float phi = dirSpherical.y + randPhiTheta.y;

			float sin_theta;
			float cos_theta;
			sincos(theta, sin_theta, cos_theta);

			float sin_phi;
			float cos_phi;
			sincos(phi, sin_phi, cos_phi);

			rayDir = float3(cos_phi * sin_theta, sin_phi * sin_theta, cos_theta);
		}

		ray.Direction = rayDir;
		ray.Origin = origin;
		ray.TMin = 1e-6;
		ray.TMax = FLT_MAX;
	}
	else if (light.m_type == LightType::Spot) {
		// Generate uniformly distributed random points on a unit sphere
		const float2 uniformRandom = randUV(rngState);

		const float kPI = 3.14159265f;
		const float phi = 2 * kPI * uniformRandom.x;
		const float theta = acos(2 * uniformRandom.y - 1);

		float sin_theta;
		float cos_theta;
		sincos(theta, sin_theta, cos_theta);

		float sin_phi;
		float cos_phi;
		sincos(phi, sin_phi, cos_phi);

		const float3 randomUnitSphere = float3(cos_phi * sin_theta, sin_phi * sin_theta, cos_theta);

		const float3 randomLightPoint = light.m_pos + light.m_radius * randomUnitSphere;
		const float distToLight = length(randomLightPoint - origin);
		const float3 rayDir = normalize(randomLightPoint - origin);

		const float NdotL = dot(normal, rayDir);

		bool isRayInvalid = NdotL <= 0;
		// ray invalid, early out
		if (isRayInvalid) {
			return kHitTForInvalidRay;
		}

		// If kHitTForInvalidRay is returned here the penumbra will not be denoised.
		// The proper solution might be to have a hard cutoff that's not dependent on
		// the random origin, and let the application generate a fake penumbra between an 
		// outer and inner apex angle.
		const float DdotL = dot(light.m_dirVec, rayDir);
		bool bRayWillHit = DdotL >= light.m_cosApexAngle;
		// ray will miss for sure, early out
		if (bRayWillHit) {
			return distToLight;
		}

		ray.Direction = rayDir;
		ray.Origin = origin;
		ray.TMin = 1e-5;
		ray.TMax = distToLight;
	}
	else if (light.m_type == LightType::Point) {
		// Generate uniformly distributed random points on a unit sphere
		const float2 uniformRandom = randUV(rngState);

		const float kPI = 3.14159265f;
		const float phi = 2 * kPI * uniformRandom.x;
		const float theta = acos(2 * uniformRandom.y - 1);

		float sin_theta;
		float cos_theta;
		sincos(theta, sin_theta, cos_theta);

		float sin_phi;
		float cos_phi;
		sincos(phi, sin_phi, cos_phi);

		const float3 randomUnitSphere = float3(cos_phi * sin_theta, sin_phi * sin_theta, cos_theta);

		const float3 randomLightPoint = light.m_pos + light.m_radius * randomUnitSphere;
		const float3 rayDir = normalize(randomLightPoint - origin);

		const float NdotL = dot(normal, rayDir);

		bool isRayInvalid = NdotL <= 0;
		// ray invalid, early out
		if (isRayInvalid) {
			return kHitTForInvalidRay;
		}

		const float distToLight = length(randomLightPoint - origin);

		ray.Direction = rayDir;
		ray.Origin = origin;
		ray.TMin = 1e-5;
		ray.TMax = distToLight;
	}

	// Trace the ray
	Payload payload;
	payload.HitT = kHitTForMiss;
	payload.Col = float3(0, 0, 0);

#if INLINE_RAY_TRACING
	{
		// Trace the ray
		RayQuery<RAY_FLAGS> rayQuery;
		rayQuery.TraceRayInline(t_SceneBVH,
			RAY_FLAGS, // ray flags
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
			RAY_FLAGS, // ray flags
			0xFF, // instanceInclusionMask
			0, //RayContributionToHitGroupIndex
			0, //MultiplierForGeometryContributionToHitGroupIndex
			0, //MissShaderIndex
			ray,
			payload);
	}
#endif

	return payload.HitT;
}

float GetViewZ(uint2 globalIdx) {
	const float4 rawDepth = t_DepthTex[globalIdx].xyzw;
	return GetViewZ(
		rawDepth,
		(DepthType)CB.m_depthType,
		float2(CB.m_Viewport_MaxDepth, CB.m_Viewport_MinDepth),
		CB.m_worldToViewMatrix,
		CB.m_clipToViewMatrix);
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
		u_OutputTex[samplePos] = float4(kHitTForInvalidRay, 0, 0, 0);
		return;
	}

	// No lights => all in shadow.
	if (CB.m_numLights == 0) {
		u_OutputTex[samplePos] = float4(kHitTForInvalidRay, 0, 0, 0);
		return;
	}

	float distanceFromCamera = length(surfacePos.xyz - origin.xyz);
	float3 viewDir = (surfacePos.xyz - origin.xyz) / distanceFromCamera;
	const float3 rayOrigin = offset_ray(surfacePos, normal - viewDir, distanceFromCamera);

	RNGState rngState = initRNG(LaunchIndex, LaunchDimensions, CB.m_frameIndex, CB.m_randomNumberGeneratorType, CB.m_globalRandom_F, CB.m_globalRandom_U);

#if ENABLE_MULTI_SHADOW 
	#if KickstartRT_SDK_WITH_NRD
		float viewZ = GetViewZ(LaunchIndex);
		SIGMA_MULTILIGHT_DATATYPE multiLightShadowData = SIGMA_FrontEnd_MultiLightStart();
		float3 Lsum = 0;
		for (uint i = 0; i < min(CB.m_numLights, kMaxNumLights); ++i) {
			float angularExtent;
			float tanOfAngularExtent;
			GetAngularExtent(CB_lightBuffer.m_lights[i], rayOrigin, angularExtent, tanOfAngularExtent);

			const float HitT = TraceShadowRay(CB_lightBuffer.m_lights[i], rayOrigin, normal, rngState);
			Lsum += CB_lightBuffer.m_lights[i].m_intensity;
			SIGMA_FrontEnd_MultiLightUpdate(
				CB_lightBuffer.m_lights[i].m_intensity,
				HitT,
				tanOfAngularExtent,
				1.0f, // Weight
				multiLightShadowData);
		}
		float4 shadowTranslucency;
		float2 val = SIGMA_FrontEnd_MultiLightEnd(viewZ, multiLightShadowData, Lsum, shadowTranslucency);

		u_OutputTex[samplePos] = float4(val, 0, 0);
		u_OutputAuxTex[samplePos] = shadowTranslucency;

	#else // #if KickstartRT_SDK_WITH_NRD
		u_OutputTex[samplePos] = float4(0, 0, 0, 0);
	#endif 

#else // #if ENABLE_MULTI_SHADOW 

	float angularExtent;
	float tanOfAngularExtent;
	GetAngularExtent(CB_lightBuffer.m_lights[0], rayOrigin, angularExtent, tanOfAngularExtent);

	const float HitT = TraceShadowRay(CB_lightBuffer.m_lights[0], rayOrigin, normal, rngState);

	#if KickstartRT_SDK_WITH_NRD
		float viewZ = GetViewZ(LaunchIndex);
		u_OutputTex[samplePos].xy = SIGMA_FrontEnd_PackShadow(viewZ, HitT, tanOfAngularExtent);
	#else // #if KickstartRT_SDK_WITH_NRD
		u_OutputTex[samplePos] = float4(HitT, 0, 0, 0);
	#endif 
#endif
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
