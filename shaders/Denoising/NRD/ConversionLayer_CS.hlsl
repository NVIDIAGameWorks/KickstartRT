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

#include "Shared/Shared.hlsli"
#include "Shared/Binding.hlsli"

#if KickstartRT_SDK_WITH_NRD
#define COMPILER_DXC
#include "NRD.hlsli"

#define REFLECTION_OUTPUT_MODE_NRD_REBLUR_SPEC (0)		// DenoisingMode::NRD_ReblurSpec
#define REFLECTION_OUTPUT_MODE_NRD_RELAX_SPEC (1)		// DenoisingMode::NRD_RelaxSpec
#define REFLECTION_OUTPUT_MODE_NRD_REBLUR_DIFF (2)		// DenoisingMode::NRD_ReblurDiff
#define REFLECTION_OUTPUT_MODE_NRD_RELAX_DIFF (3)		// DenoisingMode::NRD_RelaxDiff

enum class DenoisingMethod : uint {
    NRD_Reblur = 0,
    NRD_Relax = 1,
    NRD_Sigma = 2,
};

enum class SignalType : uint {
    Specular = 0,
    Diffuse = 1,
    SpecularAndDiffuse = 2,
    DiffuseOcclusion = 3,
    Shadows = 4,
};

struct CB_NRDConvertInputs
{
    DenoisingMethod method;
    SignalType      signalType;
    uint    pad1;
    uint    pad2;

    DepthType   depthTypeWorldPos;
    uint        pad3;
    uint        pad4;
    uint        pad5;

    float   viewport_MaxDepth;
    float   viewport_MinDepth;
    uint    viewport_Width;
    uint    viewport_Height;

    uint    enableRoughnessTex;
    float   globalRoughness;
    uint    pad6;
    uint    pad7;

    float   roughnessMultiplier;
    float   minRoughness;
    float   maxRoughness;
    uint    pad8;

    float4  roughnessMask;
    float4  hitTMask;

    float   metersToUnitsMultiplier;
    uint    pad9;
    uint    pad10;
    uint    pad11;

    float        tanOfLightAngularRadius;
    NormalType   normalType;
    float2       normalNormalizationFactor;

    float4  normalChMask1;
    float4  normalChMask2;

    float4x4 normalToWorldMatrix;

    float4 nrdHitDistanceParameters;

    float4x4 worldToViewMatrix;
    float4x4 clipToViewMatrix;
};

// ---[ Resources ]---

//[[vk::binding(0, 0)]]
KS_VK_BINDING(0, 0)
ConstantBuffer<CB_NRDConvertInputs> g_CB : register(b0);

//[[vk::binding(1, 0)]]
KS_VK_BINDING(1, 0)
Texture2D<float4> t_inDepth : register(t0);

//[[vk::binding(2, 0)]]
KS_VK_BINDING(2, 0)
Texture2D<float4> t_Normal : register(t1);

//[[vk::binding(3, 0)]]
KS_VK_BINDING(3, 0)
Texture2D<float4> t_Roughness : register(t2);

//[[vk::binding(4, 0)]]
KS_VK_BINDING(4, 0)
Texture2D<float4> t_specRadianceAndHitT : register(t3);

//[[vk::binding(5, 0)]]
KS_VK_BINDING(5, 0)
Texture2D<float4> t_diffRadianceAndHitT : register(t4);

//[[vk::binding(6, 0)]]
KS_VK_BINDING(6, 0)
Texture2D<float4> t_diffOcclusionHitT : register(t5);

//[[vk::binding(7, 0)]]
KS_VK_BINDING(7, 0)
RWTexture2D<float> u_LinearDepth : register(u0);

//[[vk::binding(8, 0)]]
KS_VK_BINDING(8, 0)
RWTexture2D<float4> u_NormalAndRoughness : register(u1);

//[[vk::binding(9, 0)]]
KS_VK_BINDING(9, 0)
RWTexture2D<float4> u_PackedSpecRadianceAndHitT : register(u2);

//[[vk::binding(10, 0)]]
KS_VK_BINDING(10, 0)
RWTexture2D<float4> u_PackedDiffRadianceAndHitT : register(u3);

//[[vk::binding(11, 0)]]
KS_VK_BINDING(11, 0)
RWTexture2D<float4> u_PackedDiffOcclusion : register(u4);

float GetViewZ(uint2 globalIdx) {

    const float4 rawDepth = t_inDepth[globalIdx].xyzw;
    DepthType depthType = (DepthType)g_CB.depthTypeWorldPos;

    return GetViewZ(
        rawDepth, 
        depthType,
        float2(g_CB.viewport_MaxDepth, g_CB.viewport_MinDepth),
        g_CB.worldToViewMatrix,
        g_CB.clipToViewMatrix);
}

