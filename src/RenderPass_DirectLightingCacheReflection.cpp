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
#include <RenderPass_DirectLightingCacheReflection.h>
#include <RenderPass_Common.h>
#include <Utils.h>
#include <Log.h>
#include <Platform.h>
#include <PersistentWorkingSet.h>
#include <TaskWorkingSet.h>
#include <ShaderFactory.h>
#include <ShaderTableRT.h>
#include <Scene.h>
#include <WinResFS.h>

#include <cstring>

#include <inttypes.h>
#include <unordered_set>
#include <tuple>

namespace KickstartRT_NativeLayer
{
	Status RenderPass_DirectLightingCacheReflection::Init(PersistentWorkingSet* pws, bool enableInlineRaytracing, bool enableShaderTableRaytracing)
	{
		GraphicsAPI::Device* dev = &pws->m_device;

		auto& sf(pws->m_shaderFactory);

		m_enableInlineRaytracing = enableInlineRaytracing;
		m_enableShaderTableRaytracing = enableShaderTableRaytracing;

		// loading BN texture.
		{
			constexpr uint32_t w = 128;
			constexpr uint32_t h = 128;
			constexpr uint32_t d = 64;
			constexpr uint32_t pixelInBytes = 1;

			std::function<void(void* dest, void* src, uint32_t nbPixels)> pixelCopyFunc = [](void* d, void* s, uint32_t nbPixels)
			{
				memcpy(d, s, nbPixels);
			};

			std::wstring texPath(L"Texture/BN_128x128x64_R8.bin");
			{
				if (pws->LoadSingleMipTextureFromResource(texPath, w, h, d, pixelInBytes,
					GraphicsAPI::Resource::Type::Texture3D, GraphicsAPI::Resource::Format::R8Uint, pixelCopyFunc, m_blueNoiseTex, m_blueNoiseTexUpBuf,
					ResourceLogger::ResourceKind::e_Other) != Status::OK) {
					Log::Fatal(L"Failed to load blue noise texture:%s", texPath.c_str());
					return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
				}
				m_blueNoiseTexUpBuf->SetName(DebugName(L"RP_DirectLightingCacheReflection - BN up"));
				m_blueNoiseTex->SetName(DebugName(L"RP_DirectLightingCacheReflection - BN"));

				if (!m_blueNoiseTexSRV.Init(&pws->m_device, m_blueNoiseTex.get())) {
					Log::Fatal(L"Failed to create shader resource view");
					return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
				}
			}
		}

		// create linear clamp sampler
		m_linearClampSampler = std::make_unique<GraphicsAPI::Sampler>();
		if (!m_linearClampSampler->CreateLinearClamp(dev)) {
			Log::Fatal(L"Failed to create a sampler");
			return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
		}

		// Root signature
		{
			m_descTableLayout0.AddRange(GraphicsAPI::DescriptorHeap::Type::Sampler, 0, 1, 0); // s0, linear clamp sampler;
			if (!m_descTableLayout0.SetAPIData(dev)) {
				Log::Fatal(L"Failed to set apiData for descriptor table layout.");
				return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
			}

			m_descTableLayout1.AddRange(GraphicsAPI::DescriptorHeap::Type::AccelerationStructureSrv, 0, 1, 1); // t0, space1 TLAS
			m_descTableLayout1.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 0, 1, 1); // u0, space1, tile table
			m_descTableLayout1.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 1, -pws->m_unboundDescTableUpperbound, 1); //u1 ~ space1, tileIndex, tileBuffer ...
			if (!m_descTableLayout1.SetAPIData(dev)) {
				Log::Fatal(L"Failed to set apiData for descriptor table layout.");
				return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
			}

			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 0, 1, 2); // t0, space2, BlueNoiseTex
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferSrv, 1, 1, 2); // t1, space2, Null
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::Cbv, 0, 1, 0); // b0, CB
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::Cbv, 1, 1, 0); // b1, CB_lights
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 0, 1, 0); // t0 (depthTex)
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 1, 1, 0); // t1 (normalTex)
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 2, 1, 0); // t2 (specularTex)
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 3, 1, 0); // t3 (roughnessTex)
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 4, 1, 0); // t4 (envMapTex)
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 5, 1, 0); // t5 (inputMaskTex)
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 6, 1, 0); // t6 (lightingTex)
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureUav, 0, 1, 0); // u0 outputTex
			m_descTableLayout2.AddRange(GraphicsAPI::DescriptorHeap::Type::TextureUav, 1, 1, 0); // u1 outAux
			if (!m_descTableLayout2.SetAPIData(dev)) {
				Log::Fatal(L"Failed to set apiData for descriptor table layout.");
				return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
			}

			std::vector<GraphicsAPI::DescriptorTableLayout*> tableLayouts = { &m_descTableLayout0, &m_descTableLayout1, &m_descTableLayout2 };
			// todo Static Sampler
			if (!m_rootSignature.Init(dev, tableLayouts)) {
				Log::Fatal(L"Failed to create rootSignature");
				return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
			}
			m_rootSignature.SetName(DebugName(L"RP_DirectLightingCacheReflection"));
		}

		auto RegisterShader = [&sf](
			const std::wstring& fileName, const std::wstring& entryName, const std::wstring& shaderName,
			ShaderFactory::ShaderType::Enum type, const std::vector<ShaderFactory::ShaderMacro>& shaderMacro, GraphicsAPI::RootSignature& rootSig)
			-> std::pair<Status, ShaderFactory::ShaderDictEntry*>
		{
			std::unique_ptr<ShaderFactory::ShaderDictEntry> dictEnt = std::make_unique< ShaderFactory::ShaderDictEntry>();
			dictEnt->m_fileName = fileName;
			dictEnt->m_entryName = entryName;
			dictEnt->m_shaderName = shaderName;
			dictEnt->m_type = type;
			dictEnt->m_shaderMacro_CRC = sf->GetShaderMacroCRC(&shaderMacro);
			dictEnt->m_rootSig = &rootSig;

			auto ofsSize = sf->FindShaderOffset(dictEnt->m_fileName.c_str(), dictEnt->m_entryName.c_str(), dictEnt->m_shaderMacro_CRC, dictEnt->m_type);
			if (!ofsSize.has_value()) {
				Log::Fatal(L"Failed to find a binary entry for shader:%s", dictEnt->m_fileName.c_str());
				return { Status::ERROR_FAILED_TO_INIT_RENDER_PASS, nullptr};
			}
			dictEnt->m_offset = ofsSize.value().first;
			dictEnt->m_size = ofsSize.value().second;

			dictEnt->CalcCRC();

			return sf->RegisterShader(std::move(dictEnt));
		};

		std::filesystem::path libPath(L"DirectLightingCache/Reflection_rt_LIB.hlsl");
		std::filesystem::path libPath_GI(L"DirectLightingCache/Reflection_GI_rt_LIB.hlsl");
		std::filesystem::path libPath_AO(L"DirectLightingCache/Reflection_AO_rt_LIB.hlsl");
		std::filesystem::path libPath_Shadows(L"DirectLightingCache/Shadows_rt_LIB.hlsl");
		std::filesystem::path libPath_DebugVis(L"DirectLightingCache/Reflection_DebugVis_rt_LIB.hlsl");

		std::filesystem::path csPath(L"DirectLightingCache/Reflection_rt_CS.hlsl");
		std::filesystem::path csPath_GI(L"DirectLightingCache/Reflection_GI_rt_CS.hlsl");
		std::filesystem::path csPath_AO(L"DirectLightingCache/Reflection_AO_rt_CS.hlsl");
		std::filesystem::path csPath_Shadows(L"DirectLightingCache/Shadows_rt_CS.hlsl");
		std::filesystem::path csPath_DebugVis(L"DirectLightingCache/Reflection_DebugVis_rt_CS.hlsl");

		{
			constexpr const char* defArr[2] = { "0", "1" };
			std::vector<ShaderFactory::ShaderMacro> defines;
			defines.push_back({ "ENABLE_SPECULAR_TEX", "" });
			defines.push_back({ "ENABLE_ROUGHNESS_TEX", "" });
			defines.push_back({ "ENABLE_ENV_MAP_TEX", "" });
			defines.push_back({ "DEMODULATE_SPECULAR", "" });
			defines.push_back({ "ENABLE_HALF_RESOLUTION", "" });
			defines.push_back({ "ENABLE_INPUT_MASK", "" });

			for (uint32_t i = 0; i < (uint32_t)ReflectionShaderPermutationBits::e_NumberOfPermutations; ++i) {
				defines[0].definition = defArr[i & (uint32_t)ReflectionShaderPermutationBits::e_ENABLE_SPECULAR_TEX ? 1 : 0];
				defines[1].definition = defArr[i & (uint32_t)ReflectionShaderPermutationBits::e_ENABLE_ROUGHNESS_TEX ? 1 : 0];
				defines[2].definition = defArr[i & (uint32_t)ReflectionShaderPermutationBits::e_ENABLE_ENV_MAP_TEX ? 1 : 0];
				defines[3].definition = defArr[i & (uint32_t)ReflectionShaderPermutationBits::e_DEMODULATE_SPECULAR ? 1 : 0];
				defines[4].definition = defArr[i & (uint32_t)ReflectionShaderPermutationBits::e_HALF_RESOLUTION ? 1 : 0];
				defines[5].definition = defArr[i & (uint32_t)ReflectionShaderPermutationBits::e_ENABLE_INPUT_MASK ? 1 : 0];

				if (m_enableInlineRaytracing)
				{
					auto [sts, itr] = RegisterShader(csPath.wstring(), L"main", DebugName(L"RP_DirectLightingCacheReflection[%d]", i),
						ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_pso[i] = itr;
				}
				{
					auto [sts, itr] = RegisterShader(libPath.wstring(), L"main", DebugName(L"RP_DirectLightingCacheReflection[%d]", i),
						ShaderFactory::ShaderType::Enum::SHADER_RAY_GENERATION, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_shaderTable[i] = itr;
				}
			}
		}
		{
			constexpr const char* defArr[2] = { "0", "1" };
			std::vector<ShaderFactory::ShaderMacro> defines;
			defines.push_back({ "ENABLE_SPECULAR_TEX", "0" });
			defines.push_back({ "ENABLE_ROUGHNESS_TEX", "0" });
			defines.push_back({ "ENABLE_ENV_MAP_TEX", "" });
			defines.push_back({ "ENABLE_HALF_RESOLUTION", "" });
			defines.push_back({ "ENABLE_INPUT_MASK", "" });
			defines.push_back({ "USE_NORMALIZED_DIFFUSE", "" });
			
			for (uint32_t i = 0; i < (uint32_t)GIShaderPermutationBits::e_NumberOfPermutations; ++i) {
				defines[1].definition = defArr[i & (uint32_t)GIShaderPermutationBits::e_ENABLE_ROUGHNESS_TEX ? 1 : 0];
				defines[2].definition = defArr[i & (uint32_t)GIShaderPermutationBits::e_ENABLE_ENV_MAP_TEX ? 1 : 0];
				defines[3].definition = defArr[i & (uint32_t)GIShaderPermutationBits::e_HALF_RESOLUTION ? 1 : 0];
				defines[4].definition = defArr[i & (uint32_t)GIShaderPermutationBits::e_ENABLE_INPUT_MASK ? 1 : 0];
				defines[5].definition = defArr[i & (uint32_t)GIShaderPermutationBits::e_USE_NORMALIZED_DIFFUSE ? 1 : 0];

				if (m_enableInlineRaytracing)
				{
					auto [sts, itr] = RegisterShader(csPath_GI.wstring(), L"main", DebugName(L"RP_DirectLightingCacheGI[%d]", i),
						ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_pso_GI[i] = itr;
				}
				{
					auto [sts, itr] = RegisterShader(libPath_GI.wstring(), L"main", DebugName(L"RP_DirectLightingCacheGI[%d]", i),
						ShaderFactory::ShaderType::Enum::SHADER_RAY_GENERATION, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_shaderTable_GI[i] = itr;
				}
			}
		}
		{
			constexpr const char* defArr[2] = { "0", "1" };
			std::vector<ShaderFactory::ShaderMacro> defines;
			defines.push_back({ "ENABLE_SPECULAR_TEX", "0" });
			defines.push_back({ "ENABLE_ROUGHNESS_TEX", "0" });
			defines.push_back({ "ENABLE_ENV_MAP_TEX", "0" });
			defines.push_back({ "ENABLE_HALF_RESOLUTION", "" });
			defines.push_back({ "ENABLE_INPUT_MASK", "" });

			for (uint32_t i = 0; i < (uint32_t)AOShaderPermutationBits::e_NumberOfPermutations; ++i) {
				defines[3].definition = defArr[i & (uint32_t)AOShaderPermutationBits::e_HALF_RESOLUTION ? 1 : 0];
				defines[4].definition = defArr[i & (uint32_t)AOShaderPermutationBits::e_ENABLE_INPUT_MASK ? 1 : 0];

				if (m_enableInlineRaytracing)
				{
					auto [sts, itr] = RegisterShader(csPath_AO.wstring(), L"main", DebugName(L"RP_DirectLightingCacheAO[%d]", i),
						ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_pso_AO[i] = itr;
				}
				{
					auto [sts, itr] = RegisterShader(libPath_AO.wstring(), L"main", DebugName(L"RP_DirectLightingCacheAO[%d]", i),
						ShaderFactory::ShaderType::Enum::SHADER_RAY_GENERATION, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_shaderTable_AO[i] = itr;
				}
			}
		}
		{
			constexpr const char* defArr[2] = { "0", "1" };
			std::vector<ShaderFactory::ShaderMacro> defines;
			defines.push_back({ "ENABLE_HALF_RESOLUTION", "" });
			defines.push_back({ "ENABLE_INPUT_MASK", "" });
			defines.push_back({ "ENABLE_MULTI_SHADOW", "" });
			defines.push_back({ "ENABLE_ACCEPT_FIRST_HIT_AND_END_SEARCH", "" });

			for (uint32_t i = 0; i < (uint32_t)ShadowsShaderPermutationBits::e_NumberOfPermutations; ++i) {
				defines[0].definition = defArr[i & (uint32_t)ShadowsShaderPermutationBits::e_HALF_RESOLUTION ? 1 : 0];
				defines[1].definition = defArr[i & (uint32_t)ShadowsShaderPermutationBits::e_ENABLE_INPUT_MASK ? 1 : 0];
				defines[2].definition = defArr[i & (uint32_t)ShadowsShaderPermutationBits::e_ENABLE_MULTI_SHADOW ? 1 : 0];
				defines[3].definition = defArr[i & (uint32_t)ShadowsShaderPermutationBits::e_ENABLE_ACCEPT_FIRST_HIT_AND_END_SEARCH ? 1 : 0];

				if (m_enableInlineRaytracing)
				{
					auto [sts, itr] = RegisterShader(csPath_Shadows.wstring(), L"main", DebugName(L"RP_DirectLightingCacheShadow[%d]", i),
						ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_pso_Shadows[i] = itr;
				}
				{
					auto [sts, itr] = RegisterShader(libPath_Shadows.wstring(), L"main", DebugName(L"RP_DirectLightingCacheShadow[%d]", i),
						ShaderFactory::ShaderType::Enum::SHADER_RAY_GENERATION, defines, m_rootSignature);
					if (sts != Status::OK)
						return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

					m_shaderTable_Shadows[i] = itr;
				}
			}
		}
		{
			std::vector<ShaderFactory::ShaderMacro> defines;
			defines.push_back({ "ENABLE_SPECULAR_TEX", "0" });
			defines.push_back({ "ENABLE_ROUGHNESS_TEX", "0" });
			defines.push_back({ "ENABLE_ENV_MAP_TEX", "0" });

			if (m_enableInlineRaytracing)
			{
				auto [sts, itr] = RegisterShader(csPath_DebugVis.wstring(), L"main", DebugName(L"RP_DirectLightingCacheDebugVis"),
					ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, m_rootSignature);
				if (sts != Status::OK)
					return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

				m_pso_DebugVis = itr;
			}
			{
				auto [sts, itr] = RegisterShader(libPath_DebugVis.wstring(), L"main", DebugName(L"RP_DirectLightingCacheDebugVis"),
					ShaderFactory::ShaderType::Enum::SHADER_RAY_GENERATION, defines, m_rootSignature);
				if (sts != Status::OK)
					return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;

				m_shaderTable_DebugVis= itr;
			}
		}

		// Initialize random generator
		std::random_device randomDevice;
		m_randomGenerator.seed(randomDevice());

		return Status::OK;
	};

	enum class OutputType : uint32_t {
		Reflections = 0,
		GI = 1,
		AO = 2,
		Shadow = 3,
		MultiShadow = 4,
	};

	// need to set root sig and desc table #1 before calling this function.
	Status RenderPass_DirectLightingCacheReflection::Dispatch(TaskWorkingSet* tws, GraphicsAPI::DescriptorTable* sampler_descTable, GraphicsAPI::DescriptorTable* lightingCache_descTable, GraphicsAPI::CommandList* cmdList, RenderPass_ResourceRegistry* resources, const RenderTask::Task *traceTask)
	{
		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);
		auto& dev(pws->m_device);

		RenderTask::Task::Type type = traceTask->type;
		const RenderTask::TraceTaskCommon* common = nullptr;
		const RenderTask::DebugParameters* debugPrm = nullptr;
		const RenderTask::TraceSpecularTask* specularTask = nullptr;
		const RenderTask::TraceDiffuseTask* diffuseTask = nullptr;
		const RenderTask::TraceAmbientOcclusionTask* aoTask = nullptr;
		const RenderTask::TraceShadowTask* shadowTask = nullptr;
		const RenderTask::TraceMultiShadowTask* mShadowTask = nullptr;
		OutputType outputType;
		const RenderTask::UnorderedAccessTex* outTex = nullptr;
		const RenderTask::UnorderedAccessTex* outAuxTex = nullptr;

		switch (type) {
		case RenderTask::Task::Type::TraceSpecular:
			specularTask = static_cast<const RenderTask::TraceSpecularTask*>(traceTask);
			common = &specularTask->common;
			debugPrm = &specularTask->debugParameters;
			outputType = OutputType::Reflections;
			outTex = &specularTask->out;
			outAuxTex = &specularTask->outAux;
			break;
		case RenderTask::Task::Type::TraceDiffuse:
			diffuseTask = static_cast<const RenderTask::TraceDiffuseTask*>(traceTask);
			common = &diffuseTask->common;
			debugPrm = &diffuseTask->debugParameters;
			outputType = OutputType::GI;
			outTex = &diffuseTask->out;
			break;
		case RenderTask::Task::Type::TraceAmbientOcclusion:
			aoTask = static_cast<const RenderTask::TraceAmbientOcclusionTask*>(traceTask);
			common = &aoTask->common;
			debugPrm = &aoTask->debugParameters;
			outputType = OutputType::AO;
			outTex = &aoTask->out;
			break;
		case RenderTask::Task::Type::TraceShadow:
			shadowTask = static_cast<const RenderTask::TraceShadowTask*>(traceTask);
			common = &shadowTask->common;
			debugPrm = &shadowTask->debugParameters;
			outputType = OutputType::Shadow;
			outTex = &shadowTask->out;
			break;
		case RenderTask::Task::Type::TraceMultiShadow:
			mShadowTask = static_cast<const RenderTask::TraceMultiShadowTask*>(traceTask);
			common = &mShadowTask->common;
			debugPrm = &mShadowTask->debugParameters;
			outputType = OutputType::MultiShadow;
			outTex = &mShadowTask->out0;
			outAuxTex = &mShadowTask->out1;
			break;
		default:
			Log::Fatal(L"Ivaid taks type detected when validating a trace task.");
			return Status::ERROR_INTERNAL;
		}

		bool useInlineRT = common->useInlineRT;
		if (useInlineRT && (!m_enableInlineRaytracing)) {
			Log::Fatal(L"Inline raytracing is disabled at the SDK initialization.");
			return Status::ERROR_INVALID_PARAM;
		}
		if ((! useInlineRT) && (!m_enableShaderTableRaytracing)) {
			Log::Fatal(L"ShaderTable raytracing is disabled at the SDK initialization.");
			return Status::ERROR_INVALID_PARAM;
		}

		resources->TrackResource(common->roughness.tex, GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(common->envMap.tex, GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(common->inputMask.tex, GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(common->depth.tex, GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(common->normal.tex, GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(common->specular.tex, GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(common->roughness.tex, GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(common->directLighting, GraphicsAPI::ResourceState::State::ShaderResource);
		if (outTex)
			resources->TrackResource(*outTex, GraphicsAPI::ResourceState::State::UnorderedAccess);
		if (outAuxTex)
			resources->TrackResource(*outAuxTex, GraphicsAPI::ResourceState::State::UnorderedAccess);

		bool isDebugShader = debugPrm->debugOutputType != RenderTask::DebugParameters::DebugOutputType::Default;
#if defined(GRAPHICS_API_D3D12)
		bool isEnableSpecularTex = specularTask != nullptr && specularTask->out.resource != nullptr && common->specular.tex.resource != nullptr; 
		bool isEnableRoughnessTex = common->roughness.tex.resource != nullptr;
		bool isEnableEnvMapTex = common->envMap.tex.resource != nullptr;
		bool isOutAuxTex = outAuxTex != nullptr && outAuxTex->resource != nullptr;
		bool isInputMaskTex = common->inputMask.tex.resource != nullptr;
#elif defined(GRAPHICS_API_VK)
		bool isEnableSpecularTex = specularTask != nullptr && specularTask->out.image != nullptr && common->specular.tex.image != nullptr;
		bool isEnableRoughnessTex = common->roughness.tex.image != nullptr;
		bool isEnableEnvMapTex = common->envMap.tex.image != nullptr;
		bool isOutAuxTex = outAuxTex != nullptr && outAuxTex->image != nullptr;
		bool isInputMaskTex = common->inputMask.tex.image != nullptr;
#endif
		bool isEnableGIPass = diffuseTask != nullptr;
		bool isEnableRTAOPass = aoTask != nullptr;
		bool isEnableShadowsPass = shadowTask != nullptr || mShadowTask != nullptr;
		bool isDemodulateSpecular = specularTask != nullptr && specularTask->demodulateSpecular;
		bool isHalfResRendering = common->halfResolutionMode != RenderTask::HalfResolutionMode::OFF;
		bool useNormalizedDisneyDiffuse = diffuseTask != nullptr && diffuseTask->diffuseBRDFType == RenderTask::DiffuseBRDFType::NormalizedDisney;
		bool isShadowsEnableFirstHitAndEndSearch = shadowTask != nullptr && shadowTask->enableFirstHitAndEndSearch;
		isShadowsEnableFirstHitAndEndSearch |= mShadowTask != nullptr && mShadowTask->enableFirstHitAndEndSearch;
		bool isMultiShadow = mShadowTask != nullptr;

#if defined(GRAPHICS_API_D3D12)
		// Check input resource states.
		if (cmdList->HasDebugCommandList()) {
			{
				constexpr D3D12_RESOURCE_STATES expectedState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
				std::deque<std::tuple<const RenderTask::ShaderResourceTex*, std::wstring>> resArr;
				resArr.push_back({ &common->depth.tex , L"depth" });
				resArr.push_back({ &common->normal.tex , L"normal" });
				if (common->specular.tex.resource != nullptr)
					resArr.push_back({ &common->specular.tex , L"specular" });
				if (common->roughness.tex.resource != nullptr)
					resArr.push_back({ &common->roughness.tex , L"roughness" });
				if (common->envMap.tex.resource)
					resArr.push_back({ &common->envMap.tex , L"envMap" });
				if (common->inputMask.tex.resource != nullptr)
					resArr.push_back({ &common->inputMask.tex , L"inputMask" });
				if (common->directLighting.resource != nullptr)
					resArr.push_back({ &common->directLighting, L"directLighting" });

				for (auto&& r : resArr) {
					auto[rp, str] = r;
					if (!Utils::CheckInputTextureState(cmdList, rp, GraphicsAPI::ResourceState::GetResourceState(expectedState))) {
						Log::Fatal(L"Invalid \"%s\" texture's resource state detected in a trace task. Expected resource is : %d", str.c_str(), expectedState);
						return Status::ERROR_INVALID_PARAM;
					}
				}
			}
			{
				constexpr D3D12_RESOURCE_STATES expectedState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
				std::deque<std::tuple<const RenderTask::UnorderedAccessTex*, std::wstring>> resArr;
				resArr.push_back({ outTex, L"output" });
				if (outAuxTex != nullptr && outAuxTex->resource != nullptr)
					resArr.push_back({ outAuxTex, L"outAuxTex" });

				for (auto&& r : resArr) {
					auto [rp, str] = r;
					if (!Utils::CheckInputTextureState(cmdList, rp, GraphicsAPI::ResourceState::GetResourceState(expectedState))) {
						Log::Fatal(L"Invalid \"%s\" texture's resource state detected in a trace task. Expected resource is : %d", str.c_str(), expectedState);
						return Status::ERROR_INVALID_PARAM;
					}
				}
			}
		}
#endif

		uint32_t numLights = 0;
		if (shadowTask != nullptr)
			numLights = 1;
		else if (mShadowTask != nullptr)
			numLights = mShadowTask->numLights;

		uint32_t reflectionShaderPermutationIdx = 0;
		reflectionShaderPermutationIdx += isEnableSpecularTex ? (uint32_t)ReflectionShaderPermutationBits::e_ENABLE_SPECULAR_TEX : 0;
		reflectionShaderPermutationIdx += isEnableRoughnessTex ? (uint32_t)ReflectionShaderPermutationBits::e_ENABLE_ROUGHNESS_TEX : 0;
		reflectionShaderPermutationIdx += isEnableEnvMapTex ? (uint32_t)ReflectionShaderPermutationBits::e_ENABLE_ENV_MAP_TEX : 0;
		reflectionShaderPermutationIdx += isDemodulateSpecular ? (uint32_t)ReflectionShaderPermutationBits::e_DEMODULATE_SPECULAR : 0;
		reflectionShaderPermutationIdx += isHalfResRendering ? (uint32_t)ReflectionShaderPermutationBits::e_HALF_RESOLUTION : 0;
		reflectionShaderPermutationIdx += isInputMaskTex ? (uint32_t)ReflectionShaderPermutationBits::e_ENABLE_INPUT_MASK : 0;

		uint32_t GIShaderPermutationIdx = 0;
		GIShaderPermutationIdx += isEnableEnvMapTex ? (uint32_t)GIShaderPermutationBits::e_ENABLE_ENV_MAP_TEX : 0;
		GIShaderPermutationIdx += isEnableRoughnessTex ? (uint32_t)GIShaderPermutationBits::e_ENABLE_ROUGHNESS_TEX : 0;
		GIShaderPermutationIdx += isHalfResRendering ? (uint32_t)GIShaderPermutationBits::e_HALF_RESOLUTION : 0;
		GIShaderPermutationIdx += isInputMaskTex ? (uint32_t)GIShaderPermutationBits::e_ENABLE_INPUT_MASK : 0;
		GIShaderPermutationIdx += useNormalizedDisneyDiffuse ? (uint32_t)GIShaderPermutationBits::e_USE_NORMALIZED_DIFFUSE : 0;
		
		uint32_t AOShaderPermutationIdx = 0;
		AOShaderPermutationIdx += isHalfResRendering ? (uint32_t)AOShaderPermutationBits::e_HALF_RESOLUTION : 0;
		AOShaderPermutationIdx += isInputMaskTex ? (uint32_t)AOShaderPermutationBits::e_ENABLE_INPUT_MASK : 0;

		uint32_t ShadowsShaderPermutationIdx = 0;
		ShadowsShaderPermutationIdx += isHalfResRendering ? (uint32_t)ShadowsShaderPermutationBits::e_HALF_RESOLUTION : 0;
		ShadowsShaderPermutationIdx += isInputMaskTex ? (uint32_t)ShadowsShaderPermutationBits::e_ENABLE_INPUT_MASK : 0;
		ShadowsShaderPermutationIdx += isMultiShadow ? (uint32_t)ShadowsShaderPermutationBits::e_ENABLE_MULTI_SHADOW : 0;
		ShadowsShaderPermutationIdx += isShadowsEnableFirstHitAndEndSearch ? (uint32_t)ShadowsShaderPermutationBits::e_ENABLE_ACCEPT_FIRST_HIT_AND_END_SEARCH : 0;
		
		if (isDemodulateSpecular && !isOutAuxTex) {
			Log::Fatal(L"'isOutAuxTex' texture must be present when 'isDemodulateSpecular' is enabled!"); // Move this check...
			return Status::ERROR_INVALID_PARAM;
		}

		GraphicsAPI::ComputePipelineState* activeCSPSO = nullptr;
		ShaderTableRT* activeShaderTable = nullptr;
		if (isDebugShader) {
			activeCSPSO = useInlineRT ? m_pso_DebugVis->GetCSPSO(pws) : nullptr;
			activeShaderTable = useInlineRT ? nullptr : m_shaderTable_DebugVis->GetShaderTableRT(pws, cmdList);
		}
		else if (isEnableShadowsPass) {
			activeCSPSO = useInlineRT ? m_pso_Shadows[ShadowsShaderPermutationIdx]->GetCSPSO(pws) : nullptr;
			activeShaderTable = useInlineRT ? nullptr : m_shaderTable_Shadows[ShadowsShaderPermutationIdx]->GetShaderTableRT(pws, cmdList);
		}
		else if (isEnableRTAOPass) {
			activeCSPSO = useInlineRT ? m_pso_AO[AOShaderPermutationIdx]->GetCSPSO(pws) : nullptr;
			activeShaderTable = useInlineRT ? nullptr : m_shaderTable_AO[AOShaderPermutationIdx]->GetShaderTableRT(pws, cmdList);
		}
		else if (isEnableGIPass) {
			activeCSPSO = useInlineRT ? m_pso_GI[GIShaderPermutationIdx]->GetCSPSO(pws) : nullptr;
			activeShaderTable = useInlineRT ? nullptr : m_shaderTable_GI[GIShaderPermutationIdx]->GetShaderTableRT(pws, cmdList);
		}
		else {
			activeCSPSO = useInlineRT ? m_pso[reflectionShaderPermutationIdx]->GetCSPSO(pws) : nullptr;
			activeShaderTable = useInlineRT ? nullptr : m_shaderTable[reflectionShaderPermutationIdx]->GetShaderTableRT(pws, cmdList);
		}

		if (useInlineRT) {
			cmdList->SetComputePipelineState(activeCSPSO);
		}
		else {
			cmdList->SetRayTracingPipelineState(activeShaderTable->m_rtPSO.get());
		}

		GraphicsAPI::DescriptorTable descTable;
		if (!descTable.Allocate(tws->m_CBVSRVUAVHeap.get(), &m_descTableLayout2)) {
			Log::Fatal(L"Faild to allocate a portion of desc heap.");
			return Status::ERROR_INTERNAL;
		}

		uint32_t dispatchWidth = common->viewport.width;
		uint32_t dispatchHeight = common->viewport.height;

		if (isHalfResRendering) {
			dispatchWidth /= 2;
		}

		uint32_t CTA_Swizzle_GroupDimension_X = GraphicsAPI::ROUND_UP(dispatchWidth, m_threadDim_XY[0]);
		uint32_t CTA_Swizzle_GroupDimension_Y = GraphicsAPI::ROUND_UP(dispatchHeight, m_threadDim_XY[1]);

		GraphicsAPI::ConstantBufferView cbv;
		{
			void* cbPtrForWrite;
			RETURN_IF_STATUS_FAILED(tws->m_volatileConstantBuffer.Allocate(sizeof(CB), &cbv, &cbPtrForWrite));

			CB cb = {};
			cb.m_Viewport_TopLeftX = common->viewport.topLeftX;
			cb.m_Viewport_TopLeftY = common->viewport.topLeftY;
			cb.m_Viewport_Width = common->viewport.width;
			cb.m_Viewport_Height = common->viewport.height;

			cb.m_Viewport_MinDepth = common->viewport.minDepth;
			cb.m_Viewport_MaxDepth = common->viewport.maxDepth;

			{
				Math::Float_4 originf = Math::Transform(common->viewToWorldMatrix, { 0.f, 0.f, 0.f, 1.f });
				cb.m_rayOrigin[0] = originf.f[0] / originf.f[3];
				cb.m_rayOrigin[1] = originf.f[1] / originf.f[3];
				cb.m_rayOrigin[2] = originf.f[2] / originf.f[3];
			}
			if (debugPrm->debugOutputType == RenderTask::DebugParameters::DebugOutputType::Default)
				cb.m_outputType = (uint32_t)outputType;
			else {
				cb.m_outputType = (uint32_t)debugPrm->debugOutputType;
			}
			cb.m_depthType = (uint32_t)common->depth.type;

			GetNormalUnpackConstants(
				common->normal.type,
				cb.m_normalType,
				cb.m_normalNormalizationFactor[0],
				cb.m_normalNormalizationFactor[1],
				cb.m_normalChMask1,
				cb.m_normalChMask2);

			cb.m_envMapType = (uint32_t)common->envMap.type;

			if (debugPrm->randomNumberGenerator == RenderTask::DebugParameters::RandomNumberGenerator::Default)
				cb.m_randomNumberGeneratorType = (uint32_t)RenderTask::DebugParameters::RandomNumberGenerator::BlueNoiseTexture;
			else
				cb.m_randomNumberGeneratorType = (uint32_t)debugPrm->randomNumberGenerator;

			if (debugPrm->useFrameIndex == 0) {
				cb.m_frameIndex = (uint32_t)pws->GetCurrentTaskIndex();

				// Update global random number
				constexpr float eps = 0.0001f;
				std::uniform_real_distribution<float> uniformDistributionF(eps, 1.0f-eps);
				m_globalRandom_F = uniformDistributionF(m_randomGenerator);
				std::uniform_int_distribution<uint32_t> uniformDistributionU(0x0000'0000, 0xFFFF'FFFF);
				m_globalRandom_U = uniformDistributionU(m_randomGenerator);

				cb.m_globalRandom_F = m_globalRandom_F;
				cb.m_globalRandom_U = m_globalRandom_U;
			}
			else {
				cb.m_frameIndex = debugPrm->frameIndex;
				cb.m_globalRandom_F = 0.f;
				cb.m_globalRandom_U = 0u;
			}

			cb.m_globalRoughness = common->roughness.globalRoughness;
			cb.m_globalMetalness = common->specular.globalMetalness;
			cb.m_envMapIntensity = common->envMap.envMapIntensity;
			cb.m_aoRadius = aoTask != nullptr ? aoTask->aoRadius : cb.m_aoRadius;
			cb.m_invertHalfResCheckerboard = (common->halfResolutionMode == RenderTask::HalfResolutionMode::CHECKERBOARD_INVERTED);

			cb.m_roughnessMultiplier = common->roughness.roughnessMultiplier;
			cb.m_minRoughness = common->roughness.minRoughness;
			cb.m_maxRoughness = common->roughness.maxRoughness;

			cb.m_CTA_Swizzle_GroupDimension_X = CTA_Swizzle_GroupDimension_X;
			cb.m_CTA_Swizzle_GroupDimension_Y = CTA_Swizzle_GroupDimension_Y;

			cb.m_maxRayLength = common->maxRayLength;

			switch (common->rayOffset.type) {
			case RenderTask::RayOffset::Type::e_Disabled:
			default:
				cb.m_OffsetRay_Type = 0;
				cb.m_OffsetRay_CamDistance_Constant = 0.f;
				cb.m_OffsetRay_CamDistance_Linear = 0.f;
				cb.m_OffsetRay_CamDistance_Quadratic = 0.f;
				break;
			case RenderTask::RayOffset::Type::e_WorldPosition:
				cb.m_OffsetRay_Type = 1;
				cb.m_OffsetRay_WorldPosition_Threshold = common->rayOffset.worldPosition.threshold;
				cb.m_OffsetRay_WorldPosition_Float_Scale = common->rayOffset.worldPosition.floatScale;
				cb.m_OffsetRay_WorldPosition_Int_Scale = common->rayOffset.worldPosition.intScale;
				break;

			case RenderTask::RayOffset::Type::e_CamDistance:
				cb.m_OffsetRay_Type = 0;
				cb.m_OffsetRay_CamDistance_Constant = common->rayOffset.camDistance.constant;
				cb.m_OffsetRay_CamDistance_Linear = common->rayOffset.camDistance.linear;
				cb.m_OffsetRay_CamDistance_Quadratic = common->rayOffset.camDistance.quadratic;
				break;
			}

			cb.m_paddingf32_1 = 0.f;

			cb.m_roughnessMask = common->roughness.roughnessMask;

			// Convert vector to spherical coords. (Add some common math headers somehwere?)

			cb.m_numLights = numLights;

#if defined(GRAPHICS_API_D3D12)
			cb.m_enableLightTex = common->directLighting.resource != nullptr;
#elif defined(GRAPHICS_API_VK)
			cb.m_enableLightTex = common->directLighting.image != nullptr;
#endif
			cb.m_enableBilinearSampling = common->enableBilinearSampling;

			cb.m_clipToViewMatrix = common->clipToViewMatrix;
			cb.m_viewToClipMatrix = common->viewToClipMatrix;
			cb.m_viewToWorldMatrix = common->viewToWorldMatrix;
			cb.m_worldToViewMatrix = common->worldToViewMatrix;
			cb.m_normalToWorldMatrix = common->normal.normalToWorldMatrix;
			cb.m_worldToEnvMapMatrix = common->envMap.worldToEnvMapMatrix;

			memcpy(cbPtrForWrite, &cb, sizeof(cb));
		}

		GraphicsAPI::ConstantBufferView cbv2;
		if (numLights > 0)
		{
			constexpr uint32_t kMaxLightNum = 32;

			if (kMaxLightNum < numLights) {
				Log::Fatal(L"Light count (%d) exceeds maximum (%d)", numLights, kMaxLightNum);
				return Status::ERROR_INVALID_PARAM;
			}

			struct CB_Light {
				uint32_t m_type;
				Math::Float_3 m_dirVec;

				Math::Float_2 m_dir;
				float m_intensity;
				uint32_t m_pad;

				float m_angularExtent;
				float m_tanOfAngularExtent;
				uint32_t m_pad1[2];

				float m_radius;
				float m_range;
				float m_cosApexAngle;
				uint32_t m_pad2;

				Math::Float_3 m_pos;
				uint32_t m_pad3;
			};

			std::vector<CB_Light> lightInfos(numLights);

			for (uint32_t i = 0; i < numLights; ++i) {
				const RenderTask::LightInfo* light = nullptr;
				if (shadowTask != nullptr)
					light = &shadowTask->lightInfo;
				else if (mShadowTask != nullptr)
					light = mShadowTask->lightInfos + i;

				if (light->type == RenderTask::LightInfo::Type::Directional) {

					const auto& directionalLight(light->dir);
					// Normalize shadow dir vec.
					float x = directionalLight.dir.f[0];
					float y = directionalLight.dir.f[1];
					float z = directionalLight.dir.f[2];

					const float r2 = x * x + y * y + z * z;
					if (r2 <= 1e-6f) {
						Log::Fatal(L"Unexpected shadow vector length");
						return Status::ERROR_INVALID_PARAM;
					}
					const float r = std::sqrt(r2);

					x /= r;
					y /= r;
					z /= r;

					const float theta = std::acos(z /*/ r == 1.f*/);
					const float phi = std::atan2(y, x);

					lightInfos[i].m_type = (uint32_t)light->type;
					lightInfos[i].m_dirVec.f[0] = x;
					lightInfos[i].m_dirVec.f[1] = y;
					lightInfos[i].m_dirVec.f[2] = z;

					lightInfos[i].m_dir.f[0] = theta;
					lightInfos[i].m_dir.f[1] = phi;
					lightInfos[i].m_intensity = directionalLight.intensity;

					lightInfos[i].m_angularExtent = directionalLight.angularExtent;
					lightInfos[i].m_tanOfAngularExtent = tan(directionalLight.angularExtent);

				}
				else if (light->type == RenderTask::LightInfo::Type::Spot) {
					const auto& spotlight(light->spot);

					// Normalize shadow dir vec.
					float x = spotlight.dir.f[0];
					float y = spotlight.dir.f[1];
					float z = spotlight.dir.f[2];

					const float r2 = x * x + y * y + z * z;
					if (r2 <= 1e-6f) {
						Log::Fatal(L"Unexpected shadow vector length");
						return Status::ERROR_INVALID_PARAM;
					}
					const float r = std::sqrt(r2);

					x /= r;
					y /= r;
					z /= r;

					const float theta = std::acos(z /*/ r == 1.f*/);
					const float phi = std::atan2(y, x);

					lightInfos[i].m_type = (uint32_t)light->type;
					lightInfos[i].m_dirVec.f[0] = x;
					lightInfos[i].m_dirVec.f[1] = y;
					lightInfos[i].m_dirVec.f[2] = z;

					lightInfos[i].m_dir.f[0] = theta;
					lightInfos[i].m_dir.f[1] = phi;
					lightInfos[i].m_intensity = spotlight.intensity;

					lightInfos[i].m_radius = spotlight.radius;
					lightInfos[i].m_range = spotlight.range;
					lightInfos[i].m_cosApexAngle = std::cos(spotlight.apexAngle);

					lightInfos[i].m_pos = spotlight.pos;
				}
				else if (light->type == RenderTask::LightInfo::Type::Point) {
					const auto &point(light->point);
					lightInfos[i].m_type = (uint32_t)light->type;
					lightInfos[i].m_intensity = point.intensity;
					lightInfos[i].m_radius = point.radius;
					lightInfos[i].m_range = point.range;
					lightInfos[i].m_pos = point.pos;
				}
			}

			void* cbPtrForWrite;
			RETURN_IF_STATUS_FAILED(tws->m_volatileConstantBuffer.Allocate(numLights * sizeof(CB_Light), &cbv2, &cbPtrForWrite));

			memcpy(cbPtrForWrite, lightInfos.data(), numLights * sizeof(CB_Light));
		}
		else {
			void* cbPtrForWrite;
			RETURN_IF_STATUS_FAILED(tws->m_volatileConstantBuffer.Allocate(80, &cbv2, &cbPtrForWrite));
			memset(cbPtrForWrite, 0, 80);
		}

		{
			descTable.SetSrv(&dev, 0, 0, &m_blueNoiseTexSRV); // Layout2 : 0, Range:0, RangeIdx:0 BNTex SRV
			
			descTable.SetSrv(&dev, 1, 0, pws->m_nullBufferSRV.get());	// Layout2 : 1, Range:1, RangeIdx:0 unused.
		}
		{
			descTable.SetCbv(&dev, 2, 0, &cbv); // Layout2 : 2, Range:2, RangeIdx:0 constants CBV 
		}
		{
			descTable.SetCbv(&dev, 3, 0, &cbv2); // Layout3 : 3, Range:2, RangeIdx:0 constants CBV 
		}

		RenderPass_ResourceStateTransition stateTransitions;

		{
			using SRV = GraphicsAPI::ShaderResourceView;
			using UAV = GraphicsAPI::UnorderedAccessView;

			std::unique_ptr<SRV> depthSRV		= resources->GetSRV(common->depth.tex, stateTransitions, GraphicsAPI::ResourceState::State::ShaderResource);
			std::unique_ptr<SRV> normalSRV		= resources->GetSRV(common->normal.tex, stateTransitions, GraphicsAPI::ResourceState::State::ShaderResource);
			std::unique_ptr<SRV> specSRV		= resources->GetSRV(common->specular.tex, stateTransitions, GraphicsAPI::ResourceState::State::ShaderResource);
			std::unique_ptr<SRV> roughnessSRV	= resources->GetSRV(common->roughness.tex, stateTransitions, GraphicsAPI::ResourceState::State::ShaderResource);
			std::unique_ptr<SRV> envSRV			= resources->GetSRV(common->envMap.tex, stateTransitions, GraphicsAPI::ResourceState::State::ShaderResource);
			std::unique_ptr<SRV> inputMaskSRV	= resources->GetSRV(common->inputMask.tex, stateTransitions, GraphicsAPI::ResourceState::State::ShaderResource);
			std::unique_ptr<SRV> lightingSRV	= resources->GetSRV(common->directLighting, stateTransitions, GraphicsAPI::ResourceState::State::ShaderResource);
			
			std::unique_ptr<UAV> outUAV			= resources->GetUAV(*outTex, stateTransitions, GraphicsAPI::ResourceState::State::UnorderedAccess);
			std::unique_ptr<UAV> outAuxUAV		= outAuxTex ? resources->GetUAV(*outAuxTex, stateTransitions, GraphicsAPI::ResourceState::State::UnorderedAccess) : nullptr;

			descTable.SetSrv(&dev, 4, 0, depthSRV.get()); // Layout2: 3, Range:3, RangeIdx:0 depth SRV
			descTable.SetSrv(&dev, 5, 0, normalSRV.get()); // Layout2: 4, Range:4, RangeIdx:0 normal SRV

			if (specSRV.get())
				descTable.SetSrv(&dev, 6, 0, specSRV.get()); // Layout2: 5, Range:5, RangeIdx:0 specular SRV
			else
				descTable.SetSrv(&dev, 6, 0, pws->m_nullTexture2DSRV.get());

			if (roughnessSRV.get())
				descTable.SetSrv(&dev, 7, 0, roughnessSRV.get()); // Layout2: 6, Range:6, RangeIdx:0 roughness SRV
			else
				descTable.SetSrv(&dev, 7, 0, pws->m_nullTexture2DSRV.get()); // Layout2: 6, Range:6, RangeIdx:0 roughness SRV

			if (envSRV.get())
				descTable.SetSrv(&dev, 8, 0, envSRV.get()); // Layout2: 7, Range:7, RangeIdx:0 envMap SRV
			else
				descTable.SetSrv(&dev, 8, 0, pws->m_nullTexture2DSRV.get()); // Layout2: 7, Range:7, RangeIdx:0 envMap SRV

			if (inputMaskSRV.get())
				descTable.SetSrv(&dev, 9, 0, inputMaskSRV.get()); // Layout2: 8, Range:8, RangeIdx:0 envMap SRV
			else
				descTable.SetSrv(&dev, 9, 0, pws->m_nullTexture2DSRV.get()); // Layout2: 8, Range:8, RangeIdx:0 inputMask SRV

			if (lightingSRV.get())
				descTable.SetSrv(&dev, 10, 0, lightingSRV.get()); // Layout2: 8, Range:8, RangeIdx:0 envMap SRV
			else
				descTable.SetSrv(&dev, 10, 0, pws->m_nullTexture2DSRV.get()); // Layout2: 8, Range:8, RangeIdx:0 inputMask SRV

			descTable.SetUav(&dev, 11, 0, outUAV.get()); // Layout2: 9, Range:9, RangeIdx:0 output UAV

			if (outAuxUAV.get())
				descTable.SetUav(&dev, 12, 0, outAuxUAV.get()); // Layout2: 10, Range:10, RangeIdx:0 output specular reflectance
			else
				descTable.SetUav(&dev, 12, 0, pws->m_nullTexture2DUAV.get()); // Layout2: 10, Range:10, RangeIdx:0 output specular reflectance

			pws->DeferredRelease(std::move(depthSRV));
			pws->DeferredRelease(std::move(normalSRV));
			pws->DeferredRelease(std::move(specSRV));
			pws->DeferredRelease(std::move(roughnessSRV));
			pws->DeferredRelease(std::move(envSRV));
			pws->DeferredRelease(std::move(inputMaskSRV));
			pws->DeferredRelease(std::move(lightingSRV));

			pws->DeferredRelease(std::move(outUAV));
			pws->DeferredRelease(std::move(outAuxUAV));
		}

		stateTransitions.Flush(cmdList);

		{
			std::vector<GraphicsAPI::DescriptorTable*>	descTables = { sampler_descTable, lightingCache_descTable, &descTable };
			if (useInlineRT) {
				cmdList->SetComputeRootDescriptorTable(&m_rootSignature, 0, descTables.data(), descTables.size());
				cmdList->Dispatch(CTA_Swizzle_GroupDimension_X, CTA_Swizzle_GroupDimension_Y, 1);
			}
			else {
				cmdList->SetRayTracingRootDescriptorTable(&m_rootSignature, 0, descTables.data(), descTables.size());
				activeShaderTable->DispatchRays(cmdList, common->viewport.width, common->viewport.height);
			}
		}

		return Status::OK;
	}

	Status RenderPass_DirectLightingCacheReflection::BuildCommandList(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList,
		RenderPass_ResourceRegistry* resources,
		GraphicsAPI::DescriptorTable *lightingCache_descTable,
		const RenderTask::Task *traceTask)
	{
		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);

		// copy blue noise texture here at the first time. 
		if (! m_blueNoiseTextureIsReady) {
			{
				// To copy uploaded texture, In D3D12, upload heap resource need to be GENERIC_READ state, so, D3D12 doesn't need any state transision for upload buffer.
				// In Vulkan, upload heap also doesn't need any state transition.
				// As for destination texutre, In D3D12, CopyTextureSingleMip do state transition Undefined(COMMON) -> COPY_DEST -> (Copy) -> SHADER_RESOURCE
				// In VK, CopyTextureSingleMip do image layout transision, Undefined -> VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL -> (Copy) -> VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				if (!cmdList->CopyTextureSingleMip(&pws->m_device, 0, m_blueNoiseTex.get(), m_blueNoiseTexUpBuf.get())) {
					Log::Fatal(L"Failed to set up commands for copy texture single mip");
					return Status::ERROR_INTERNAL;
				}
			}
			pws->DeferredRelease(std::move(m_blueNoiseTexUpBuf));

			m_blueNoiseTextureIsReady = true;
		}

		GraphicsAPI::DescriptorTable sampler_descTable;
		if (!sampler_descTable.Allocate(tws->m_CBVSRVUAVHeap.get(), &m_descTableLayout0)) {
			Log::Fatal(L"Faild to allocate a portion of desc heap.");
			return Status::ERROR_INTERNAL;
		}

		sampler_descTable.SetSampler(&pws->m_device, 0, 0, m_linearClampSampler.get());	// Layout0 : 0

		cmdList->SetComputeRootSignature(&m_rootSignature);

		{
			GraphicsAPI::Utils::ScopedEventObject sce(cmdList, { 0, 128, 0 }, DebugName("TraceTask"));
			RETURN_IF_STATUS_FAILED(Dispatch(tws,  &sampler_descTable, lightingCache_descTable, cmdList, resources, traceTask));
		}

		return Status::OK;
	}
};
