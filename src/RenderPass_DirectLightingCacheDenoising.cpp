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
#include <RenderPass_DirectLightingCacheDenoising.h>
#include <RenderPass_Common.h>
#include <common/ShaderBlob.h>
#include <Utils.h>
#include <Scene.h>
#include <TaskWorkingSet.h>
#include <WinResFS.h>
#include <ShaderFactory.h>
#include <math.h> 

#if (KickstartRT_SDK_WITH_NRD)
#include "NRD.h"
#include "NRDDescs.h"
#include "NRDSettings.h"
#endif

#define RETURN_IF_STATUS_FAILED_NRD(x) do {				\
if (x != nrd::Result::SUCCESS)							\
	return Status::ERROR_INTERNAL;						\
} while (false)

#define NOT_IMPLEMENTED_FATAL(fmt, ...)		do { Log::Fatal(L"NOT IMPLEMENTED "  fmt, ## __VA_ARGS__); } while(false)
#define NOT_IMPLEMENTED_WARNING(fmt, ...)	do { Log::Warning(L"NOT IMPLEMENTED "  fmt, ## __VA_ARGS__); } while(false)

namespace KickstartRT_NativeLayer
{
	namespace RenderTask {
		Status DenoisingOutput::ConvertFromRenderTask(const RenderTask::Task* task)	{
			auto CopyFromDenoiseCommon = [this](const RenderTask::DenoisingTaskCommon& common) {
				mode = (RenderTask::DenoisingOutput::Mode)common.mode;
				viewport = common.viewport;
				depth = common.depth;
				normal = common.normal;
				roughness = common.roughness;
				motion = common.motion;
				if (common.debugDisableMotion)
					motion.tex = ShaderResourceTex();
				else
					motion = common.motion;

				clipToViewMatrix = common.clipToViewMatrix;
				halfResolutionMode = common.halfResolutionMode;
				viewToClipMatrix = common.viewToClipMatrix;
				viewToClipMatrixPrev = common.viewToClipMatrixPrev;
				worldToViewMatrix = common.worldToViewMatrix;
				worldToViewMatrixPrev = common.worldToViewMatrixPrev;
				cameraJitter = common.cameraJitter;
			};

			if (task->type == RenderTask::Task::Type::DenoiseSpecular) {
				auto& dSpec(*static_cast<const RenderTask::DenoiseSpecularTask*>(task));
				CopyFromDenoiseCommon(dSpec.common);
				context = dSpec.context;
				inOutSpecular = dSpec.inOutSpecular;
				inSpecular = dSpec.inSpecular;
			}
			else if (task->type == RenderTask::Task::Type::DenoiseDiffuse) {
				auto& dDiff(*static_cast<const RenderTask::DenoiseDiffuseTask*>(task));
				CopyFromDenoiseCommon(dDiff.common);
				context = dDiff.context;
				inOutDiffuse = dDiff.inOutDiffuse;
				inDiffuse = dDiff.inDiffuse;
			}
			else if (task->type == RenderTask::Task::Type::DenoiseSpecularAndDiffuse) {
				auto& dSpecDiff(*static_cast<const RenderTask::DenoiseSpecularAndDiffuseTask*>(task));
				CopyFromDenoiseCommon(dSpecDiff.common);
				context = dSpecDiff.context;
				inOutDiffuse = dSpecDiff.inOutDiffuse;
				inDiffuse = dSpecDiff.inDiffuse;
				inOutSpecular = dSpecDiff.inOutSpecular;
				inSpecular = dSpecDiff.inSpecular;
			}
			else if (task->type == RenderTask::Task::Type::DenoiseDiffuseOcclusion) {
				auto& dDiffOccl(*static_cast<const RenderTask::DenoiseDiffuseOcclusionTask*>(task));
				CopyFromDenoiseCommon(dDiffOccl.common);
				context = dDiffOccl.context;
				occlusionHitTMask = dDiffOccl.hitTMask;
				inHitT = dDiffOccl.inHitT;
				inOutOcclusion = dDiffOccl.inOutOcclusion;
			}
			else if (task->type == RenderTask::Task::Type::DenoiseShadow) {
				auto& dShadow(*static_cast<const RenderTask::DenoiseShadowTask*>(task));
				CopyFromDenoiseCommon(dShadow.common);
				context = dShadow.context;
				inShadow0 = dShadow.inShadow;
				inOutShadow = dShadow.inOutShadow;
			}
			else if (task->type == RenderTask::Task::Type::DenoiseMultiShadow) {
				auto& dShadow(*static_cast<const RenderTask::DenoiseMultiShadowTask*>(task));
				CopyFromDenoiseCommon(dShadow.common);
				context = dShadow.context;
				inShadow0 = dShadow.inShadow0;
				inShadow1 = dShadow.inShadow1;
				inOutShadow = dShadow.inOutShadow;
			}
			else {
				return Status::ERROR_INTERNAL;
			}
			return Status::OK;
		};
	};

#if (KickstartRT_SDK_WITH_NRD)

