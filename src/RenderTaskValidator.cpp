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
#include <Platform.h>
#include <Utils.h>
#include <Log.h>
#include <RenderTaskValidator.h>
#include <Geometry.h>
#include <DenoisingContext.h>

namespace KickstartRT_NativeLayer
{
	Status RenderTaskValidator::DirectLightingInjectionTask(const RenderTask::DirectLightingInjectionTask* input)
	{
		if (input->viewport.width == 0 || input->viewport.height == 0) {
			Log::Fatal(L"Ivaid viewport rect was detected.");
			return Status::ERROR_INVALID_PARAM;
		}

		const GraphicsAPI::TexValidator depth("depth", input->depth.tex);
		RETURN_IF_STATUS_FAILED(depth.AssertIsNotNull());

		const GraphicsAPI::TexValidator directLighting("directLighting", input->directLighting);
		RETURN_IF_STATUS_FAILED(directLighting.AssertIsNotNull());

		return Status::OK;
	}

	Status RenderTaskValidator::DirectLightTransferTask(const RenderTask::DirectLightTransferTask* input)
	{
		if (input->target == InstanceHandle::Null) {
			Log::Fatal(L"Target must be a non-null instance");
			return Status::ERROR_INVALID_PARAM;
		}

		BVHTask::Instance* inst = BVHTask::Instance::ToPtr(input->target);
		if (!inst) {
			Log::Fatal(L"Bad instance handle.");
			return Status::ERROR_INTERNAL;
		}

		if (!inst->m_geometry->m_input.allowLightTransferTarget) {
			Log::Fatal(L"Target geometry must be built with allowLightTransferTarget=true");
			return Status::ERROR_INVALID_PARAM;
		}

		if (inst->m_geometry->m_input.surfelType != BVHTask::GeometryInput::SurfelType::WarpedBarycentricStorage) {
			Log::Fatal(L"Target geometry must be built with surfelType=WarpedBarycentricStorage");
			return Status::ERROR_INVALID_PARAM;
		}

		return Status::OK;
	}

	static Status ValidateTraceCommon(const RenderTask::TraceTaskCommon& common)
	{
		if (common.viewport.width == 0 || common.viewport.height == 0) {
			Log::Fatal(L"Invaild viewport rect was detected.");
			return Status::ERROR_INVALID_PARAM;
		}
		const GraphicsAPI::TexValidator depth("depth", common.depth.tex);
		RETURN_IF_STATUS_FAILED(depth.AssertIsNotNull());

		const GraphicsAPI::TexValidator normal("normal", common.normal.tex);
		RETURN_IF_STATUS_FAILED(normal.AssertIsNotNull());

		return Status::OK;
	}

