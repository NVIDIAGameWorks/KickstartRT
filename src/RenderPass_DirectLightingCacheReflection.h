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
#pragma once
#include <Platform.h>
#include <GraphicsAPI/GraphicsAPI.h>
#include <ShaderFactory.h>

#include <memory>
#include <deque>
#include <random>

namespace KickstartRT_NativeLayer
{
	class PersistentkWorkingSet;
	class TaskWorkingSet;
	struct RenderPass_ResourceRegistry;
};

namespace KickstartRT_NativeLayer
{
	struct RenderPass_DirectLightingCacheReflection
	{
		static constexpr uint32_t     m_threadDim_XY[2] = { 8, 16 };

		enum DescTableLayout : uint32_t {
			e_BlueNoiseTex_SRV = 0,
			e_UnusedTex_SRV,
			e_CB_CBV,
			e_DepthTex_SRV,
			e_NormalTex_SRV,
			e_Specularex_SRV,
			e_RoughnessTex_SRV,
			e_EnvMapTex_SRV,
			e_Output_UAV,
			e_OutputSpecularReflectance_UAV,
			e_DescTableSize,
		};

		enum class ReflectionShaderPermutationBits : uint32_t {
			e_ENABLE_SPECULAR_TEX		= 0b0000'0001,
			e_ENABLE_ROUGHNESS_TEX		= 0b0000'0010,
			e_ENABLE_ENV_MAP_TEX		= 0b0000'0100,
			e_DEMODULATE_SPECULAR		= 0b0000'1000,
			e_HALF_RESOLUTION			= 0b0001'0000,
			e_ENABLE_INPUT_MASK			= 0b0010'0000,
			e_NumberOfPermutations		= 0b0100'0000
		};
		enum class GIShaderPermutationBits : uint32_t {
			e_ENABLE_ENV_MAP_TEX	= 0b0000'0001,
			e_HALF_RESOLUTION		= 0b0000'0010,
			e_ENABLE_INPUT_MASK		= 0b0000'0100,
			e_USE_NORMALIZED_DIFFUSE = 0b0000'1000,
			e_ENABLE_ROUGHNESS_TEX	= 0b0001'0000,
			e_NumberOfPermutations	= 0b0010'0000
		};
		enum class AOShaderPermutationBits : uint32_t {
			e_HALF_RESOLUTION		= 0b0000'0001,
			e_ENABLE_INPUT_MASK		= 0b0000'0010,
			e_NumberOfPermutations	= 0b0000'0100
		};
		enum class ShadowsShaderPermutationBits : uint32_t {
			e_HALF_RESOLUTION = 0b0000'0001,
			e_ENABLE_INPUT_MASK = 0b0000'0010,
			e_ENABLE_MULTI_SHADOW = 0b0000'0100,
			e_ENABLE_ACCEPT_FIRST_HIT_AND_END_SEARCH = 0b0000'1000,
			e_NumberOfPermutations = 0b0001'0000
		};

		struct CB {
			uint32_t    m_Viewport_TopLeftX;
			uint32_t    m_Viewport_TopLeftY;
			uint32_t    m_Viewport_Width;
			uint32_t    m_Viewport_Height;

			float       m_Viewport_MinDepth;
			float       m_Viewport_MaxDepth;
			float		m_globalRandom_F;
			uint32_t    m_globalRandom_U;

			float       m_rayOrigin[3];
			uint32_t	m_outputType;

			uint32_t	m_depthType;
			uint32_t	m_normalType;
			uint32_t	m_envMapType;
			uint32_t	m_randomNumberGeneratorType;

			float	m_normalNormalizationFactor[2];
			float	m_aoRadius;
			bool	m_invertHalfResCheckerboard;

			Math::Float_4 m_normalChMask1;
			Math::Float_4 m_normalChMask2;

			uint32_t	m_frameIndex;
			float		m_globalRoughness;
			float		m_globalMetalness;
			float		m_envMapIntensity;

			uint32_t	m_CTA_Swizzle_GroupDimension_X;
			uint32_t	m_CTA_Swizzle_GroupDimension_Y;
			uint32_t	m_paddingU32_2;
			uint32_t	m_OffsetRay_Type;

			float	m_OffsetRay_WorldPosition_Threshold;
			float	m_OffsetRay_WorldPosition_Float_Scale;
			float	m_OffsetRay_WorldPosition_Int_Scale;
			float	m_paddingf32_1;