	static std::pair<Status, ShaderFactory::ShaderDictEntry *> RegisterShader(
		ShaderFactory::Factory *sf, 
		const std::wstring& fileName, const std::wstring& entryName, const std::wstring& shaderName,
		ShaderFactory::ShaderType::Enum type, const std::vector<ShaderFactory::ShaderMacro>& shaderMacro, GraphicsAPI::RootSignature& rootSig)
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
			return { Status::ERROR_FAILED_TO_INIT_RENDER_PASS, nullptr };
		}
		dictEnt->m_offset = ofsSize.value().first;
		dictEnt->m_size = ofsSize.value().second;

		dictEnt->CalcCRC();

		return sf->RegisterShader(std::move(dictEnt));
	};


	struct RenderPass_NRDConvertInputs
	{
		std::unique_ptr<GraphicsAPI::RootSignature>							m_rootSignature;
		std::unique_ptr<GraphicsAPI::DescriptorTableLayout>					m_descTableLayout;
		ShaderFactory::ShaderDictEntry*										m_pso = nullptr;
	public:
		Status Init(PersistentWorkingSet* pws, ShaderFactory::Factory* sf) {

			// CBV/SRV/UAV descriptor table
			{
				m_descTableLayout = std::make_unique<GraphicsAPI::DescriptorTableLayout>();
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::Cbv, 0, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 0, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 1, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 2, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 3, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 4, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 5, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureUav, 0, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureUav, 1, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureUav, 2, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureUav, 3, 1, 0);
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureUav, 4, 1, 0);

				if (!m_descTableLayout->SetAPIData(&pws->m_device)) {
					Log::Fatal(L"Failed to set apiData for descriptor table layout.");
					return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
				}

				m_rootSignature = std::make_unique<GraphicsAPI::RootSignature>();
				std::vector<GraphicsAPI::DescriptorTableLayout*> tableLayouts = { m_descTableLayout.get() };
				if (!m_rootSignature->Init(&pws->m_device, tableLayouts)) {
					Log::Fatal(L"Failed to create rootSignature");
					return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
				}
				m_rootSignature->SetName(DebugName(L"RP_NRDConvertInputs"));
			}

			{
				std::filesystem::path csPath(L"Denoising/NRD/ConversionLayer_CS.hlsl");
				std::vector<ShaderFactory::ShaderMacro> defines;
				auto [sts, ptr] = RegisterShader(
					sf,
					csPath.wstring(), L"main", DebugName(L"RP_NRDConvertInputs"),
					ShaderFactory::ShaderType::Enum::SHADER_COMPUTE, defines, *m_rootSignature.get());
				if (sts != Status::OK) {
					Log::Fatal(L"Faild to register shader: %s", csPath.wstring().c_str());
					return Status::ERROR_FAILED_TO_INIT_RENDER_PASS;
				}

				m_pso = ptr;
			}

			return Status::OK;
		}

		Status DeferredRelease(PersistentWorkingSet* pws) {
			if (pws != nullptr) {
				pws->DeferredRelease(std::move(m_rootSignature));
				pws->DeferredRelease(std::move(m_descTableLayout));
			}
			else {
				m_rootSignature.reset();
				m_descTableLayout.reset();
			}
			m_pso = nullptr;
			return Status::OK;
		}

		Status BuildCommandList(TaskWorkingSet* tws,
			GraphicsAPI::CommandList* cmdList,
			RenderPass_ResourceRegistry* resourceContext,
			const DenoisingContextInput& context,
			const RenderTask::DenoisingOutput& output,
			const std::array<GraphicsAPI::Texture*, (size_t)nrd::ResourceType::MAX_NUM>& resources) {

			GraphicsAPI::DescriptorTable descTable;
			PersistentWorkingSet* pws(tws->m_persistentWorkingSet);
			auto& dev(pws->m_device);

			GraphicsAPI::Resource* roughness = resourceContext->GetResource(output.roughness.tex);

			RenderPass_ResourceStateTransition stateTransitions;

			{
				{ // Desctriptor Table
					if (!descTable.Allocate(&tws->m_CBVSRVUAVHeap, m_descTableLayout.get())) {
						Log::Fatal(L"Failed to allocate a portion of desc heap.");
						return Status::ERROR_INTERNAL;
					}

					struct CB_NRDConvertInputs
					{
						uint32_t method;
						uint32_t signalType;
						uint32_t pad1;
						uint32_t pad2;

						uint32_t depthType;
						uint32_t pad3;
						uint32_t pad4;
						uint32_t pad5;

						float   viewport_MaxDepth;
						float   viewport_MinDepth;
						uint32_t   viewport_Width;
						uint32_t   viewport_Height;

						uint32_t enableRoughnessTex;
						float globalRoughness;
						uint32_t pad6;
						uint32_t pad7;

						float roughnessMultiplier;
						float minRoughness;
						float maxRoughness;
						uint32_t pad8;

						Math::Float_4 roughnessMask;
						Math::Float_4 hitTMask;

						float		metersToUnitsMultiplier;
						uint32_t    pad9;
						uint32_t    pad10;
						uint32_t    pad11;

						float		tanOfLightAngularRadius;
						uint32_t	normalType;
						float		normalNormalizationFactor[2];

						Math::Float_4  normalChMask1;
						Math::Float_4  normalChMask2;

						Math::Float_4x4 normalToWorldMatrix;

						Math::Float_4 nrdHitDistanceParameters;

						Math::Float_4x4 worldToViewMatrix;
						Math::Float_4x4 clipToViewMatrix;
					} cbuffer;

					cbuffer.method = (uint32_t)context.denoisingMethod;
					cbuffer.signalType = (uint32_t)context.signalType;

					cbuffer.depthType = (uint32_t)output.depth.type;
					cbuffer.pad1	= 0;
					cbuffer.pad2	= 0;
					cbuffer.pad3	= 0;

					cbuffer.viewport_MaxDepth = output.viewport.maxDepth;
					cbuffer.viewport_MinDepth = output.viewport.minDepth;
					cbuffer.viewport_Width = output.viewport.width;
					cbuffer.viewport_Height = output.viewport.height;

					cbuffer.enableRoughnessTex = roughness ? 1 : 0;
					cbuffer.globalRoughness = output.roughness.globalRoughness;
					cbuffer.pad2 = 0;
					cbuffer.pad3 = 0;

					cbuffer.roughnessMultiplier = output.roughness.roughnessMultiplier;
					cbuffer.minRoughness = output.roughness.minRoughness;
					cbuffer.maxRoughness = output.roughness.maxRoughness;
					cbuffer.pad4 = 0;

					cbuffer.roughnessMask = output.roughness.roughnessMask;
					cbuffer.hitTMask = output.occlusionHitTMask;

					cbuffer.metersToUnitsMultiplier = 1;

					if (output.shadow.numLights == 1)
						cbuffer.tanOfLightAngularRadius = std::tan(output.shadow.lightInfos[0].dir.angularExtent);
					else
						cbuffer.tanOfLightAngularRadius = 0.f;

					cbuffer.normalType = (uint32_t)output.normal.type;

					GetNormalUnpackConstants(
						output.normal.type,
						cbuffer.normalType,
						cbuffer.normalNormalizationFactor[0],
						cbuffer.normalNormalizationFactor[1],
						cbuffer.normalChMask1,
						cbuffer.normalChMask2);

					cbuffer.normalToWorldMatrix = output.normal.normalToWorldMatrix;

					nrd::HitDistanceParameters hitDistanceParameters = {};
					cbuffer.nrdHitDistanceParameters = reinterpret_cast<Math::Float_4&>(hitDistanceParameters);

					cbuffer.worldToViewMatrix = output.worldToViewMatrix;
					cbuffer.clipToViewMatrix = output.clipToViewMatrix;

					GraphicsAPI::ConstantBufferView cbv;
					void* cbPtrForWrite;
					RETURN_IF_STATUS_FAILED(tws->m_volatileConstantBuffer.Allocate(sizeof(cbuffer), &cbv, &cbPtrForWrite));
					memcpy(cbPtrForWrite, &cbuffer, sizeof(cbuffer));

					descTable.SetCbv(&dev, 0, 0, &cbv);

					auto BindSrv = [&pws, &dev, &descTable, resourceContext, &stateTransitions](const RenderTask::ShaderResourceTex& tex, GraphicsAPI::ResourceState::State state, uint32_t slot)->void {
						std::unique_ptr<GraphicsAPI::ShaderResourceView> srv = resourceContext->GetSRV(tex, stateTransitions, state);
						if (srv.get()) {
							descTable.SetSrv(&dev, slot, 0, srv.get());
							pws->DeferredRelease(std::move(srv));
						}
						else {
							descTable.SetSrv(&dev, slot, 0, pws->m_nullTexture2DSRV.get());
						}
					};

					auto BindUav = [&pws, &dev, &descTable, &stateTransitions](GraphicsAPI::Texture* resource, GraphicsAPI::ResourceState::State state, uint32_t slot)->void {
						std::unique_ptr<GraphicsAPI::UnorderedAccessView> uav = std::make_unique<GraphicsAPI::UnorderedAccessView>();
						uav->Init(&dev, resource);
						descTable.SetUav(&dev, slot, 0, uav.get());
						pws->DeferredRelease(std::move(uav));

						stateTransitions.RequestState(resource, state);
					};

					BindSrv(output.depth.tex,		GraphicsAPI::ResourceState::State::ShaderResource, 1);
					BindSrv(output.normal.tex,		GraphicsAPI::ResourceState::State::ShaderResource, 2);
					BindSrv(output.roughness.tex,	GraphicsAPI::ResourceState::State::ShaderResource, 3);
					BindSrv(output.inSpecular,		GraphicsAPI::ResourceState::State::ShaderResource, 4);
					BindSrv(output.inDiffuse,		GraphicsAPI::ResourceState::State::ShaderResource, 5);
					BindSrv(output.inHitT,			GraphicsAPI::ResourceState::State::ShaderResource, 6);

					if (resources[(uint32_t)nrd::ResourceType::IN_VIEWZ])
						BindUav(resources[(uint32_t)nrd::ResourceType::IN_VIEWZ], GraphicsAPI::ResourceState::State::UnorderedAccess, 7);
					else
						descTable.SetUav(&dev, 7, 0, pws->m_nullTexture2DUAV.get());

					if (resources[(uint32_t)nrd::ResourceType::IN_NORMAL_ROUGHNESS])
						BindUav(resources[(uint32_t)nrd::ResourceType::IN_NORMAL_ROUGHNESS], GraphicsAPI::ResourceState::State::UnorderedAccess, 8);
					else
						descTable.SetUav(&dev, 8, 0, pws->m_nullTexture2DUAV.get());

					if (resources[(uint32_t)nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST])
						BindUav(resources[(uint32_t)nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST], GraphicsAPI::ResourceState::State::UnorderedAccess, 9);
					else
						descTable.SetUav(&dev, 9, 0, pws->m_nullTexture2DUAV.get());

					if (resources[(uint32_t)nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST])
						BindUav(resources[(uint32_t)nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST], GraphicsAPI::ResourceState::State::UnorderedAccess, 10);
					else
						descTable.SetUav(&dev, 10, 0, pws->m_nullTexture2DUAV.get());

					if (resources[(uint32_t)nrd::ResourceType::IN_DIFF_HITDIST])
						BindUav(resources[(uint32_t)nrd::ResourceType::IN_DIFF_HITDIST], GraphicsAPI::ResourceState::State::UnorderedAccess, 11);
					else
						descTable.SetUav(&dev, 11, 0, pws->m_nullTexture2DUAV.get());
				}

				stateTransitions.Flush(cmdList);

				std::vector<GraphicsAPI::DescriptorTable*> tableArr{ &descTable };

				cmdList->SetComputeRootSignature(m_rootSignature.get());
				cmdList->SetComputeRootDescriptorTable(m_rootSignature.get(), 0, tableArr.data(), tableArr.size());
				cmdList->SetComputePipelineState(m_pso->GetCSPSO(pws));

				uint32_t gridWidth = GraphicsAPI::ROUND_UP(output.viewport.width, 8u);
				uint32_t gridHeight = GraphicsAPI::ROUND_UP(output.viewport.height, 16u);

				cmdList->Dispatch(gridWidth, gridHeight, 1);
			}

			return Status::OK;
		}
	};

	class RenderPass_NRDDenoising
	{
		nrd::Denoiser* m_denoiser = nullptr;

		struct Sampler
		{
			std::unique_ptr<GraphicsAPI::Sampler> sampler;
			int registerIndex;
		};

		DenoisingContextInput														m_context;
		uint32_t																				m_frameIndex = 0;

		std::unique_ptr<GraphicsAPI::RootSignature>												m_rootSignature;
		std::unique_ptr<GraphicsAPI::DescriptorTableLayout>										m_descTableLayout;
		std::unique_ptr<GraphicsAPI::DescriptorTableLayout>										m_samplerTableLayout;
		std::vector<std::unique_ptr<GraphicsAPI::ComputePipelineState>>							m_psos;
		std::vector<std::unique_ptr<GraphicsAPI::Texture>>										m_resources;
		std::array<std::unique_ptr<GraphicsAPI::Texture>, (size_t)nrd::ResourceType::MAX_NUM>	m_namedResources;
		std::vector<Sampler>																	m_samplers;
		uint32_t																				m_transientPoolSize = 0;

		RenderPass_NRDConvertInputs																m_nrdConvertInputs;

		GraphicsAPI::Texture* ResolveNrdResource(
			PersistentWorkingSet* pws,
			const nrd::Resource& nrdResource,
			RenderPass_ResourceRegistry* resource,
			const RenderTask::DenoisingOutput& output,
			GraphicsAPI::ShaderResourceView* srv,
			GraphicsAPI::UnorderedAccessView* uav);

		static nrd::CheckerboardMode GetCheckerboardMode(const RenderTask::DenoisingOutput& reflectionOutputs, uint64_t frameIndex);
		Status UpdateSettings(const RenderTask::DenoisingOutput& reflectionOutputs, uint64_t frameIndex);

	public:
		Status Init(PersistentWorkingSet* pws, const DenoisingContextInput& input, ShaderFactory::Factory* sf);
		Status DeferredRelease(PersistentWorkingSet* pws);
		Status BuildCommandList(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList, RenderPass_ResourceRegistry* resources, const RenderTask::DenoisingOutput& reflectionOutputs);
	};

	static const wchar_t* GetResourceName(nrd::ResourceType resourceType) {
		switch (resourceType) {
			case nrd::ResourceType::IN_MV:						return L"IN_MV";
			case nrd::ResourceType::IN_NORMAL_ROUGHNESS:		return L"IN_NORMAL_ROUGHNESS";
			case nrd::ResourceType::IN_VIEWZ:					return L"IN_VIEWZ";
			case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:	return L"IN_DIFF_RADIANCE_HITDIST";
			case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:	return L"IN_SPEC_RADIANCE_HITDIST";
			case nrd::ResourceType::IN_DIFF_HITDIST:			return L"IN_DIFF_HITDIST";
			case nrd::ResourceType::IN_SPEC_HITDIST:			return L"IN_SPEC_HITDIST";
			case nrd::ResourceType::IN_DIFF_DIRECTION_PDF:		return L"IN_DIFF_DIRECTION_PDF";
			case nrd::ResourceType::IN_SPEC_DIRECTION_PDF:		return L"IN_SPEC_DIRECTION_PDF";
			case nrd::ResourceType::IN_DIFF_CONFIDENCE:			return L"IN_DIFF_CONFIDENCE";
			case nrd::ResourceType::IN_SPEC_CONFIDENCE:			return L"IN_SPEC_CONFIDENCE";
			case nrd::ResourceType::IN_SHADOWDATA:				return L"IN_SHADOWDATA";
			case nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY:	return L"OUT_SHADOW_TRANSLUCENCY";
			case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:	return L"OUT_DIFF_RADIANCE_HITDIST";
			case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:	return L"OUT_SPEC_RADIANCE_HITDIST";
			case nrd::ResourceType::OUT_DIFF_HITDIST:			return L"OUT_DIFF_HITDIST";
			case nrd::ResourceType::OUT_SPEC_HITDIST:			return L"OUT_SPEC_HITDIST";
			case nrd::ResourceType::TRANSIENT_POOL:				return L"TRANSIENT_POOL";
			case nrd::ResourceType::PERMANENT_POOL:				return L"PERMANENT_POOL";
			default:
			{
				assert(!"Unknown resource type");
				return L"Unknown";
			}
		}
	}

	static nrd::Method GetNrdMethodForDenoisingContext(const DenoisingContextInput& context) {

		if (context.denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Reblur) {
			switch (context.signalType) {
			case DenoisingContextInput::SignalType::Specular:					return nrd::Method::REBLUR_SPECULAR;
			case DenoisingContextInput::SignalType::Diffuse:					return nrd::Method::REBLUR_DIFFUSE;
			case DenoisingContextInput::SignalType::SpecularAndDiffuse:			return nrd::Method::REBLUR_DIFFUSE_SPECULAR;
			case DenoisingContextInput::SignalType::DiffuseOcclusion:			return nrd::Method::REBLUR_DIFFUSE_OCCLUSION;
			default: {
				assert(!"Unknown signal type");
				return nrd::Method(0);
				}
			}
		}

		if (context.denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Relax) {
			switch (context.signalType) {
			case DenoisingContextInput::SignalType::Specular:				return nrd::Method::RELAX_SPECULAR;
			case DenoisingContextInput::SignalType::Diffuse:				return nrd::Method::RELAX_DIFFUSE;
			case DenoisingContextInput::SignalType::SpecularAndDiffuse:		return nrd::Method::RELAX_DIFFUSE_SPECULAR;
			default: {
				assert(!"Unknown signal type");
				return nrd::Method(0);
			}
			}
		}

		if (context.denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Sigma) {
			switch (context.signalType) {
			case DenoisingContextInput::SignalType::Shadow:				return nrd::Method::SIGMA_SHADOW;
			case DenoisingContextInput::SignalType::MultiShadow:			return nrd::Method::SIGMA_SHADOW_TRANSLUCENCY;
			default: {
				assert(!"Unknown signal type");
				return nrd::Method(0);
			}
			}
		}

		assert(!"Unknown signal type");
		return nrd::Method(0);
	}

	bool IsAnyOf(nrd::Method method, nrd::Method other) {
		return method == other;
	}

	template<class... TArgs>
	bool IsAnyOf(nrd::Method method, nrd::Method other, TArgs... methods) {
		return IsAnyOf(method, other) || IsAnyOf(method, methods...);
	}

	bool IsResourceRequiredForMethod(nrd::ResourceType resourceType, nrd::Method method) {

		auto RELAX_SPECULAR				= nrd::Method::RELAX_SPECULAR;
		auto RELAX_DIFFUSE				= nrd::Method::RELAX_DIFFUSE;
		auto RELAX_DIFFUSE_SPECULAR		= nrd::Method::RELAX_DIFFUSE_SPECULAR;
		auto REBLUR_SPECULAR			= nrd::Method::REBLUR_SPECULAR;
		auto REBLUR_DIFFUSE				= nrd::Method::REBLUR_DIFFUSE;
		auto REBLUR_DIFFUSE_SPECULAR	= nrd::Method::REBLUR_DIFFUSE_SPECULAR;
		auto REBLUR_DIFFUSE_OCCLUSION	= nrd::Method::REBLUR_DIFFUSE_OCCLUSION;
		auto SIGMA_SHADOW				= nrd::Method::SIGMA_SHADOW;
		auto SIGMA_SHADOW_TRANSLUCENCY	= nrd::Method::SIGMA_SHADOW_TRANSLUCENCY;

		//
		// Returns true if 'method' is any of the methods defined in the va args list.
		// TODO: flip it around to return a list of required resources per method instead?
		#define CHECK_IF_NEEDED(resource, ...) case resource: return IsAnyOf(method, __VA_ARGS__);

		switch (resourceType) {
			CHECK_IF_NEEDED(nrd::ResourceType::IN_VIEWZ,						
				REBLUR_SPECULAR, REBLUR_DIFFUSE, REBLUR_DIFFUSE_SPECULAR, REBLUR_DIFFUSE_OCCLUSION,
				RELAX_DIFFUSE, RELAX_SPECULAR, RELAX_DIFFUSE_SPECULAR, 
				SIGMA_SHADOW, SIGMA_SHADOW_TRANSLUCENCY);

			CHECK_IF_NEEDED(nrd::ResourceType::IN_NORMAL_ROUGHNESS,				
				REBLUR_SPECULAR, REBLUR_DIFFUSE, REBLUR_DIFFUSE_SPECULAR, REBLUR_DIFFUSE_OCCLUSION,
				RELAX_DIFFUSE, RELAX_SPECULAR, RELAX_DIFFUSE_SPECULAR, 
				SIGMA_SHADOW, SIGMA_SHADOW_TRANSLUCENCY);

			CHECK_IF_NEEDED(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST,		
				REBLUR_SPECULAR, REBLUR_DIFFUSE_SPECULAR, 
				RELAX_SPECULAR, RELAX_DIFFUSE_SPECULAR);

			CHECK_IF_NEEDED(nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST,		
				REBLUR_SPECULAR, REBLUR_DIFFUSE_SPECULAR,
				RELAX_SPECULAR, RELAX_DIFFUSE_SPECULAR);

			CHECK_IF_NEEDED(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST,		
				REBLUR_DIFFUSE,	 REBLUR_DIFFUSE_SPECULAR,
				RELAX_DIFFUSE, RELAX_DIFFUSE_SPECULAR);

			CHECK_IF_NEEDED(nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST,		
				REBLUR_DIFFUSE,	 REBLUR_DIFFUSE_SPECULAR, 
				RELAX_DIFFUSE, RELAX_DIFFUSE_SPECULAR);

			CHECK_IF_NEEDED(nrd::ResourceType::IN_DIFF_HITDIST,
				REBLUR_DIFFUSE_OCCLUSION);

			default: {
				assert(!"Unknown resource type");
				return false;
			}
		}
	}

	bool IsResourceRequiredForAnyMethod(nrd::ResourceType resourceType, nrd::Method* methods, uint32_t numMethods) {
		for (uint32_t i = 0; i < numMethods; ++i) {
			if (IsResourceRequiredForMethod(resourceType, methods[i]))
				return true;
		}
		return false;
	}

	static const wchar_t* GetDescriptorTypeName(nrd::DescriptorType descriptorType) {
		switch (descriptorType) {
			case nrd::DescriptorType::TEXTURE:				return L"TEXTURE";
			case nrd::DescriptorType::STORAGE_TEXTURE:		return L"STORAGE_TEXTURE";
			default:
			{
				assert(!"Unknown resource type");
				return L"Unknown";
			}
		}
	}

	static GraphicsAPI::DescriptorHeap::Type GetDescriptorType(nrd::DescriptorType type) {
		switch (type) {
			case nrd::DescriptorType::TEXTURE:					return GraphicsAPI::DescriptorHeap::Type::TextureSrv;
			case nrd::DescriptorType::STORAGE_TEXTURE:			return GraphicsAPI::DescriptorHeap::Type::TextureUav;
			default:
			{
				assert(!"Unknown descriptor type");
				return GraphicsAPI::DescriptorHeap::Type::TextureSrv;
			}
		}
	}

	static const char* GetMethodName(nrd::Method method) {
		switch (method) {
			case nrd::Method::REBLUR_DIFFUSE:						return "REBLUR_DIFFUSE";
			case nrd::Method::REBLUR_DIFFUSE_OCCLUSION:				return "REBLUR_DIFFUSE_OCCLUSION";
			case nrd::Method::REBLUR_SPECULAR:						return "REBLUR_SPECULAR";
			case nrd::Method::REBLUR_SPECULAR_OCCLUSION:			return "REBLUR_SPECULAR_OCCLUSION";
			case nrd::Method::REBLUR_DIFFUSE_SPECULAR:				return "REBLUR_DIFFUSE_SPECULAR";
			case nrd::Method::REBLUR_DIFFUSE_SPECULAR_OCCLUSION:	return "REBLUR_DIFFUSE_SPECULAR_OCCLUSION";
			case nrd::Method::SIGMA_SHADOW:							return "SIGMA_SHADOW";
			case nrd::Method::SIGMA_SHADOW_TRANSLUCENCY:			return "SIGMA_SHADOW_TRANSLUCENCY";
			case nrd::Method::RELAX_DIFFUSE:						return "RELAX_DIFFUSE";
			case nrd::Method::RELAX_SPECULAR:						return "RELAX_SPECULAR";
			case nrd::Method::RELAX_DIFFUSE_SPECULAR:				return "RELAX_DIFFUSE_SPECULAR";

			default:												return "UNKNOWN METHOD";
		}
	}

	static GraphicsAPI::Resource::Format  GetFormat(nrd::Format format) {
		switch (format) {
			case nrd::Format::R8_UNORM:					return GraphicsAPI::Resource::Format::R8Unorm;
			case nrd::Format::R8_SNORM:					return GraphicsAPI::Resource::Format::R8Snorm;
			case nrd::Format::R8_UINT:					return GraphicsAPI::Resource::Format::R8Uint;
			case nrd::Format::R8_SINT:					return GraphicsAPI::Resource::Format::R8Int;

			case nrd::Format::RG8_UNORM:				return GraphicsAPI::Resource::Format::RG8Unorm;
			case nrd::Format::RG8_SNORM:				return GraphicsAPI::Resource::Format::RG8Snorm;
			case nrd::Format::RG8_UINT:					return GraphicsAPI::Resource::Format::RG8Uint;
			case nrd::Format::RG8_SINT:					return GraphicsAPI::Resource::Format::RG8Int;

			case nrd::Format::RGBA8_UNORM:				return GraphicsAPI::Resource::Format::RGBA8Unorm;
			case nrd::Format::RGBA8_SNORM:				return GraphicsAPI::Resource::Format::RGBA8Snorm;
			case nrd::Format::RGBA8_UINT:				return GraphicsAPI::Resource::Format::RGBA8Uint;
			case nrd::Format::RGBA8_SINT:				return GraphicsAPI::Resource::Format::RGBA8Int;
			case nrd::Format::RGBA8_SRGB:				return GraphicsAPI::Resource::Format::RGBA8UnormSrgb;

			case nrd::Format::R16_UNORM:				return GraphicsAPI::Resource::Format::R16Unorm;
			case nrd::Format::R16_SNORM:				return GraphicsAPI::Resource::Format::R16Snorm;
			case nrd::Format::R16_UINT:					return GraphicsAPI::Resource::Format::R16Uint;
			case nrd::Format::R16_SINT:					return GraphicsAPI::Resource::Format::R16Int;
			case nrd::Format::R16_SFLOAT:				return GraphicsAPI::Resource::Format::R16Float;

			case nrd::Format::RG16_UNORM:				return GraphicsAPI::Resource::Format::RG16Unorm;
			case nrd::Format::RG16_SNORM:				return GraphicsAPI::Resource::Format::RG16Snorm;
			case nrd::Format::RG16_UINT:				return GraphicsAPI::Resource::Format::RG16Uint;
			case nrd::Format::RG16_SINT:				return GraphicsAPI::Resource::Format::RG16Int;
			case nrd::Format::RG16_SFLOAT:				return GraphicsAPI::Resource::Format::RG16Float;

			case nrd::Format::RGBA16_UNORM:				return GraphicsAPI::Resource::Format::RGBA16Unorm;
			case nrd::Format::RGBA16_SNORM:				return GraphicsAPI::Resource::Format::Unknown;
			case nrd::Format::RGBA16_UINT:				return GraphicsAPI::Resource::Format::RGBA16Uint;
			case nrd::Format::RGBA16_SINT:				return GraphicsAPI::Resource::Format::RGBA16Int;
			case nrd::Format::RGBA16_SFLOAT:			return GraphicsAPI::Resource::Format::RGBA16Float;

			case nrd::Format::R32_UINT:					return GraphicsAPI::Resource::Format::R32Uint;
			case nrd::Format::R32_SINT:					return GraphicsAPI::Resource::Format::R32Int;
			case nrd::Format::R32_SFLOAT:				return GraphicsAPI::Resource::Format::R32Float;

			case nrd::Format::RG32_UINT:				return GraphicsAPI::Resource::Format::RG32Uint;
			case nrd::Format::RG32_SINT:				return GraphicsAPI::Resource::Format::RG32Int;
			case nrd::Format::RG32_SFLOAT:				return GraphicsAPI::Resource::Format::RG32Float;

			case nrd::Format::RGB32_UINT:				return GraphicsAPI::Resource::Format::RGB32Uint;
			case nrd::Format::RGB32_SINT:				return GraphicsAPI::Resource::Format::RGB32Int;
			case nrd::Format::RGB32_SFLOAT:				return GraphicsAPI::Resource::Format::RGB32Float;

			case nrd::Format::RGBA32_UINT:				return GraphicsAPI::Resource::Format::RGBA32Uint;
			case nrd::Format::RGBA32_SINT:				return GraphicsAPI::Resource::Format::RGBA32Int;
			case nrd::Format::RGBA32_SFLOAT:			return GraphicsAPI::Resource::Format::RGBA32Float;

			case nrd::Format::R10_G10_B10_A2_UNORM:		return GraphicsAPI::Resource::Format::Unknown;
			case nrd::Format::R10_G10_B10_A2_UINT:		return GraphicsAPI::Resource::Format::Unknown;
			case nrd::Format::R11_G11_B10_UFLOAT:		return GraphicsAPI::Resource::Format::R11G11B10Float;
			case nrd::Format::R9_G9_B9_E5_UFLOAT:		return GraphicsAPI::Resource::Format::Unknown;

			default:
			{
				assert(!"Unknown nrd format");
				return GraphicsAPI::Resource::Format::Unknown;
			}
		}
	}

	nrd::AccumulationMode GetNrdAccumulationMode(RenderTask::DenoisingOutput::Mode mode) {
		switch (mode) {
		case RenderTask::DenoisingOutput::Mode::Continue:					return nrd::AccumulationMode::CONTINUE;
		case RenderTask::DenoisingOutput::Mode::DiscardHistory:			return nrd::AccumulationMode::RESTART;
			default:
			{
				assert(!"RenderTask::DenoisingOutput::Mode mode");
				return nrd::AccumulationMode::CONTINUE;
			}
		}
	}