float GetRoughness(uint2 globalIdx) {
    const float roughness = g_CB.enableRoughnessTex ? LoadRoughness(t_Roughness, globalIdx, g_CB.roughnessMask) : g_CB.globalRoughness;
    return RemapRoughness(roughness, g_CB.roughnessMultiplier, g_CB.minRoughness, g_CB.maxRoughness);;
}

float3 GetWorldNormal(uint2 globalIdx, out bool IsValid) {
    const float4 rawNormal = t_Normal[globalIdx].xyzw;
    return GetWorldNormal(rawNormal, g_CB.normalType, g_CB.normalNormalizationFactor, g_CB.normalChMask1, g_CB.normalChMask2, (float3x3)g_CB.normalToWorldMatrix, IsValid);
}

void WriteDepth(uint2 globalIdx, float viewZ) {
    u_LinearDepth[globalIdx] = viewZ;
}

void WriteNormalAndRougness(uint2 globalIdx, float3 worldNormal, float roughness) {
    float3 worldNormalPackedUnorm = (normalize(worldNormal) + 1.f) * 0.5f;
    u_NormalAndRoughness[globalIdx] = float4(worldNormalPackedUnorm, roughness);
}

void _WriteRadianceAndHitT(Texture2D<float4> t_radianceAndHitT, RWTexture2D<float4> u_packedRadianceAndHitT, uint2 globalIdx, float linearRoughness, float viewZ) {
    const float4 radianceAndHitT = t_radianceAndHitT[globalIdx];
    const float3 radiance = radianceAndHitT.xyz;
    const float hitT = radianceAndHitT.w;

    if (g_CB.method == DenoisingMethod::NRD_Reblur) {
        const float normHitDist = REBLUR_FrontEnd_GetNormHitDist(hitT, viewZ, g_CB.nrdHitDistanceParameters, g_CB.metersToUnitsMultiplier, linearRoughness);
        u_packedRadianceAndHitT[globalIdx] = REBLUR_FrontEnd_PackRadianceAndHitDist(radiance, hitT);
    }
    else if (g_CB.method == DenoisingMethod::NRD_Relax) {
        u_packedRadianceAndHitT[globalIdx] = RELAX_FrontEnd_PackRadianceAndHitDist(radiance, hitT);
    }
}

void _WriteOcclusion(Texture2D<float4> t_occlusionHitT, RWTexture2D<float4> u_PackedOcclusion, uint2 globalIdx, float linearRoughness, float viewZ) {
    float hitT = dot(t_occlusionHitT[globalIdx].xyzw, g_CB.hitTMask);
    if (hitT < 0)
        hitT = 65504.f;

    if (g_CB.method == DenoisingMethod::NRD_Reblur) {
        u_PackedOcclusion[globalIdx].x = REBLUR_FrontEnd_GetNormHitDist(hitT, viewZ, g_CB.nrdHitDistanceParameters, g_CB.metersToUnitsMultiplier, linearRoughness);
    }
}

void WriteRadianceAndHitT(uint2 globalIdx, float linearRoughness, float viewZ) {
    if (g_CB.signalType == SignalType::Specular || g_CB.signalType == SignalType::SpecularAndDiffuse)
    {
        _WriteRadianceAndHitT(t_specRadianceAndHitT, u_PackedSpecRadianceAndHitT, globalIdx, linearRoughness, viewZ);
    }

    if (g_CB.signalType == SignalType::Diffuse || g_CB.signalType == SignalType::SpecularAndDiffuse)
    {
        _WriteRadianceAndHitT(t_diffRadianceAndHitT, u_PackedDiffRadianceAndHitT, globalIdx, 1.f /*diffuse must set roughness = 1*/, viewZ);
    }

    if (g_CB.signalType == SignalType::DiffuseOcclusion)
    {
        _WriteOcclusion(t_diffOcclusionHitT, u_PackedDiffOcclusion, globalIdx, 1.f /*diffuse must set roughness = 1*/, viewZ);
    }
}

[numthreads(8, 16, 1)]
void main(
	uint2 groupIdx : SV_GroupID,
	uint2 globalIdx : SV_DispatchThreadID,
	uint2 threadIdx : SV_GroupThreadID)
{
    bool isWorldNormalValid;
    const float3 worldNormal = GetWorldNormal(globalIdx, isWorldNormalValid);

    const float viewZ = GetViewZ(globalIdx);
    const float roughness = GetRoughness(globalIdx);

    WriteDepth(globalIdx, viewZ);
    WriteNormalAndRougness(globalIdx, worldNormal, roughness);
    WriteRadianceAndHitT(globalIdx, roughness, viewZ);
}
#else
[numthreads(8, 16, 1)]
void main(
    uint2 groupIdx : SV_GroupID,
    uint2 globalIdx : SV_DispatchThreadID,
    uint2 threadIdx : SV_GroupThreadID)
{
}
#endif