			float	m_OffsetRay_CamDistance_Constant;
			float	m_OffsetRay_CamDistance_Linear;
			float	m_OffsetRay_CamDistance_Quadratic;
			float	m_paddingf32_2;

			Math::Float_4 m_roughnessMask;

			float	m_roughnessMultiplier;
			float	m_minRoughness;
			float	m_maxRoughness;
			float	m_paddingf32_3;

			uint32_t m_numLights;
			uint32_t m_enableLightTex;
			uint32_t m_enableBilinearSampling;
			uint32_t m_pad;

			Math::Float_4x4	m_clipToViewMatrix;
			Math::Float_4x4	m_viewToClipMatrix;
			Math::Float_4x4	m_viewToWorldMatrix;
			Math::Float_4x4	m_worldToViewMatrix;
			Math::Float_4x4	m_normalToWorldMatrix;
			Math::Float_4x4	m_worldToEnvMapMatrix;
		};

		GraphicsAPI::DescriptorTableLayout	m_descTableLayout0;
		GraphicsAPI::DescriptorTableLayout	m_descTableLayout1;
		GraphicsAPI::DescriptorTableLayout	m_descTableLayout2;
		GraphicsAPI::RootSignature			m_rootSignature;

		bool m_enableInlineRaytracing = false;
		bool m_enableShaderTableRaytracing = false;

		std::array<ShaderFactory::ShaderDictEntry*, (size_t)ReflectionShaderPermutationBits::e_NumberOfPermutations>	m_shaderTable = {};
		std::array<ShaderFactory::ShaderDictEntry*, (size_t)GIShaderPermutationBits::e_NumberOfPermutations>			m_shaderTable_GI = {};
		ShaderFactory::ShaderDictEntry* m_shaderTable_DebugVis = nullptr;
		std::array<ShaderFactory::ShaderDictEntry*, (size_t)AOShaderPermutationBits::e_NumberOfPermutations>			m_shaderTable_AO = {};
		std::array<ShaderFactory::ShaderDictEntry*, (size_t)ShadowsShaderPermutationBits::e_NumberOfPermutations>		m_shaderTable_Shadows = {};

		std::array<ShaderFactory::ShaderDictEntry*, (size_t)ReflectionShaderPermutationBits::e_NumberOfPermutations>	m_pso = {};
		std::array<ShaderFactory::ShaderDictEntry*, (size_t)GIShaderPermutationBits::e_NumberOfPermutations>			m_pso_GI = {};
		ShaderFactory::ShaderDictEntry* m_pso_DebugVis = nullptr;
		std::array<ShaderFactory::ShaderDictEntry*, (size_t)AOShaderPermutationBits::e_NumberOfPermutations>			m_pso_AO = {};
		std::array<ShaderFactory::ShaderDictEntry*, (size_t)ShadowsShaderPermutationBits::e_NumberOfPermutations>		m_pso_Shadows = {};

		std::unique_ptr< GraphicsAPI::Sampler>	m_linearClampSampler;
		std::unique_ptr<GraphicsAPI::Texture>	m_blueNoiseTex;
		std::unique_ptr<GraphicsAPI::Buffer>	m_blueNoiseTexUpBuf;
		GraphicsAPI::ShaderResourceView			m_blueNoiseTexSRV;

		bool						m_blueNoiseTextureIsReady = false;

		// Random numbers generator
		std::mt19937				m_randomGenerator;
		uint32_t					m_globalRandomLastUpdate = uint32_t(-1);
		float						m_globalRandom_F = 0.f;
		uint32_t					m_globalRandom_U = 0u;

    public:
		Status Init(PersistentWorkingSet* pws, bool enableInlineRaytracing, bool enableShaderTableRaytracing);
		Status Dispatch(TaskWorkingSet* tws,
			GraphicsAPI::DescriptorTable* sampler_descTable,
			GraphicsAPI::DescriptorTable* lightingCache_descTable,
			GraphicsAPI::CommandList* cmdList, 
			RenderPass_ResourceRegistry* resources, 
			const RenderTask::Task* traceTask);
		Status BuildCommandList(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList,
			RenderPass_ResourceRegistry* resources,
			GraphicsAPI::DescriptorTable* lightingCache_descTable,
			const RenderTask::Task* traceTask);
	};
};