#if defined(GRAPHICS_API_D3D12)
	static D3D12_FILTER GetSamplerFilterMode(const nrd::Sampler& nrdSampler) {
		switch (nrdSampler) {
			case nrd::Sampler::NEAREST_CLAMP:					return D3D12_FILTER_MIN_MAG_MIP_POINT;
			case nrd::Sampler::NEAREST_MIRRORED_REPEAT:			return D3D12_FILTER_MIN_MAG_MIP_POINT;
			case nrd::Sampler::LINEAR_CLAMP:					return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			case nrd::Sampler::LINEAR_MIRRORED_REPEAT:			return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			default:
			{
				assert(!"Unknown nrd filter mode");
				return D3D12_FILTER_MIN_MAG_MIP_POINT;
			}
		}
	}

	static D3D12_TEXTURE_ADDRESS_MODE GetSamplerAddressMode(const nrd::Sampler& nrdSampler) {
		switch (nrdSampler) {
		case nrd::Sampler::NEAREST_CLAMP:					return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case nrd::Sampler::NEAREST_MIRRORED_REPEAT:			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case nrd::Sampler::LINEAR_CLAMP:					return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case nrd::Sampler::LINEAR_MIRRORED_REPEAT:			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		default:
		{
			assert(!"Unknown nrd sampler mode");
			return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		}
		}
	}

	static D3D12_SAMPLER_DESC GetSamplerCreateDesc(const nrd::Sampler& nrdSampler) {
		D3D12_SAMPLER_DESC desc = {};
		desc.Filter				= GetSamplerFilterMode(nrdSampler);
		desc.AddressU			= GetSamplerAddressMode(nrdSampler);
		desc.AddressV			= GetSamplerAddressMode(nrdSampler);
		desc.AddressW			= GetSamplerAddressMode(nrdSampler);
		desc.MipLODBias			= 0.f;
		desc.MaxAnisotropy		= 1;
		desc.ComparisonFunc		= D3D12_COMPARISON_FUNC_LESS;
		desc.BorderColor[0]		= 1.f;
		desc.BorderColor[1]		= 1.f;
		desc.BorderColor[2]		= 1.f;
		desc.BorderColor[3]		= 1.f;
		desc.MinLOD				= 0.f;
		desc.MaxLOD				= FLT_MAX;
		return desc;
	}

	static std::unique_ptr<GraphicsAPI::Sampler> CreateSampler(GraphicsAPI::Device* /*dev*/, const nrd::Sampler& nrdSampler) {
		std::unique_ptr<GraphicsAPI::Sampler> sampler = std::make_unique<GraphicsAPI::Sampler>();
		sampler->m_apiData.m_desc = GetSamplerCreateDesc(nrdSampler);
		return std::move(sampler);
	}
