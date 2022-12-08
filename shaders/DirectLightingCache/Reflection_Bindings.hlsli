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

// ---[ Structures ]---
struct CB_Reflection
{
    uint    m_Viewport_TopLeftX;
    uint    m_Viewport_TopLeftY;
    uint    m_Viewport_Width;
    uint    m_Viewport_Height;

    float   m_Viewport_MinDepth;
    float   m_Viewport_MaxDepth;
	float	m_globalRandom_F;
	uint    m_globalRandom_U;

    float3  m_rayOrigin;
    uint    m_outputType;

	DepthType	m_depthType;
	NormalType	m_normalType;
	uint		m_envMapType;
	uint		m_randomNumberGeneratorType;

	float2	m_normalNormalizationFactor;
	float	m_aoRadius;
	bool	m_invertHalfResCheckerboard;

	float4	m_normalChMask1;
	float4	m_normalChMask2;

	uint	m_frameIndex;
	float	m_globalRoughness;
	float	m_globalMetalness;
	float	m_envMapIntensity;

	uint	m_CTA_Swizzle_GroupDimension_X;
	uint	m_CTA_Swizzle_GroupDimension_Y;
	uint	m_paddingu32_1;
	uint	m_OffsetRay_Type;

	float	m_OffsetRay_WorldPosition_Threshold;
	float	m_OffsetRay_WorldPosition_Float_Scale;
	float	m_OffsetRay_WorldPosition_Int_Scale;
	float	m_maxRayLength;

	float	m_OffsetRay_CamDistance_Constant;
	float	m_OffsetRay_CamDistance_Linear;
	float	m_OffsetRay_CamDistance_Quadratic;
	float	m_paddingf32_2;

	float4	m_roughnessMask;

	float	m_roughnessMultiplier;
	float	m_minRoughness;
	float	m_maxRoughness;
	float	m_paddingf32_3;

	uint	m_numLights;
	uint	m_enableLightTex;
	uint	m_enableBilinearSample;
	uint	m_pad;

	float4x4	m_clipToViewMatrix;
	float4x4	m_viewToClipMatrix;
	float4x4	m_viewToWorldMatrix;
	float4x4	m_worldToViewMatrix;
    float4x4	m_normalToWorldMatrix;
	float4x4	m_worldToEnvMapMatrix;
};

// TODO:
// This should be a regular SRV - Thread divergent access to cbuffers are bad for perf, which can become an issue for multiple stochastically sampled lights
struct CB_Light
{
	// Common...
	LightType	m_type;
	float3		m_dirVec;
	
	float2	m_dir; // Sperical coords.
	float	m_intensity;
	uint	_pad0;

	// Directional light data...
	float	m_angularExtent;
	float	m_tanOfAngularExtent;
	uint2	_pad1;

	// Spotlight data...
	float	m_radius;
	float	m_range;
	float	m_cosApexAngle;
	uint	_pad2;

	float3	m_pos;
	uint	_pad3;
};

struct CB_Lights
{
	CB_Light m_lights[128];
};

// ---[ Resources ]---
//[[vk::binding(0, 0)]]
KS_VK_BINDING(0, 0)
SamplerState LinearClampSampler : register(s0);

//[[vk::binding(0, 1)]]
KS_VK_BINDING(0, 1)
RaytracingAccelerationStructure t_SceneBVH : register(t0, space1);

//[[vk::binding(2, 2)]]
KS_VK_BINDING(2, 2)
ConstantBuffer<CB_Reflection> CB : register(b0);

//[[vk::binding(3, 2)]]
KS_VK_BINDING(3, 2)
ConstantBuffer<CB_Lights> CB_lightBuffer : register(b1);

//[[vk::binding(4, 2)]]
KS_VK_BINDING(4, 2)
Texture2D<float4> t_DepthTex : register(t0);

//[[vk::binding(5, 2)]]
KS_VK_BINDING(5, 2)
Texture2D<float4> t_NormalTex : register(t1);

//[[vk::binding(6, 2)]]
KS_VK_BINDING(6, 2)
Texture2D<float3> t_SpecularTex : register(t2);

//[[vk::binding(7, 2)]]
KS_VK_BINDING(7, 2)
Texture2D<float4> t_RoughnessTex : register(t3);

//[[vk::binding(8, 2)]]
KS_VK_BINDING(8, 2)
Texture2D<float3> t_EnvMapTex : register(t4);

//[[vk::binding(9, 2)]]
KS_VK_BINDING(9, 2)
Texture2D<uint> t_InputMaskTex : register(t5);

//[[vk::binding(10, 2)]]
KS_VK_BINDING(10, 2)
Texture2D<float4> t_LightingTex : register(t6);

//[[vk::binding(11, 2)]]
KS_VK_BINDING(11, 2)
RWTexture2D<float4> u_OutputTex	: register(u0);

//[[vk::binding(12, 2)]]
KS_VK_BINDING(12, 2)
RWTexture2D<float4> u_OutputAuxTex	: register(u1);

// ---[ Common functions among reflection shaders ]---
float3 offset_ray(const float3 p, const float3 n, const float distanceFromCamera)
{
	if (CB.m_OffsetRay_Type == 0) {
		const float	c = CB.m_OffsetRay_CamDistance_Constant;
		const float	l = CB.m_OffsetRay_CamDistance_Linear;
		const float	q = CB.m_OffsetRay_CamDistance_Quadratic;

		float ofs = c + l * distanceFromCamera + q * distanceFromCamera * distanceFromCamera;

		return p + n * ofs;
	}
	else {
		// From Ray Tracing Gems V1.4.
		// Normal points outward for rays exiting the surface, else is flipped.
		const float threshold = CB.m_OffsetRay_WorldPosition_Threshold;
		const uint int_scale = CB.m_OffsetRay_WorldPosition_Int_Scale;
		const float float_scale = CB.m_OffsetRay_WorldPosition_Float_Scale;

		int3 of_i = int3(int_scale * n.x, int_scale * n.y, int_scale * n.z);

		float3 p_i = float3(
			asfloat(asint(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
			asfloat(asint(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
			asfloat(asint(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

		return float3(
			abs(p.x) < threshold ? p.x + float_scale * n.x : p_i.x,
			abs(p.y) < threshold ? p.y + float_scale * n.y : p_i.y,
			abs(p.z) < threshold ? p.z + float_scale * n.z : p_i.z);
	}
}