	Status RenderTaskValidator::TraceTask(const RenderTask::Task* task)
	{
		switch (task->type) {
		case RenderTask::Task::Type::TraceSpecular:
		case RenderTask::Task::Type::TraceDiffuse:
		case RenderTask::Task::Type::TraceAmbientOcclusion:
		case RenderTask::Task::Type::TraceShadow:
		case RenderTask::Task::Type::TraceMultiShadow:
			break;
		default:
			Log::Fatal(L"Ivaid taks type detected when validating a trace task.");
			return Status::ERROR_INTERNAL;
		}

		if (task->type == RenderTask::Task::Type::TraceSpecular) {
			auto& spec(*static_cast<const RenderTask::TraceSpecularTask*>(task));
			ValidateTraceCommon(spec.common);

			if (spec.demodulateSpecular && spec.common.halfResolutionMode != RenderTask::HalfResolutionMode::OFF) {
				Log::Fatal(L"Demodule specular is not compatible with checkerboarding");
				return Status::ERROR_INTERNAL;
			}

			const GraphicsAPI::TexValidator uavOutput("out", spec.out);
			RETURN_IF_STATUS_FAILED(uavOutput.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(uavOutput.AssertChannelCount({ 4 }));
		}
		else if (task->type == RenderTask::Task::Type::TraceDiffuse) {
			auto& diff(*static_cast<const RenderTask::TraceDiffuseTask*>(task));
			ValidateTraceCommon(diff.common);

			const GraphicsAPI::TexValidator uavOutput("out", diff.out);
			RETURN_IF_STATUS_FAILED(uavOutput.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(uavOutput.AssertChannelCount({ 4 }));
		}
		else if (task->type == RenderTask::Task::Type::TraceAmbientOcclusion) {
			auto& ao(*static_cast<const RenderTask::TraceAmbientOcclusionTask*>(task));
			ValidateTraceCommon(ao.common);

			const GraphicsAPI::TexValidator uavOutput("out", ao.out);
			RETURN_IF_STATUS_FAILED(uavOutput.AssertIsNotNull());
		}
		else if (task->type == RenderTask::Task::Type::TraceShadow) {
			auto& shadow(*static_cast<const RenderTask::TraceShadowTask*>(task));
			ValidateTraceCommon(shadow.common);

			const GraphicsAPI::TexValidator uavShadow0("out", shadow.out);
			RETURN_IF_STATUS_FAILED(uavShadow0.AssertIsNotNull());
			//RETURN_IF_STATUS_FAILED(uavShadow0.AssertChannelCount({ 1, 2, 3, 4 }));
			RETURN_IF_STATUS_FAILED(uavShadow0.AssertFormatType({ GraphicsAPI::Resource::FormatType::Float }));
		}
		else if (task->type == RenderTask::Task::Type::TraceMultiShadow) {
			auto& mShadow(*static_cast<const RenderTask::TraceMultiShadowTask*>(task));
			ValidateTraceCommon(mShadow.common);

			const GraphicsAPI::TexValidator uavShadow0("out", mShadow.out0);
			RETURN_IF_STATUS_FAILED(uavShadow0.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(uavShadow0.AssertChannelCount({ 2, 3, 4 }));
			RETURN_IF_STATUS_FAILED(uavShadow0.AssertFormatType({ GraphicsAPI::Resource::FormatType::Float }));

			const GraphicsAPI::TexValidator uavShadow1("outAux", mShadow.out1);
			RETURN_IF_STATUS_FAILED(uavShadow1.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(uavShadow1.AssertChannelCount({ 4 }));
			RETURN_IF_STATUS_FAILED(uavShadow1.AssertFormatType(
				{ GraphicsAPI::Resource::FormatType::Float, GraphicsAPI::Resource::FormatType::Unorm }));
		}
		else {
			return Status::ERROR_INTERNAL;
		}

		return Status::OK;
	}

	Status RenderTaskValidator::DenoisingTask(const RenderTask::Task* task)
	{
		using namespace RenderTask;

		DenoisingContextHandle dh = DenoisingContextHandle::Null;

		const DenoisingTaskCommon* common = nullptr;
		const ShaderResourceTex* inSpecular = nullptr;
		const CombinedAccessTex* inOutSpecular = nullptr;
		const ShaderResourceTex* inDiffuse = nullptr;
		const CombinedAccessTex* inOutDiffuse = nullptr;
		const ShaderResourceTex* inHitT = nullptr;
		const CombinedAccessTex* inOutOcclusion = nullptr;
		const ShaderResourceTex* inShadow0 = nullptr;
		const ShaderResourceTex* inShadow1 = nullptr;
		const CombinedAccessTex* inOutShadow = nullptr;
		const ShaderResourceTex* lightSelectionMask = nullptr; // not implemented yet.

		if (task->type == RenderTask::Task::Type::DenoiseSpecular) {
			auto& dSpec(*static_cast<const RenderTask::DenoiseSpecularTask*>(task));
			dh = dSpec.context;
			common = &dSpec.common;
			inSpecular = &dSpec.inSpecular;
			inOutSpecular = &dSpec.inOutSpecular;
		}
		else if (task->type == RenderTask::Task::Type::DenoiseDiffuse) {
			auto& dDiff(*static_cast<const RenderTask::DenoiseDiffuseTask*>(task));
			dh = dDiff.context;
			common = &dDiff.common;
			inDiffuse = &dDiff.inDiffuse;
			inOutDiffuse = &dDiff.inOutDiffuse;
		}
		else if (task->type == RenderTask::Task::Type::DenoiseSpecularAndDiffuse) {
			auto& dSpecDiff(*static_cast<const RenderTask::DenoiseSpecularAndDiffuseTask*>(task));
			dh = dSpecDiff.context;
			common = &dSpecDiff.common;
			inSpecular = &dSpecDiff.inSpecular;
			inOutSpecular = &dSpecDiff.inOutSpecular;
			inDiffuse = &dSpecDiff.inDiffuse;
			inOutDiffuse = &dSpecDiff.inOutDiffuse;
		}
		else if (task->type == RenderTask::Task::Type::DenoiseDiffuseOcclusion) {
			auto& dOcclusion(*static_cast<const RenderTask::DenoiseDiffuseOcclusionTask*>(task));
			dh = dOcclusion.context;
			common = &dOcclusion.common;
			inHitT = &dOcclusion.inHitT;
			inOutOcclusion = &dOcclusion.inOutOcclusion;
		}
		else if (task->type == RenderTask::Task::Type::DenoiseShadow) {
			auto& dShadow(*static_cast<const RenderTask::DenoiseShadowTask*>(task));
			dh = dShadow.context;
			common = &dShadow.common;
			inShadow0 = &dShadow.inShadow;
			inOutShadow = &dShadow.inOutShadow;
		}
		else if (task->type == RenderTask::Task::Type::DenoiseMultiShadow) {
			auto& dShadow(*static_cast<const RenderTask::DenoiseMultiShadowTask*>(task));
			dh = dShadow.context;
			common = &dShadow.common;
			inShadow0 = &dShadow.inShadow0;
			inShadow1 = &dShadow.inShadow1;
			inOutShadow = &dShadow.inOutShadow;
		}
		else {
			Log::Fatal(L"Invalid task type detected while validating a denoising task.");
			return Status::ERROR_INTERNAL;
		}

		if (dh == DenoisingContextHandle::Null) {
			Log::Fatal(L"Invaild context handle was detected.");
			return Status::ERROR_INVALID_PARAM;
		}

		DenoisingContext* context = DenoisingContext::ToPtr(dh);

		if (context->m_input.signalType == DenoisingContextInput::SignalType::Specular ||
			context->m_input.signalType == DenoisingContextInput::SignalType::SpecularAndDiffuse) {

			if (inSpecular == nullptr || inOutSpecular == nullptr) {
				Log::Fatal(L"Invalid specular input texture detected while validating a denoising task.");
				return Status::ERROR_INTERNAL;
			}

			const GraphicsAPI::TexValidator inSpecularV("inSpecular", *inSpecular);
			RETURN_IF_STATUS_FAILED(inSpecularV.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inSpecularV.AssertChannelCount({ 4 }));
			RETURN_IF_STATUS_FAILED(inSpecularV.AssertFormatType({
				GraphicsAPI::Resource::FormatType::Unorm, GraphicsAPI::Resource::FormatType::Float }));

			const GraphicsAPI::TexValidator inOutSpecularV("inOutSpecular", *inOutSpecular);
			RETURN_IF_STATUS_FAILED(inOutSpecularV.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inOutSpecularV.AssertChannelCount({ 4 }));
			RETURN_IF_STATUS_FAILED(inOutSpecularV.AssertFormatType({
				GraphicsAPI::Resource::FormatType::Unorm, GraphicsAPI::Resource::FormatType::Float }));
		}

		if (context->m_input.signalType == DenoisingContextInput::SignalType::Diffuse ||
			context->m_input.signalType == DenoisingContextInput::SignalType::SpecularAndDiffuse) {

			if (inDiffuse == nullptr || inOutDiffuse == nullptr) {
				Log::Fatal(L"Invalid diffuse input texture detected while validating a denoising task.");
				return Status::ERROR_INTERNAL;
			}

			const GraphicsAPI::TexValidator inDiffuseV("inDiffuse", *inDiffuse);
			RETURN_IF_STATUS_FAILED(inDiffuseV.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inDiffuseV.AssertChannelCount({ 4 }));
			RETURN_IF_STATUS_FAILED(inDiffuseV.AssertFormatType({
				GraphicsAPI::Resource::FormatType::Unorm, GraphicsAPI::Resource::FormatType::Float }));

			const GraphicsAPI::TexValidator inOutDiffuseV("inOutDiffuse", *inOutDiffuse);
			RETURN_IF_STATUS_FAILED(inOutDiffuseV.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inOutDiffuseV.AssertChannelCount({ 4 }));
			RETURN_IF_STATUS_FAILED(inOutDiffuseV.AssertFormatType({
				GraphicsAPI::Resource::FormatType::Unorm, GraphicsAPI::Resource::FormatType::Float }));
		}

		if (context->m_input.signalType == DenoisingContextInput::SignalType::DiffuseOcclusion) {

			if (inHitT == nullptr || inOutOcclusion == nullptr) {
				Log::Fatal(L"Invalid diffuse occlusion input texture detected while validating a denoising task.");
				return Status::ERROR_INTERNAL;
			}

			const GraphicsAPI::TexValidator inHitTV("inHitT", *inHitT);
			RETURN_IF_STATUS_FAILED(inHitTV.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inHitTV.AssertChannelCount({ 1, 2, 3, 4 }));
			RETURN_IF_STATUS_FAILED(inHitTV.AssertFormatType({
				GraphicsAPI::Resource::FormatType::Unorm, GraphicsAPI::Resource::FormatType::Float }));

			const GraphicsAPI::TexValidator inOutOcclusionV("inOutOcclusion", *inOutOcclusion);
			RETURN_IF_STATUS_FAILED(inOutOcclusionV.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inOutOcclusionV.AssertChannelCount({ 1, 2, 3, 4 }));
			RETURN_IF_STATUS_FAILED(inOutOcclusionV.AssertFormatType({
				GraphicsAPI::Resource::FormatType::Unorm, GraphicsAPI::Resource::FormatType::Float }));
		}

		if (context->m_input.signalType == DenoisingContextInput::SignalType::Shadow) {

			if (common->halfResolutionMode != RenderTask::HalfResolutionMode::OFF)
				return Status::ERROR_INVALID_PARAM;

			if (inShadow0 == nullptr || inOutShadow == nullptr) {
				Log::Fatal(L"Invalid shadow input texture detected while validating a denoising task.");
				return Status::ERROR_INTERNAL;
			}

			const GraphicsAPI::TexValidator inShadow0V("inShadow0", *inShadow0);
			RETURN_IF_STATUS_FAILED(inShadow0V.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inShadow0V.AssertChannelCount({ 2 }));
			RETURN_IF_STATUS_FAILED(inShadow0V.AssertFormatType({ GraphicsAPI::Resource::FormatType::Float }));

			const GraphicsAPI::TexValidator inOutShadowV("inOutShadow", *inOutShadow);
			RETURN_IF_STATUS_FAILED(inOutShadowV.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inOutShadowV.AssertChannelCount({ 1, 2, 3, 4 }));
			RETURN_IF_STATUS_FAILED(inOutShadowV.AssertFormatType({
				GraphicsAPI::Resource::FormatType::Unorm, GraphicsAPI::Resource::FormatType::Float }));

			if (lightSelectionMask != nullptr) {
				const GraphicsAPI::TexValidator lightSelectionMaskV("shadow.lightSelectionMask", *lightSelectionMask);
				if (!lightSelectionMaskV.IsNull()) {
					RETURN_IF_STATUS_FAILED(lightSelectionMaskV.AssertIsNotNull());
					RETURN_IF_STATUS_FAILED(lightSelectionMaskV.AssertChannelCount({ 1, 2, 3, 4 }));
					RETURN_IF_STATUS_FAILED(lightSelectionMaskV.AssertFormatType({ GraphicsAPI::Resource::FormatType::Uint }));
				}
			}
		}

		if (context->m_input.signalType == DenoisingContextInput::SignalType::MultiShadow) {

			if (common->halfResolutionMode != RenderTask::HalfResolutionMode::OFF)
				return Status::ERROR_INVALID_PARAM;

			if (inShadow0 == nullptr || inShadow1 == nullptr || inOutShadow == nullptr) {
				Log::Fatal(L"Invalid shadow input texture detected while validating a denoising task.");
				return Status::ERROR_INTERNAL;
			}

			const GraphicsAPI::TexValidator inShadow0V("inShadow0", *inShadow0);
			RETURN_IF_STATUS_FAILED(inShadow0V.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inShadow0V.AssertChannelCount({ 2 }));
			RETURN_IF_STATUS_FAILED(inShadow0V.AssertFormatType({ GraphicsAPI::Resource::FormatType::Float }));

			const GraphicsAPI::TexValidator inShadow1V("inShadow1", *inShadow1);
			RETURN_IF_STATUS_FAILED(inShadow1V.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inShadow1V.AssertChannelCount({ 4 }));
			RETURN_IF_STATUS_FAILED(inShadow1V.AssertFormatType({
				GraphicsAPI::Resource::FormatType::Unorm, GraphicsAPI::Resource::FormatType::Float }));

			const GraphicsAPI::TexValidator inOutShadowV("inOutShadow", *inOutShadow);
			RETURN_IF_STATUS_FAILED(inOutShadowV.AssertIsNotNull());
			RETURN_IF_STATUS_FAILED(inOutShadowV.AssertChannelCount({ 4 }));
			RETURN_IF_STATUS_FAILED(inOutShadowV.AssertFormatType({
				GraphicsAPI::Resource::FormatType::Unorm, GraphicsAPI::Resource::FormatType::Float }));
		}

		const GraphicsAPI::TexValidator depthV("depth", common->depth.tex);
		RETURN_IF_STATUS_FAILED(depthV.AssertIsNotNull());

		const GraphicsAPI::TexValidator normalV("normal", common->normal.tex);
		RETURN_IF_STATUS_FAILED(normalV.AssertIsNotNull());

		if (!common->debugDisableMotion) {
			const GraphicsAPI::TexValidator motionV("motion", common->motion.tex);
			RETURN_IF_STATUS_FAILED(motionV.AssertIsNotNull());
		}

		if (common->viewport.width == 0 || common->viewport.height == 0) {
			Log::Fatal(L"Invaild viewport rect was detected.");
			return Status::ERROR_INVALID_PARAM;
		}

		return Status::OK;
	};

};