#endif

#if defined(GRAPHICS_API_VK)
	static VkFilter GetSamplerFilterMode(const nrd::Sampler& nrdSampler) {
		switch (nrdSampler) {
			case nrd::Sampler::NEAREST_CLAMP:					return VkFilter::VK_FILTER_NEAREST;
			case nrd::Sampler::NEAREST_MIRRORED_REPEAT:			return VkFilter::VK_FILTER_NEAREST;
			case nrd::Sampler::LINEAR_CLAMP:					return VkFilter::VK_FILTER_LINEAR;
			case nrd::Sampler::LINEAR_MIRRORED_REPEAT:			return VkFilter::VK_FILTER_LINEAR;
			default: 
			{
				assert(!"Unknown nrd sampler mode");
				return VK_FILTER_MAX_ENUM;
			}
		}
	}

	static VkSamplerAddressMode GetSamplerAddressMode(const nrd::Sampler& nrdSampler) {
		switch (nrdSampler) {
			case nrd::Sampler::NEAREST_CLAMP:					return VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case nrd::Sampler::NEAREST_MIRRORED_REPEAT:			return VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			case nrd::Sampler::LINEAR_CLAMP:					return VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			case nrd::Sampler::LINEAR_MIRRORED_REPEAT:			return VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			default:
			{
				assert(!"Unknown nrd address mode");
				return VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
			}
		}
	}

	static VkSamplerCreateInfo GetSamplerCreateInfo(const nrd::Sampler& nrdSampler) {
		VkSamplerCreateInfo info = {};
		info.sType					= VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter				= GetSamplerFilterMode(nrdSampler);
		info.minFilter				= GetSamplerFilterMode(nrdSampler);
		info.mipmapMode				= VK_SAMPLER_MIPMAP_MODE_NEAREST;
		info.addressModeU			= GetSamplerAddressMode(nrdSampler);
		info.addressModeV			= GetSamplerAddressMode(nrdSampler);
		info.addressModeW			= GetSamplerAddressMode(nrdSampler);
		info.mipLodBias				= 0.f;
		info.anisotropyEnable		= false;
		info.maxAnisotropy			= 1.f;
		info.compareEnable			= false;
		info.compareOp				= VK_COMPARE_OP_NEVER;
		info.minLod					= 0.f;
		info.maxLod					= VK_LOD_CLAMP_NONE;
		info.borderColor			= VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		info.unnormalizedCoordinates = false;
		return info;
	}

	static std::unique_ptr<GraphicsAPI::Sampler> CreateSampler(GraphicsAPI::Device* dev, const nrd::Sampler& nrdSampler) {

		std::unique_ptr<GraphicsAPI::Sampler> sampler = std::make_unique<GraphicsAPI::Sampler>();
		sampler->m_apiData.m_device = dev->m_apiData.m_device;
		const VkSamplerCreateInfo info = GetSamplerCreateInfo(nrdSampler);
		if (vkCreateSampler(dev->m_apiData.m_device, &info, nullptr, &sampler->m_apiData.m_sampler) != VK_SUCCESS) {
			Log::Fatal(L"Faild to create a sampler");
			return nullptr;
		}

		return std::move(sampler);
	}
