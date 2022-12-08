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
#include <PersistentWorkingSet.h>

namespace KickstartRT_NativeLayer
{
	class TaskWorkingSet;
	struct RenderPass_ResourceRegistry;

	namespace RenderTask {
		struct ShadowParams {

			// Global Light list
			LightInfo lightInfos[32];
			uint32_t numLights = 0;

			// Input mask can be used to control which lights will be traced against on a per-pixel granularity.
			// uint-uint4 for managing up to 128 scene lights.
			// (Not implemented)
			ShaderResourceTex	lightSelectionMask;

			// Enabling usually results in more efficient shadow tracing. 
			// While it may resut in less accurate results for the denoiser that may require an accurate and stable hit distance
			bool				enableFirstHitAndEndSearch = true;
		};

		struct DenoisingOutput {

			enum class Mode : uint32_t {
				// (Default) Regular history accumulation mode
				Continue,

				// Discard and clear the history buffer
				DiscardHistory
			};

			// Required.
			DenoisingContextHandle context = DenoisingContextHandle::Null;
			Mode mode = Mode::Continue;

			HalfResolutionMode	halfResolutionMode = HalfResolutionMode::OFF;

			// Required for all signal types
			Viewport			viewport;
			DepthInput			depth;
			NormalInput			normal;
			RoughnessInput		roughness;
			// Motion is required for high quality denoising. Optional for debug purposes only
			MotionInput			motion;

			Math::Float_4x4		clipToViewMatrix = Math::Float_4x4::Identity();		// (Pos_View) = (Pos_CliP) * (M)
			Math::Float_4x4		viewToClipMatrix = Math::Float_4x4::Identity();		// (Pos_CliP) = (Pos_View) * (M)
			Math::Float_4x4		viewToClipMatrixPrev = Math::Float_4x4::Identity();		// (Pos_CliP) = (Pos_View) * (M)
			Math::Float_4x4		worldToViewMatrix = Math::Float_4x4::Identity();		// (Pos_View) = (Pos_World) * (M)
			Math::Float_4x4		worldToViewMatrixPrev = Math::Float_4x4::Identity();		// (Pos_View) = (Pos_World) * (M)
			Math::Float_2		cameraJitter = { 0.f, 0.f };

			// Required for SignalType DiffuseOcclusion
			Math::Float_4		occlusionHitTMask;

			// Required when running SignalType::Shadows. 
			ShadowParams		shadow;

			// Optional
			InputMaskInput		inputMask;

			// Required for SignalType Specular and SpecularAndDiffuse
			ShaderResourceTex	inSpecular;
			// Required for SignalType Specular and SpecularAndDiffuse
			CombinedAccessTex	inOutSpecular;

			// Required for SignalType Diffuse and SpecularAndDiffuse
			ShaderResourceTex	inDiffuse;
			// Required for SignalType Diffuse and SpecularAndDiffuse
			CombinedAccessTex	inOutDiffuse;

			// Required for SignalType DiffuseOcclusion
			ShaderResourceTex	inHitT;
			// Required for SignalType DiffuseOcclusion
			CombinedAccessTex	inOutOcclusion;

			// Required for SignalType Shadow and MultiShadow
			// RG16f+		- Opaque NRD denoising data.
			ShaderResourceTex	inShadow0;
			// Required for SignalType MultiShadow
			// RGBA8+		- Opaque NRD denoising data.
			ShaderResourceTex	inShadow1;
			// Required for SignalType Shadow and MultiShadow
			// Shadow		- R8+, R - Shadow value
			// MultiShadow	- RGBA8+, R - Shadow value. GBA - Opaque history data
			CombinedAccessTex	inOutShadow;

			Status ConvertFromRenderTask(const RenderTask::Task* task);
		};
	};
};

namespace KickstartRT_NativeLayer
{
	class RenderPass_NRDDenoising;

	struct RenderPass_Denoising
	{
#if (KickstartRT_SDK_WITH_NRD)
		std::unique_ptr<RenderPass_NRDDenoising>		m_nrd;
#endif
	public:
		RenderPass_Denoising();
		~RenderPass_Denoising();
		RenderPass_Denoising(RenderPass_Denoising&&);
		RenderPass_Denoising& operator=(RenderPass_Denoising&& other);
		Status Init(PersistentWorkingSet* pws, const DenoisingContextInput& context, ShaderFactory::Factory* sf);
		Status DeferredRelease(PersistentWorkingSet* pws);
		Status BuildCommandList(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList, RenderPass_ResourceRegistry* resources, const RenderTask::DenoisingOutput& reflectionOutputs);
	};
};