#endif

	Status RenderPass_NRDDenoising::Init(PersistentWorkingSet *pws, const DenoisingContextInput& context, ShaderFactory::Factory* sf)
	{
		m_context = context;
		m_nrdConvertInputs.Init(pws, sf);

		nrd::Method method = GetNrdMethodForDenoisingContext(m_context);
		
		constexpr uint32_t kNumMethods = 1;

		nrd::Method methods[kNumMethods] = { method };
		const uint32_t maxWidth = context.maxWidth;
		const uint32_t maxHeight = context.maxHeight;

		nrd::MethodDesc methodDescs[kNumMethods];
		methodDescs[0].method = methods[0];
		methodDescs[0].fullResolutionWidth = (uint16_t)maxWidth;
		methodDescs[0].fullResolutionHeight = (uint16_t)maxHeight;
		static_assert(kNumMethods == 1, "Add method descs");

		nrd::DenoiserCreationDesc denoiserCreateDesc;
		denoiserCreateDesc.memoryAllocatorInterface = { nullptr, nullptr, nullptr, nullptr };
		denoiserCreateDesc.requestedMethodNum = kNumMethods;
		denoiserCreateDesc.requestedMethods = methodDescs;
#if _DEBUG
		denoiserCreateDesc.enableValidation = true;
#else
		denoiserCreateDesc.enableValidation = false;
#endif

		assert(m_denoiser == nullptr);
		RETURN_IF_STATUS_FAILED_NRD(nrd::CreateDenoiser(denoiserCreateDesc, m_denoiser));

		assert(m_denoiser != nullptr);
		const nrd::DenoiserDesc& denDesc = nrd::GetDenoiserDesc(*m_denoiser);

		////
		// NRD Internal Resources
		////
		{
			const uint32_t poolSize = (size_t)(denDesc.transientPoolSize) + denDesc.permanentPoolSize;
			assert(m_resources.size() == 0);
			m_resources.resize(poolSize);
			m_transientPoolSize = denDesc.transientPoolSize;
			for (uint32_t poolIdx = 0; poolIdx < poolSize; ++poolIdx)
			{
				const nrd::TextureDesc& desc = poolIdx < denDesc.transientPoolSize ? denDesc.transientPool[poolIdx] : denDesc.permanentPool[poolIdx - denDesc.transientPoolSize];

				const GraphicsAPI::Resource::Format format = GetFormat(desc.format);
				assert(format != GraphicsAPI::Resource::Format::Unknown);

				std::unique_ptr<GraphicsAPI::Texture> texture = pws->CreateTextureResource(
					GraphicsAPI::Resource::Type::Texture2D,
					format,
					GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderResource,
					desc.width, desc.height, 1 /*depth*/, 1 /*arraySize*/, desc.mipNum, 1/*sampleCount*/,
					ResourceLogger::ResourceKind::e_DenoiserPerm_SharedEntry);

				const bool isTransient = poolIdx < denDesc.transientPoolSize;
				std::wstring debugName = std::wstring(L"NRD ") + (isTransient ? L"Transient" : L"Permanent") + L"Texture [" + std::to_wstring(isTransient ? poolIdx : poolIdx - denDesc.transientPoolSize) + L"]";

				texture->SetName(DebugName(debugName));

				m_resources[poolIdx] = std::move(texture);
			}
		}

		////
		// Named resources
		////
		{
			auto CreateNRDTexture = [pws](GraphicsAPI::Resource::Format format, uint32_t width, uint32_t height, const wchar_t* debugName) {
				std::unique_ptr<GraphicsAPI::Texture> texture = pws->CreateTextureResource(
					GraphicsAPI::Resource::Type::Texture2D,
					format,
					GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderResource,
					width, height, 1 /*depth*/, 1 /*arraySize*/, 1/*mipNum*/, 1 /*sampleCount*/,
					ResourceLogger::ResourceKind::e_DenoiserPerm_SharedEntry);
				texture->SetName(DebugName(std::wstring(debugName)));

				return std::move(texture);
			};

			if (IsResourceRequiredForAnyMethod(nrd::ResourceType::IN_VIEWZ, methods, kNumMethods))
				m_namedResources[(size_t)nrd::ResourceType::IN_VIEWZ] = CreateNRDTexture(GraphicsAPI::Resource::Format::R32Float, maxWidth, maxHeight, L"IN_VIEWZ");

			if (IsResourceRequiredForAnyMethod(nrd::ResourceType::IN_NORMAL_ROUGHNESS, methods, kNumMethods))
				m_namedResources[(size_t)nrd::ResourceType::IN_NORMAL_ROUGHNESS] = CreateNRDTexture(GraphicsAPI::Resource::Format::RGBA8Unorm, maxWidth, maxHeight, L"IN_NORMAL_ROUGHNESS");

			if (IsResourceRequiredForAnyMethod(nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST, methods, kNumMethods))
				m_namedResources[(size_t)nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST] = CreateNRDTexture(GraphicsAPI::Resource::Format::RGBA16Float, maxWidth, maxHeight, L"IN_SPEC_RADIANCE_HITDIST");

			if (IsResourceRequiredForAnyMethod(nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST, methods, kNumMethods))
				m_namedResources[(size_t)nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST] = CreateNRDTexture(GraphicsAPI::Resource::Format::RGBA16Float, maxWidth, maxHeight, L"IN_DIFF_RADIANCE_HITDIST");

			if (IsResourceRequiredForAnyMethod(nrd::ResourceType::IN_DIFF_HITDIST, methods, kNumMethods))
				m_namedResources[(size_t)nrd::ResourceType::IN_DIFF_HITDIST] = CreateNRDTexture(GraphicsAPI::Resource::Format::R16Float, maxWidth, maxHeight, L"IN_DIFF_HITDIST");
		}

		////
		// Samplers
		////
		{
			m_samplers.resize(denDesc.staticSamplerNum);
			for (uint32_t samplerIdx = 0; samplerIdx < denDesc.staticSamplerNum; ++samplerIdx)
			{
				const nrd::StaticSamplerDesc& sampler = denDesc.staticSamplers[samplerIdx];
				m_samplers[samplerIdx].sampler			= std::move(CreateSampler(&pws->m_device, sampler.sampler));
				m_samplers[samplerIdx].registerIndex	= sampler.registerIndex;
			}
		}

		{
#if defined(GRAPHICS_API_D3D12)
			m_descTableLayout = std::make_unique<GraphicsAPI::DescriptorTableLayout>();
			m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::Cbv, denDesc.constantBufferDesc.registerIndex, denDesc.descriptorSetDesc.constantBufferMaxNum, 0);
			m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 0, denDesc.descriptorSetDesc.textureMaxNum, 0);
			m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureUav, 0, denDesc.descriptorSetDesc.storageTextureMaxNum, 0);
			m_descTableLayout->SetAPIData(&pws->m_device);
			
			m_samplerTableLayout = std::make_unique<GraphicsAPI::DescriptorTableLayout>();
			m_samplerTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::Sampler, 0, denDesc.staticSamplerNum, 0);
			m_samplerTableLayout->SetAPIData(&pws->m_device);

			m_rootSignature = std::make_unique<GraphicsAPI::RootSignature>();
			m_rootSignature->Init(&pws->m_device, { m_descTableLayout.get(), m_samplerTableLayout.get() });
			m_rootSignature->SetName(DebugName(L"NRD_RootSignature"));
#elif defined(GRAPHICS_API_VK)
			m_descTableLayout = std::make_unique<GraphicsAPI::DescriptorTableLayout>();
			const nrd::LibraryDesc& libraryDesc = nrd::GetLibraryDesc();
			uint32_t samplerOffset = libraryDesc.spirvBindingOffsets.samplerOffset;
			uint32_t textureOffset = libraryDesc.spirvBindingOffsets.textureOffset;
			uint32_t constantBufferOffset = libraryDesc.spirvBindingOffsets.constantBufferOffset;
			uint32_t storageTextureAndBufferOffset = libraryDesc.spirvBindingOffsets.storageTextureAndBufferOffset;

			for (uint32_t cbvIt = 0; cbvIt < denDesc.descriptorSetDesc.constantBufferMaxNum; ++cbvIt) {
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::Cbv, 0, 1, 0, constantBufferOffset + cbvIt);
			}
			for (uint32_t srvIt = 0; srvIt < denDesc.descriptorSetDesc.textureMaxNum; ++srvIt) {
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureSrv, 0, 1, 0, textureOffset + srvIt);
			}
			for (uint32_t uavIt = 0; uavIt < denDesc.descriptorSetDesc.storageTextureMaxNum; ++uavIt) {
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TextureUav, 0, 1, 0, storageTextureAndBufferOffset + uavIt);
			}
			for (uint32_t samplerIt = 0; samplerIt < denDesc.staticSamplerNum; ++samplerIt) {
				m_descTableLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::Sampler, 0, 1, 0, samplerOffset + samplerIt);
			}
			m_descTableLayout->SetAPIData(&pws->m_device);

			m_rootSignature = std::make_unique<GraphicsAPI::RootSignature>();
			m_rootSignature->Init(&pws->m_device, { m_descTableLayout.get() });
			m_rootSignature->SetName(DebugName(L"NRD_RootSignature"));
#endif
		}
		
		{
			assert(m_psos.size() == 0);

			m_psos.resize(denDesc.pipelineNum);
			for (uint32_t pipelineIdx = 0; pipelineIdx < denDesc.pipelineNum; ++pipelineIdx)
			{
				const nrd::PipelineDesc& pipelineDesc = denDesc.pipelines[pipelineIdx];

				#if defined(GRAPHICS_API_D3D12)
				GraphicsAPI::ComputeShader	cs;
				cs.Init(pipelineDesc.computeShaderDXIL.bytecode, pipelineDesc.computeShaderDXIL.size);
				#endif

				#if defined(GRAPHICS_API_VK)
				GraphicsAPI::ComputeShader	cs;
				cs.Init(pipelineDesc.computeShaderSPIRV.bytecode, pipelineDesc.computeShaderSPIRV.size);
				#endif
				std::unique_ptr<GraphicsAPI::ComputePipelineState> pso = std::make_unique<GraphicsAPI::ComputePipelineState>();
				pso->Init(&pws->m_device, m_rootSignature.get(), &cs);
				m_psos[pipelineIdx] = std::move(pso);
			}
		}

		return Status::OK;
	};

	Status RenderPass_NRDDenoising::DeferredRelease(PersistentWorkingSet* pws) {
		for (std::unique_ptr<GraphicsAPI::ComputePipelineState>& pso : m_psos) {
			if (pws != nullptr)
				pws->DeferredRelease(std::move(pso));
			else
				pso.reset();
		}

		for (std::unique_ptr<GraphicsAPI::Texture>& texture : m_resources) {
			if (pws != nullptr)
				pws->DeferredRelease(std::move(texture));
			else
				texture.reset();
		}

		for (std::unique_ptr<GraphicsAPI::Texture>& texture : m_namedResources) {
			if (texture.get()) {
				if (pws != nullptr)
					pws->DeferredRelease(std::move(texture));
				else
					texture.reset();
			}
		}

		if (pws != nullptr) {
			pws->DeferredRelease(std::move(m_rootSignature));
			pws->DeferredRelease(std::move(m_descTableLayout));
			pws->DeferredRelease(std::move(m_samplerTableLayout));
		}
		else {
			m_rootSignature.reset();
			m_descTableLayout.reset();
			m_samplerTableLayout.reset();
		}

		return m_nrdConvertInputs.DeferredRelease(pws);
	}

	GraphicsAPI::Texture* RenderPass_NRDDenoising::ResolveNrdResource(
		PersistentWorkingSet* pws,
		const nrd::Resource& nrdResource,
		RenderPass_ResourceRegistry* registry,
		const RenderTask::DenoisingOutput& output,
		GraphicsAPI::ShaderResourceView* srv,
		GraphicsAPI::UnorderedAccessView* uav) {

		auto& dev(pws->m_device);

		switch (nrdResource.type) {
		case nrd::ResourceType::TRANSIENT_POOL:
		case nrd::ResourceType::PERMANENT_POOL: {
			int32_t indexInPool = nrdResource.indexInPool;
			if (nrdResource.type == nrd::ResourceType::PERMANENT_POOL)
				indexInPool += m_transientPoolSize;

			GraphicsAPI::Texture* resource = m_resources[indexInPool].get();
			if (srv)
				srv->Init(&dev, resource, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			if (uav)
				uav->Init(&dev, resource, nrdResource.mipOffset, 0, 1);
			return resource;
		}
		case nrd::ResourceType::IN_VIEWZ: {
			GraphicsAPI::Texture* resource = m_namedResources[(size_t)nrd::ResourceType::IN_VIEWZ].get();
			if (srv)
				srv->Init(&dev, resource, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			return resource;
		}
		case nrd::ResourceType::IN_NORMAL_ROUGHNESS: {
			GraphicsAPI::Texture* resource = m_namedResources[(size_t)nrd::ResourceType::IN_NORMAL_ROUGHNESS].get();
			if (srv)
				srv->Init(&dev, resource, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			return resource;
		}
		case nrd::ResourceType::IN_MV: {
			// Motion vectors are optional for debugging purposes.
			GraphicsAPI::Texture* motion = registry->GetTexture(output.motion.tex);
			if (motion && srv)
			{
				srv->Init(&dev, motion, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
				return motion;
			}

#if defined(GRAPHICS_API_D3D12)
			if (srv)
				srv->InitNullView(GraphicsAPI::Resource::Type::Texture2D, false);
			return nullptr;
#elif defined(GRAPHICS_API_VK)
			if (srv)
				srv->InitNullView(&dev, GraphicsAPI::Resource::Type::Texture2D, GraphicsAPI::Resource::Format::RGBA16Float, false);
			return nullptr;
#endif
		}
		case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST: {
			GraphicsAPI::Texture* resource = m_namedResources[(size_t)nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST].get();
			if (srv)
				srv->Init(&dev, resource, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			return resource;
		}
		case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST: {
			GraphicsAPI::Texture* resource = m_namedResources[(size_t)nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST].get();
			if (srv)
				srv->Init(&dev, resource, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			if (uav)
				uav->Init(&dev, resource, nrdResource.mipOffset, 0, 1);
			return resource;
		}
		case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST: {
			GraphicsAPI::Texture* rw = registry->GetTexture(output.inOutSpecular);
			if (srv)
				srv->Init(&dev, rw, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			if (uav)
				uav->Init(&dev, rw, nrdResource.mipOffset, 0, 1);
			return rw;
		}
		case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: {
			GraphicsAPI::Texture* rw = registry->GetTexture(output.inOutDiffuse);
			if (srv)
				srv->Init(&dev, rw, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			if (uav)
				uav->Init(&dev, rw, nrdResource.mipOffset, 0, 1);
			return rw;
		}
		case nrd::ResourceType::IN_SHADOWDATA: {
			GraphicsAPI::Texture* rw = registry->GetTexture(output.inShadow0);
			if (srv)
				srv->Init(&dev, rw, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			return rw;
		}
		case nrd::ResourceType::IN_SHADOW_TRANSLUCENCY: {
			GraphicsAPI::Texture* rw = registry->GetTexture(output.inShadow1);
			if (srv)
				srv->Init(&dev, rw, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			return rw;
		}
		case nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY: {
			GraphicsAPI::Texture* rw = registry->GetTexture(output.inOutShadow);
			if (srv)
				srv->Init(&dev, rw, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			if (uav)
				uav->Init(&dev, rw, nrdResource.mipOffset, 0, 1);
			return rw;
		}
		case nrd::ResourceType::IN_DIFF_HITDIST: {
			GraphicsAPI::Texture* resource = m_namedResources[(size_t)nrd::ResourceType::IN_DIFF_HITDIST].get();
			if (srv)
				srv->Init(&dev, resource, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			if (uav)
				uav->Init(&dev, resource, nrdResource.mipOffset, 0, 1);
			return resource;
		}
		case nrd::ResourceType::OUT_DIFF_HITDIST: {
			GraphicsAPI::Texture* rw = registry->GetTexture(output.inOutOcclusion);
			if (srv)
				srv->Init(&dev, rw, nrdResource.mipOffset, nrdResource.mipNum, 0, 1);
			if (uav)
				uav->Init(&dev, rw, nrdResource.mipOffset, 0, 1);
			return rw;
		}
		case nrd::ResourceType::IN_SPEC_HITDIST:
		case nrd::ResourceType::IN_DIFF_DIRECTION_PDF:
		case nrd::ResourceType::IN_SPEC_DIRECTION_PDF:
		case nrd::ResourceType::IN_DIFF_CONFIDENCE:
		case nrd::ResourceType::IN_SPEC_CONFIDENCE:
		case nrd::ResourceType::OUT_SPEC_HITDIST: {
#if defined(GRAPHICS_API_D3D12)
			if (srv)
				srv->InitNullView(GraphicsAPI::Resource::Type::Texture2D, false);
			if (uav)
				uav->InitNullView(GraphicsAPI::Resource::Type::Texture2D, false);
			NOT_IMPLEMENTED_WARNING(L"Resource %s %s Input/Outputs not hooked up.", GetResourceName(nrdResource.type), GetDescriptorTypeName(nrdResource.stateNeeded));
			return nullptr;
#elif defined(GRAPHICS_API_VK)
			NOT_IMPLEMENTED_FATAL(L"Resource %s %s Input/Outputs not hooked up.", GetResourceName(nrdResource.type), GetDescriptorTypeName(nrdResource.stateNeeded));
			return nullptr;
#endif
		}
		default: {
			NOT_IMPLEMENTED_FATAL(L"Resource %d not recognized", (uint32_t)(nrdResource.type));
			return nullptr;
		}
		}
	};

	struct NRDStateTransitions
	{
		void RegisterStateTransition(const nrd::Resource& nrdResource, GraphicsAPI::Texture* resource) {

			dstBufArr.push_back(resource);

			GraphicsAPI::SubresourceRange subresourceRange(0, 1, (uint8_t)nrdResource.mipOffset, (uint8_t)nrdResource.mipNum);
			subresourceIdx.push_back(subresourceRange);

			if (nrdResource.stateNeeded == nrd::DescriptorType::TEXTURE)
				desiredState.push_back(GraphicsAPI::ResourceState::State::NonPixelShader);
			else if (nrdResource.stateNeeded == nrd::DescriptorType::STORAGE_TEXTURE) {
				desiredState.push_back(GraphicsAPI::ResourceState::State::UnorderedAccess);
				uavArr.push_back(resource);
			}
			else {
				desiredState.push_back(GraphicsAPI::ResourceState::State::Undefined);
				NOT_IMPLEMENTED_FATAL("Unexpected resource state!");
			}
		};

		void Flush(GraphicsAPI::CommandList* cmdList) {

#if defined(GRAPHICS_API_D3D12)
			cmdList->ResourceTransitionBarrier(
				dstBufArr.data(),
				subresourceIdx.data(),
				desiredState.size(),
				desiredState.data());
#else
			cmdList->ResourceTransitionBarrier(
				dstBufArr.data(),
				subresourceIdx.data(),
				desiredState.size(),
				desiredState.data());
#endif

			cmdList->ResourceUAVBarrier(uavArr.data(), uavArr.size());

			dstBufArr.clear();
			subresourceIdx.clear();
			desiredState.clear();
			uavArr.clear();
		}

		std::vector<GraphicsAPI::Resource*>						dstBufArr;
		std::vector<GraphicsAPI::SubresourceRange>				subresourceIdx;
		std::vector<GraphicsAPI::ResourceState::State>			desiredState;
		std::vector<GraphicsAPI::Resource*>						uavArr;
	};

	nrd::CheckerboardMode RenderPass_NRDDenoising::GetCheckerboardMode(const RenderTask::DenoisingOutput& reflectionOutputs, uint64_t frameIndex) {

		// CHECKERBOARD  CHECKERBOARD_INVERTED
		//		0 1			   1 0
		//		1 0			   0 1

		/// Given any of the two possible checkerboard states above we must decide between either
		/// CheckerboardMode::WHITE or CheckerboardMode::BLACK depending on the oddness of the frame index. 
		/// This is in order to be consistent with NRD that internally calculates the checkerboard pattern based on odd and even frames...
		/// When frameIndex is reset the checkerboard mode might need to update.

		//		CASE 1			CASE 2
		// nrd::CheckerboardMode::BLACK
		// Even frame(0)  Odd frame(1)   ...
		// 		0 1             1 0
		// 		1 0             0 1

		//		CASE 3			CASE 4
		// nrd::CheckerboardMode::WHITE
		// Even frame(0)  Odd frame(1)   ...
		// 		1 0             0 1
		// 		0 1             1 0

		const bool bIsEvenFrame = frameIndex % 2 == 0;

		if (reflectionOutputs.halfResolutionMode == RenderTask::HalfResolutionMode::CHECKERBOARD)
		{
			return bIsEvenFrame ? nrd::CheckerboardMode::BLACK /*CASE 1*/ : nrd::CheckerboardMode::WHITE  /*CASE 4*/;
		}
		else if (reflectionOutputs.halfResolutionMode == RenderTask::HalfResolutionMode::CHECKERBOARD_INVERTED)
		{
			return bIsEvenFrame ? nrd::CheckerboardMode::WHITE /*CASE 3*/ : nrd::CheckerboardMode::BLACK /*CASE 2*/;
		}

		assert(reflectionOutputs.halfResolutionMode == RenderTask::HalfResolutionMode::OFF);
		return nrd::CheckerboardMode::OFF;
	}

	Status RenderPass_NRDDenoising::UpdateSettings(const RenderTask::DenoisingOutput& reflectionOutputs, uint64_t frameIndex) {
		nrd::Method method = GetNrdMethodForDenoisingContext(m_context);

		if (method == nrd::Method::REBLUR_SPECULAR) {
			nrd::ReblurSpecularSettings settings = {};
			settings.checkerboardMode = GetCheckerboardMode(reflectionOutputs, frameIndex);
			RETURN_IF_STATUS_FAILED_NRD(nrd::SetMethodSettings(*m_denoiser, nrd::Method::REBLUR_SPECULAR, &settings));
		}
		else if (method == nrd::Method::REBLUR_DIFFUSE) {
			nrd::ReblurDiffuseSettings settings = {};
			settings.checkerboardMode = GetCheckerboardMode(reflectionOutputs, frameIndex);
			RETURN_IF_STATUS_FAILED_NRD(nrd::SetMethodSettings(*m_denoiser, nrd::Method::REBLUR_DIFFUSE, &settings));
		}
		else if (method == nrd::Method::REBLUR_DIFFUSE_SPECULAR) {
			nrd::ReblurDiffuseSpecularSettings settings = {};
			settings.specularSettings.checkerboardMode = GetCheckerboardMode(reflectionOutputs, frameIndex);
			settings.diffuseSettings.checkerboardMode = GetCheckerboardMode(reflectionOutputs, frameIndex);
			RETURN_IF_STATUS_FAILED_NRD(nrd::SetMethodSettings(*m_denoiser, nrd::Method::REBLUR_DIFFUSE_SPECULAR, &settings));
		}
		else if (method == nrd::Method::REBLUR_DIFFUSE_OCCLUSION) {
			nrd::ReblurDiffuseSettings settings = {};
			settings.checkerboardMode = GetCheckerboardMode(reflectionOutputs, frameIndex);
			RETURN_IF_STATUS_FAILED_NRD(nrd::SetMethodSettings(*m_denoiser, nrd::Method::REBLUR_DIFFUSE_OCCLUSION, &settings));
		}
		else if (method == nrd::Method::RELAX_SPECULAR) {
			nrd::RelaxSpecularSettings settings = {};
			settings.checkerboardMode = GetCheckerboardMode(reflectionOutputs, frameIndex);
			RETURN_IF_STATUS_FAILED_NRD(nrd::SetMethodSettings(*m_denoiser, nrd::Method::RELAX_SPECULAR, &settings));
		}
		else if (method == nrd::Method::RELAX_DIFFUSE) {
			nrd::RelaxDiffuseSettings settings = {};
			settings.checkerboardMode = GetCheckerboardMode(reflectionOutputs, frameIndex);
			RETURN_IF_STATUS_FAILED_NRD(nrd::SetMethodSettings(*m_denoiser, nrd::Method::RELAX_DIFFUSE, &settings));
		}
		else if (method == nrd::Method::RELAX_DIFFUSE_SPECULAR) {
			nrd::RelaxDiffuseSpecularSettings settings = {};
			settings.checkerboardMode = GetCheckerboardMode(reflectionOutputs, frameIndex);
			RETURN_IF_STATUS_FAILED_NRD(nrd::SetMethodSettings(*m_denoiser, nrd::Method::RELAX_DIFFUSE_SPECULAR, &settings));
		}
		else if (method == nrd::Method::SIGMA_SHADOW) {
			nrd::SigmaShadowSettings settings = {};
			RETURN_IF_STATUS_FAILED_NRD(nrd::SetMethodSettings(*m_denoiser, nrd::Method::SIGMA_SHADOW, &settings));
		}
		else if (method == nrd::Method::SIGMA_SHADOW_TRANSLUCENCY) {
			nrd::SigmaShadowSettings settings = {};
			RETURN_IF_STATUS_FAILED_NRD(nrd::SetMethodSettings(*m_denoiser, nrd::Method::SIGMA_SHADOW_TRANSLUCENCY, &settings));
		}
		else {
			return Status::ERROR_INTERNAL;
		}

		return Status::OK;
	}

	Status RenderPass_NRDDenoising::BuildCommandList(TaskWorkingSet* tws, 
		GraphicsAPI::CommandList* cmdList, 
		RenderPass_ResourceRegistry* resources,
		const RenderTask::DenoisingOutput& output)
	{
		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);
		auto& dev(pws->m_device);

		resources->TrackResource(output.depth.tex,		GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.normal.tex,		GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.roughness.tex,	GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.motion.tex,		GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.inputMask.tex,	GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.inSpecular,		GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.inOutSpecular,	GraphicsAPI::ResourceState::State::UnorderedAccess);
		resources->TrackResource(output.inDiffuse,		GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.inOutDiffuse,	GraphicsAPI::ResourceState::State::UnorderedAccess);
		resources->TrackResource(output.inHitT,			GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.inOutOcclusion,	GraphicsAPI::ResourceState::State::UnorderedAccess);
		resources->TrackResource(output.inShadow0,		GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.inShadow1,		GraphicsAPI::ResourceState::State::ShaderResource);
		resources->TrackResource(output.inOutShadow,	GraphicsAPI::ResourceState::State::UnorderedAccess);

		// Can't really see any value in providing this in DenoisingOutput.
		// AFAIK it's only used in combination with checkerboard mode to select the checkerboard pattern.
		m_frameIndex++;

		RETURN_IF_STATUS_FAILED(UpdateSettings(output, m_frameIndex));

		{
			// Run NRD...
			{
				nrd::CommonSettings commonSettings;
				{
					Math::Float_4x4 viewToClipMatrix = output.viewToClipMatrix;
					Math::Float_4x4 viewToClipMatrixPrev = output.viewToClipMatrixPrev;
					Math::Float_4x4 worldToViewMatrix = output.worldToViewMatrix;
					Math::Float_4x4 worldToViewMatrixPrev = output.worldToViewMatrixPrev;

					memcpy(commonSettings.viewToClipMatrix, viewToClipMatrix.f, sizeof(viewToClipMatrix.f));
					memcpy(commonSettings.viewToClipMatrixPrev, viewToClipMatrixPrev.f, sizeof(viewToClipMatrixPrev.f));
					memcpy(commonSettings.worldToViewMatrix, worldToViewMatrix.f, sizeof(worldToViewMatrix.f));
					memcpy(commonSettings.worldToViewMatrixPrev, worldToViewMatrixPrev.f, sizeof(worldToViewMatrixPrev.f));

					commonSettings.cameraJitter[0] = output.cameraJitter.f[0];
					commonSettings.cameraJitter[1] = output.cameraJitter.f[1];

					const bool enableMotionVecs = resources->GetTexture(output.motion.tex) ? true : false;
					if (enableMotionVecs)
					{
						commonSettings.motionVectorScale[0] = output.motion.scale.f[0];
						commonSettings.motionVectorScale[1] = output.motion.scale.f[1];
						if (output.motion.type == RenderTask::MotionType::RGB_WorldSpace) {
							commonSettings.isMotionVectorInWorldSpace = true;
						}
						else {
							assert(output.motion.type == RenderTask::MotionType::RG_ViewSpace);
							commonSettings.isMotionVectorInWorldSpace = false;
						}
					}
					else {
						commonSettings.motionVectorScale[0] = 0;
						commonSettings.motionVectorScale[1] = 0;
						commonSettings.isMotionVectorInWorldSpace = true;
					}

					// Always clear first frame
					commonSettings.accumulationMode = m_frameIndex == 0 ? nrd::AccumulationMode::CLEAR_AND_RESTART : GetNrdAccumulationMode(output.mode);

					commonSettings.frameIndex = m_frameIndex;
				}

				const nrd::DenoiserDesc& denDesc = nrd::GetDenoiserDesc(*m_denoiser);
				const nrd::DispatchDesc* dispatchDescs = nullptr;
				uint32_t dispatchDescNum = 0;
				RETURN_IF_STATUS_FAILED_NRD(GetComputeDispatches(*m_denoiser, commonSettings, dispatchDescs, dispatchDescNum));

#if defined(GRAPHICS_API_VK)
				const nrd::LibraryDesc& libraryDesc = nrd::GetLibraryDesc();
				uint32_t samplerOffset = libraryDesc.spirvBindingOffsets.samplerOffset;
				uint32_t textureOffset = libraryDesc.spirvBindingOffsets.textureOffset;
				uint32_t constantBufferOffset = libraryDesc.spirvBindingOffsets.constantBufferOffset;
				uint32_t storageTextureAndBufferOffset = libraryDesc.spirvBindingOffsets.storageTextureAndBufferOffset;
#endif


#if defined(GRAPHICS_API_D3D12)
				GraphicsAPI::DescriptorTable samplerTable;
				if (!samplerTable.Allocate(&tws->m_CBVSRVUAVHeap, m_samplerTableLayout.get())) {
					Log::Fatal(L"Faild to allocate a portion of desc heap.");
					return Status::ERROR_INTERNAL;
				}

				for (Sampler& sampler : m_samplers) {
					samplerTable.SetSampler(&dev, 0, sampler.registerIndex, sampler.sampler.get());
				}
#endif
				// Convert inputs
				{
					const nrd::Method method = GetNrdMethodForDenoisingContext(m_context);
					GraphicsAPI::Utils::ScopedEventObject ev(cmdList, { 0, 128, 0 }, DebugName("%s - Conversion Layer", GetMethodName(method)));

					std::array<GraphicsAPI::Texture*, (size_t)nrd::ResourceType::MAX_NUM> inputsToPrepare;
					inputsToPrepare.fill(nullptr);
					inputsToPrepare[(int)nrd::ResourceType::IN_VIEWZ]					= m_namedResources[(int)nrd::ResourceType::IN_VIEWZ].get();
					inputsToPrepare[(int)nrd::ResourceType::IN_NORMAL_ROUGHNESS]		= m_namedResources[(int)nrd::ResourceType::IN_NORMAL_ROUGHNESS].get();
					inputsToPrepare[(int)nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST]	= m_namedResources[(int)nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST].get();
					inputsToPrepare[(int)nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST]	= m_namedResources[(int)nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST].get();
					inputsToPrepare[(int)nrd::ResourceType::IN_DIFF_HITDIST]			= m_namedResources[(int)nrd::ResourceType::IN_DIFF_HITDIST].get();
					RETURN_IF_STATUS_FAILED(m_nrdConvertInputs.BuildCommandList(tws, cmdList, resources, m_context, output, inputsToPrepare));
				}

				cmdList->SetComputeRootSignature(m_rootSignature.get());

				for (uint32_t i = 0; i < dispatchDescNum; ++i) {

					const nrd::DispatchDesc& dispatch = dispatchDescs[i];
					const nrd::PipelineDesc& pipelineDesc = denDesc.pipelines[dispatch.pipelineIndex];

					GraphicsAPI::Utils::ScopedEventObject ev(cmdList, { 0, 128, 0 }, DebugName("%s", dispatch.name));

					GraphicsAPI::DescriptorTable descTable;
					NRDStateTransitions stateTransitions;

					{ // Desctriptor Table
						if (!descTable.Allocate(&tws->m_CBVSRVUAVHeap, m_descTableLayout.get())) {
							Log::Fatal(L"Faild to allocate a portion of desc heap.");
							return Status::ERROR_INTERNAL;
						}

						if (pipelineDesc.hasConstantData) {
							assert(dispatch.constantBufferData && dispatch.constantBufferDataSize != 0);
							GraphicsAPI::ConstantBufferView cbv;
							void* cbPtrForWrite;
							RETURN_IF_STATUS_FAILED(tws->m_volatileConstantBuffer.Allocate(dispatch.constantBufferDataSize, &cbv, &cbPtrForWrite));
							memcpy(cbPtrForWrite, dispatch.constantBufferData, dispatch.constantBufferDataSize);

#if defined(GRAPHICS_API_D3D12)
							descTable.SetCbv(&dev, 0, 0, &cbv);
#elif defined(GRAPHICS_API_VK)
							descTable.SetCbv(&dev, constantBufferOffset, 0, &cbv);
#endif
						}

						uint32_t resourceIdx = 0;
						for (uint32_t rangeIt = 0; rangeIt < pipelineDesc.descriptorRangeNum; ++rangeIt) {

							const nrd::DescriptorRangeDesc& range = pipelineDesc.descriptorRanges[rangeIt];

							for (uint32_t descIt = 0; descIt < range.descriptorNum; ++descIt) {
								const nrd::Resource& nrdResource = dispatch.resources[resourceIdx++];

								std::unique_ptr<GraphicsAPI::ShaderResourceView> srv = range.descriptorType == nrd::DescriptorType::TEXTURE ? std::make_unique<GraphicsAPI::ShaderResourceView>() : nullptr;
								std::unique_ptr<GraphicsAPI::UnorderedAccessView> uav = range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE ? std::make_unique<GraphicsAPI::UnorderedAccessView>() : nullptr;

								GraphicsAPI::Texture* resource = ResolveNrdResource(pws, nrdResource, resources, output, srv.get(), uav.get());

								if (resource) { // Returns nullptr for null-srv/uavs
									stateTransitions.RegisterStateTransition(nrdResource, resource);
								}

								if (range.descriptorType == nrd::DescriptorType::TEXTURE) {
#if defined(GRAPHICS_API_D3D12)
									descTable.SetSrv(&dev, 1, range.baseRegisterIndex + descIt, srv.get());
#elif defined(GRAPHICS_API_VK)
									descTable.SetSrv(&dev, textureOffset + range.baseRegisterIndex + descIt, 0, srv.get());
#endif
								}
								else if (range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE) {
									assert(nrdResource.mipNum == 1);
#if defined(GRAPHICS_API_D3D12)
									descTable.SetUav(&dev, 2, range.baseRegisterIndex + descIt, uav.get());
#elif defined(GRAPHICS_API_VK)
									descTable.SetUav(&dev, storageTextureAndBufferOffset + range.baseRegisterIndex + descIt, 0, uav.get());
#endif
								}

								pws->DeferredRelease(std::move(srv));
								pws->DeferredRelease(std::move(uav));
							}
						}
						assert(resourceIdx == dispatch.resourceNum);
					}

					stateTransitions.Flush(cmdList);


#if defined(GRAPHICS_API_D3D12)

					std::vector<GraphicsAPI::DescriptorTable*> tableArr{ &descTable , &samplerTable };

#elif defined(GRAPHICS_API_VK)
					for (Sampler& sampler : m_samplers)
					{
						descTable.SetSampler(&dev, samplerOffset, sampler.registerIndex, sampler.sampler.get());
					}

					std::vector<GraphicsAPI::DescriptorTable*> tableArr{ &descTable };
#endif

					cmdList->SetComputeRootDescriptorTable(m_rootSignature.get(), 0, tableArr.data(), tableArr.size());
					cmdList->SetComputePipelineState(m_psos[dispatch.pipelineIndex].get());
					cmdList->Dispatch(dispatch.gridWidth, dispatch.gridHeight, 1);
				}

				{
					std::vector<GraphicsAPI::Resource*> dstBufArr;
					std::vector<GraphicsAPI::ResourceState::State> desiredStateArr;

					for (std::unique_ptr<GraphicsAPI::Texture>& resource : m_resources) {
						dstBufArr.push_back(resource.get());
						desiredStateArr.push_back(GraphicsAPI::ResourceState::State::Common);
					}

					for (std::unique_ptr<GraphicsAPI::Texture>& resource : m_namedResources) {
						if (resource.get()) {
							dstBufArr.push_back(resource.get());
							desiredStateArr.push_back(GraphicsAPI::ResourceState::State::Common);
						}
					}
					cmdList->ResourceTransitionBarrier(dstBufArr.data(), dstBufArr.size(), desiredStateArr.data());
				}
			}
		}

		return Status::OK;
	}
#endif

	// Prevent inlining to allow forward declaration of internal render passes as unique_ptr
	RenderPass_DirectLightingCacheDenoising::RenderPass_DirectLightingCacheDenoising() = default;
	RenderPass_DirectLightingCacheDenoising::~RenderPass_DirectLightingCacheDenoising() = default;
	RenderPass_DirectLightingCacheDenoising::RenderPass_DirectLightingCacheDenoising(RenderPass_DirectLightingCacheDenoising&&) = default;
	RenderPass_DirectLightingCacheDenoising& RenderPass_DirectLightingCacheDenoising::operator=(RenderPass_DirectLightingCacheDenoising&&) = default;

	Status RenderPass_DirectLightingCacheDenoising::Init(PersistentWorkingSet* pws, const DenoisingContextInput& context, ShaderFactory::Factory* sf) {
#if (KickstartRT_SDK_WITH_NRD)
		if (context.denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Reblur ||
			context.denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Relax ||
			context.denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Sigma) {
			m_nrd = std::make_unique<RenderPass_NRDDenoising>();
			RETURN_IF_STATUS_FAILED(m_nrd->Init(pws, context, sf));
		}
		else {
			assert(false);
		}
#else
		pws;
		context;
		sf;
#endif
		return Status::OK;
	}

	Status RenderPass_DirectLightingCacheDenoising::DeferredRelease(PersistentWorkingSet* pws) {
#if (KickstartRT_SDK_WITH_NRD)
		if (m_nrd)
			return m_nrd->DeferredRelease(pws);
#else
		pws;
#endif
		return Status::OK;
	}

	Status RenderPass_DirectLightingCacheDenoising::BuildCommandList(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList, RenderPass_ResourceRegistry* resources, const RenderTask::DenoisingOutput& output) {
#if (KickstartRT_SDK_WITH_NRD)
		RETURN_IF_STATUS_FAILED(m_nrd->BuildCommandList(tws, cmdList, resources, output));
#else
		tws;
		cmdList;
		output;
#endif
		return Status::OK;
	}
};
