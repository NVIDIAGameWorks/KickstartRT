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
#include <ExecuteContext.h>
#include <TaskContainer.h>
#include <PersistentWorkingSet.h>
#include <Log.h>

namespace KickstartRT_ExportLayer
{
	TaskContainer::TaskContainer()
	{
	};

	TaskContainer::~TaskContainer()
	{
	};

	TaskContainer_impl::~TaskContainer_impl()
	{
		if (m_taskContainer_12 != nullptr) {
			Log::Fatal(L"TaskContainer for D3D12 was not null.");
			assert(false);
		}
	};

	static void ConvertGeometryInput(InteropCacheSet* cs, const D3D11::BVHTask::GeometryInput* input_11, D3D12::BVHTask::GeometryInput* input_12, intptr_t usedTaskContainer)
	{
		auto& src(*input_11);
		auto& dst(*input_12);

		{
			dst.indexBuffer.count = src.indexBuffer.count;
			dst.indexBuffer.format = src.indexBuffer.format;
			dst.indexBuffer.offsetInBytes = src.indexBuffer.offsetInBytes;
#if 0
			// Common state promotion (CMN -> NonPixShRes) should happen in D3D12 KS SDK. 
			dst.indexBuffer.m_resourceState = D3D12_RESOURCE_STATE_COMMON;
#endif

			if (cs->ConvertGeometry(src.indexBuffer.resource, &dst.indexBuffer.resource, usedTaskContainer) != Status::OK) {
				Log::Fatal(L"Failed to convert index buffer resource.");
			}

			dst.indexRange.isEnabled = src.indexRange.isEnabled;
			dst.indexRange.maxIndex = src.indexRange.maxIndex;
			dst.indexRange.minIndex = src.indexRange.minIndex;
		}
		{
			dst.vertexBuffer.count = src.vertexBuffer.count;
			dst.vertexBuffer.format = src.vertexBuffer.format;
			dst.vertexBuffer.offsetInBytes = src.vertexBuffer.offsetInBytes;
#if 0
			// Common state promotion (CMN -> NonPixShRes) should happen in D3D12 KS SDK. 
			dst.vertexBuffer.resourceState = D3D12_RESOURCE_STATE_COMMON;
#endif
			dst.vertexBuffer.strideInBytes = src.vertexBuffer.strideInBytes;

			if (cs->ConvertGeometry(src.vertexBuffer.resource, &dst.vertexBuffer.resource, usedTaskContainer) != Status::OK) {
				Log::Fatal(L"Failed to convert vertex buffer resource.");
			}
		}

		dst.allowUpdate = src.allowUpdate;
		dst.directTileMappingThreshold = src.directTileMappingThreshold;
		dst.forceDirectTileMapping = src.forceDirectTileMapping;
		dst.surfelType = (D3D12::BVHTask::GeometryInput::SurfelType)src.surfelType;
		dst.buildHint = (D3D12::BVHTask::GeometryInput::BuildHint)src.buildHint;
		dst.name = src.name;
		dst.tileResolutionLimit = src.tileResolutionLimit;
		dst.tileUnitLength = src.tileUnitLength;
		dst.transform = src.transform;
		dst.type = static_cast<decltype(dst.type)>(src.type);
		dst.useTransform = src.useTransform;

	}

	static void ConvertInstanceInput(InteropCacheSet* cs, const D3D11::BVHTask::InstanceInput* input_11, D3D12::BVHTask::InstanceInput* input_12)
	{
		(void)cs;
		auto& src(*input_11);
		auto& dst(*input_12);

		static_assert(sizeof(src.geomHandle) == sizeof(uint64_t));
		static_assert(sizeof(dst.geomHandle) == sizeof(uint64_t));
		dst.geomHandle = static_cast<D3D12::GeometryHandle>(src.geomHandle);
		dst.name = src.name;
		dst.transform = src.transform;
	}

	static void ConvertGeometryTask(InteropCacheSet* cs, const D3D11::BVHTask::GeometryTask* task_11, D3D12::BVHTask::GeometryTask* task_12, intptr_t usedTaskContainer)
	{
		task_12->taskOperation = (D3D12::BVHTask::TaskOperation)task_11->taskOperation;
		task_12->handle = (D3D12::GeometryHandle)task_11->handle;
		ConvertGeometryInput(cs, &task_11->input, &task_12->input, usedTaskContainer);
	};

	static void ConvertInstanceTask(InteropCacheSet* cs, const D3D11::BVHTask::InstanceTask* task_11, D3D12::BVHTask::InstanceTask* task_12)
	{
		task_12->taskOperation = (D3D12::BVHTask::TaskOperation)task_11->taskOperation;
		task_12->handle = (D3D12::InstanceHandle)task_11->handle;
		ConvertInstanceInput(cs, &task_11->input, &task_12->input);
	};

	static void ConvertBVHBuildTask(const D3D11::BVHTask::BVHBuildTask* task_11, D3D12::BVHTask::BVHBuildTask* task_12)
	{
		task_12->maxBlasBuildCount = task_12->maxBlasBuildCount;
		task_12->buildTLAS = task_11->buildTLAS;
	};

	Status TaskContainer_impl::ScheduleBVHTask(const BVHTask::Task* bvhTask)
	{
		return ScheduleBVHTasks(&bvhTask, 1);
	}

	Status TaskContainer_impl::ScheduleBVHTasks(const BVHTask::Task* const * bvhTaskPtrArr, uint32_t nbTasks)
	{
		for (uint32_t i = 0; i < nbTasks; ++i) {
			auto* t = bvhTaskPtrArr[i];

			switch (t->type) {
			case BVHTask::Task::Type::Geometry:
			{
				D3D12::BVHTask::GeometryTask task_12;
				ConvertGeometryTask(m_interopCacheSet, static_cast<const D3D11::BVHTask::GeometryTask*>(t), &task_12, reinterpret_cast<intptr_t>(this));
				Status sts = m_taskContainer_12->ScheduleBVHTask(&task_12);
				if (sts != Status::OK) {
					return sts;
				}
			}
			break;

			case BVHTask::Task::Type::Instance:
			{
				D3D12::BVHTask::InstanceTask task_12;
				ConvertInstanceTask(m_interopCacheSet, static_cast<const D3D11::BVHTask::InstanceTask*>(t), &task_12);
				Status sts = m_taskContainer_12->ScheduleBVHTask(&task_12);
				if (sts != Status::OK) {
					return sts;
				}
			}
			break;

			case BVHTask::Task::Type::BVHBuild:
			{
				D3D12::BVHTask::BVHBuildTask task_12;
				ConvertBVHBuildTask(static_cast<const D3D11::BVHTask::BVHBuildTask*>(t), &task_12);
				Status sts = m_taskContainer_12->ScheduleBVHTask(&task_12);
				if (sts != Status::OK) {
					return sts;
				}
			}
			break;

			default:
			{
				Log::Fatal(L"Unknown BVH task detected in D3D11 layer.");
				return Status::ERROR_INVALID_PARAM;
			}
			break;
			}
		}

		return Status::OK;
	}

	static D3D12_SHADER_RESOURCE_VIEW_DESC Convert(const D3D11_SHADER_RESOURCE_VIEW_DESC& src)
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv12 = {};

		auto cv = [](const D3D11_SRV_DIMENSION& dim_11) {
#if 0
			D3D_SRV_DIMENSION_UNKNOWN = 0,
				D3D_SRV_DIMENSION_BUFFER = 1,
				D3D_SRV_DIMENSION_TEXTURE1D = 2,
				D3D_SRV_DIMENSION_TEXTURE1DARRAY = 3,
				D3D_SRV_DIMENSION_TEXTURE2D = 4,
				D3D_SRV_DIMENSION_TEXTURE2DARRAY = 5,
				D3D_SRV_DIMENSION_TEXTURE2DMS = 6,
				D3D_SRV_DIMENSION_TEXTURE2DMSARRAY = 7,
				D3D_SRV_DIMENSION_TEXTURE3D = 8,
				D3D_SRV_DIMENSION_TEXTURECUBE = 9,
				D3D_SRV_DIMENSION_TEXTURECUBEARRAY = 10,
				D3D_SRV_DIMENSION_BUFFEREX = 11,

				D3D12_SRV_DIMENSION_UNKNOWN = 0,
				D3D12_SRV_DIMENSION_BUFFER = 1,
				D3D12_SRV_DIMENSION_TEXTURE1D = 2,
				D3D12_SRV_DIMENSION_TEXTURE1DARRAY = 3,
				D3D12_SRV_DIMENSION_TEXTURE2D = 4,
				D3D12_SRV_DIMENSION_TEXTURE2DARRAY = 5,
				D3D12_SRV_DIMENSION_TEXTURE2DMS = 6,
				D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY = 7,
				D3D12_SRV_DIMENSION_TEXTURE3D = 8,
				D3D12_SRV_DIMENSION_TEXTURECUBE = 9,
				D3D12_SRV_DIMENSION_TEXTURECUBEARRAY = 10
#endif
			if ((uint32_t)dim_11 < 11)
				return (D3D12_SRV_DIMENSION)dim_11;

			if (dim_11 == D3D_SRV_DIMENSION_BUFFEREX)
				return D3D12_SRV_DIMENSION_BUFFER;

			return D3D12_SRV_DIMENSION_UNKNOWN;
		};

		srv12.ViewDimension = cv(src.ViewDimension);
		srv12.Format = src.Format;
		srv12.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		if (srv12.ViewDimension == D3D12_SRV_DIMENSION_BUFFER) {
			if (src.ViewDimension == D3D_SRV_DIMENSION_BUFFEREX) {
				srv12.Buffer.FirstElement = src.BufferEx.FirstElement;
				srv12.Buffer.NumElements = src.BufferEx.NumElements;
				srv12.Buffer.Flags = src.BufferEx.Flags == D3D11_BUFFEREX_SRV_FLAG_RAW ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
				srv12.Buffer.StructureByteStride = 0;
			}
			else {
				srv12.Buffer.FirstElement = src.Buffer.FirstElement;
				srv12.Buffer.NumElements = src.Buffer.NumElements;
				srv12.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
				srv12.Buffer.StructureByteStride = src.Buffer.ElementWidth;
			}
		}
		else if (srv12.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE1D) {
			srv12.Texture1D.MipLevels = src.Texture1D.MipLevels;
			srv12.Texture1D.MostDetailedMip = src.Texture1D.MostDetailedMip;
			srv12.Texture1D.ResourceMinLODClamp = 0.f;
		}
		else if (srv12.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE1DARRAY) {
			srv12.Texture1DArray.ArraySize = src.Texture1DArray.ArraySize;
			srv12.Texture1DArray.FirstArraySlice = src.Texture1DArray.FirstArraySlice;
			srv12.Texture1DArray.MipLevels = src.Texture1DArray.MipLevels;
			srv12.Texture1DArray.MostDetailedMip = src.Texture1DArray.MostDetailedMip;
			srv12.Texture1DArray.ResourceMinLODClamp = 0.f;
		}
		else if (srv12.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D) {
			srv12.Texture2D.MipLevels = src.Texture2D.MipLevels;
			srv12.Texture2D.MostDetailedMip = src.Texture2D.MostDetailedMip;
			srv12.Texture2D.PlaneSlice = 0;
			srv12.Texture2D.ResourceMinLODClamp = 0.f;
		}
		else if (srv12.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY) {
			srv12.Texture2DArray.ArraySize = src.Texture2DArray.ArraySize;
			srv12.Texture2DArray.FirstArraySlice = src.Texture2DArray.FirstArraySlice;
			srv12.Texture2DArray.MipLevels = src.Texture2DArray.MipLevels;
			srv12.Texture2DArray.MostDetailedMip = src.Texture2DArray.MostDetailedMip;
			srv12.Texture2DArray.PlaneSlice = 0;
			srv12.Texture2DArray.ResourceMinLODClamp = 0.f;
		}
		else if (srv12.ViewDimension == D3D12_SRV_DIMENSION_UNKNOWN) {
			srv12 = {};
		}
		else {
			// for now, we only need to support 2D textures.
			Log::Fatal(L"Unsupported SRV type detected.");
			return D3D12_SHADER_RESOURCE_VIEW_DESC{};
		}

		return srv12;
	}

	static void ConvertShaderResourceTex(InteropCacheSet *cs, const RenderTask::ShaderResourceTex* srt_11, D3D12::RenderTask::ShaderResourceTex* srt_12, intptr_t usedTaskContainer)
	{
		srt_12->srvDesc = Convert(srt_11->srvDesc);
		if (cs->ConvertTexture(srt_11->resource, &srt_12->resource, usedTaskContainer) != Status::OK) {
			Log::Fatal(L"Failed to convert texture resource.");
		}
#if 0
		// Common state promotion (CMN -> NonPixShRes) should happen in D3D12 KS SDK. 
		srt_12->m_resourceState = D3D12_RESOURCE_STATE_COMMON;
#endif
	}

	static D3D12_UNORDERED_ACCESS_VIEW_DESC Convert(const D3D11_UNORDERED_ACCESS_VIEW_DESC& src)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav12 = {};

		auto cv = [](const D3D11_UAV_DIMENSION& dim_11) {
#if 0
			D3D11_UAV_DIMENSION_UNKNOWN = 0,
				D3D11_UAV_DIMENSION_BUFFER = 1,
				D3D11_UAV_DIMENSION_TEXTURE1D = 2,
				D3D11_UAV_DIMENSION_TEXTURE1DARRAY = 3,
				D3D11_UAV_DIMENSION_TEXTURE2D = 4,
				D3D11_UAV_DIMENSION_TEXTURE2DARRAY = 5,
				D3D11_UAV_DIMENSION_TEXTURE3D = 8
				
				D3D12_UAV_DIMENSION_UNKNOWN = 0,
				D3D12_UAV_DIMENSION_BUFFER = 1,
				D3D12_UAV_DIMENSION_TEXTURE1D = 2,
				D3D12_UAV_DIMENSION_TEXTURE1DARRAY = 3,
				D3D12_UAV_DIMENSION_TEXTURE2D = 4,
				D3D12_UAV_DIMENSION_TEXTURE2DARRAY = 5,
				D3D12_UAV_DIMENSION_TEXTURE3D = 8
#endif
			return (D3D12_UAV_DIMENSION)dim_11;
		};

		uav12.ViewDimension = cv(src.ViewDimension);
		uav12.Format = src.Format;

		if (uav12.ViewDimension == D3D12_UAV_DIMENSION_BUFFER) {

			if (src.Buffer.Flags > D3D11_BUFFER_UAV_FLAG_RAW) {
				// APPEND and/or COUNTER
				Log::Fatal(L"Unsupported buffer flag detected.");
				return D3D12_UNORDERED_ACCESS_VIEW_DESC{};
			}
			uav12.Buffer.CounterOffsetInBytes = 0;
			uav12.Buffer.FirstElement = src.Buffer.FirstElement;
			uav12.Buffer.Flags = src.Buffer.Flags == D3D11_BUFFER_UAV_FLAG_RAW ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
			uav12.Buffer.NumElements = src.Buffer.NumElements;
			uav12.Buffer.StructureByteStride = 0;
		}
		else if (uav12.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE1D) {
			uav12.Texture1D.MipSlice = src.Texture1D.MipSlice;
		}
		else if (uav12.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE1DARRAY) {
			uav12.Texture1DArray.ArraySize = src.Texture1DArray.ArraySize;
			uav12.Texture1DArray.FirstArraySlice = src.Texture1DArray.FirstArraySlice;
			uav12.Texture1DArray.MipSlice = src.Texture1DArray.MipSlice;
		}
		else if (uav12.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D) {
			uav12.Texture2D.MipSlice = src.Texture2D.MipSlice;
			uav12.Texture2D.PlaneSlice = 0;
		}
		else if (uav12.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2DARRAY) {
			uav12.Texture2DArray.ArraySize = src.Texture2DArray.ArraySize;
			uav12.Texture2DArray.FirstArraySlice = src.Texture2DArray.FirstArraySlice;
			uav12.Texture2DArray.MipSlice = src.Texture2DArray.MipSlice;
			uav12.Texture2DArray.PlaneSlice = 0;
		}
		else if (uav12.ViewDimension == D3D12_UAV_DIMENSION_UNKNOWN) {
			uav12 = {};
		}
		else {
			// for now, we only need to support 2D textures.
			Log::Fatal(L"Unsupported UAV type detected.");
			return D3D12_UNORDERED_ACCESS_VIEW_DESC{};
		}

		return uav12;
	}

	static void ConvertUnorderedAccessTex(InteropCacheSet* cs, const RenderTask::UnorderedAccessTex* uat_11, D3D12::RenderTask::UnorderedAccessTex* uat_12, intptr_t usedTaskContainer)
	{
		uat_12->uavDesc = Convert(uat_11->uavDesc);
		if (cs->ConvertTexture(uat_11->resource, &uat_12->resource, usedTaskContainer) != Status::OK) {
			Log::Fatal(L"Failed to convert unordered tex.");
		}

#if 0
		// Common state promotion (CMN -> UA) should happen in D3D12 KS SDK. 
		uat_12->resourceState = D3D12_RESOURCE_STATE_COMMON;
#endif
	}

	static void ConvertCombinedAccessTex(InteropCacheSet* cs, const RenderTask::CombinedAccessTex * uat_11, D3D12::RenderTask::CombinedAccessTex * uat_12, intptr_t usedTaskContainer)
	{
		uat_12->srvDesc = Convert(uat_11->srvDesc);
		uat_12->uavDesc = Convert(uat_11->uavDesc);
		if (cs->ConvertTexture(uat_11->resource, &uat_12->resource, usedTaskContainer) != Status::OK) {
			Log::Fatal(L"Failed to convert combined access tex.");
		}

#if 0
		// Common state promotion (CMN -> UA) should happen in D3D12 KS SDK. 
		uat_12->resourceState = D3D12_RESOURCE_STATE_COMMON;
#endif
	}

	static void ConvertViewport(const D3D11::RenderTask::Viewport &v11, D3D12::RenderTask::Viewport& v12)
	{
		static_assert(sizeof(v12) == sizeof(v11));
		memcpy(&v12, &v11, sizeof(v12));
	}

	static void ConvertDepthInput(InteropCacheSet* cs, const D3D11::RenderTask::DepthInput* src, D3D12::RenderTask::DepthInput* dst, intptr_t usedTaskContainer)
	{
		dst->type = (D3D12::RenderTask::DepthType)src->type;
		ConvertShaderResourceTex(cs, &src->tex, &dst->tex, usedTaskContainer);
	}

	static void ConvertNormalInput(InteropCacheSet* cs, const D3D11::RenderTask::NormalInput* src, D3D12::RenderTask::NormalInput* dst, intptr_t usedTaskContainer)
	{
		dst->normalToWorldMatrix = src->normalToWorldMatrix;
		dst->type = (D3D12::RenderTask::NormalType)src->type;
		ConvertShaderResourceTex(cs, &src->tex, &dst->tex, usedTaskContainer);
	}

	static void ConvertInputMaskInput(InteropCacheSet* cs, const D3D11::RenderTask::InputMaskInput* src, D3D12::RenderTask::InputMaskInput* dst, intptr_t usedTaskContainer)
	{
		ConvertShaderResourceTex(cs, &src->tex, &dst->tex, usedTaskContainer);
	}

	static void ConvertRoughnessInput(InteropCacheSet* cs, const D3D11::RenderTask::RoughnessInput* src, D3D12::RenderTask::RoughnessInput* dst, intptr_t usedTaskContainer)
	{
		dst->globalRoughness = src->globalRoughness;
		dst->maxRoughness = src->maxRoughness;
		dst->minRoughness = src->minRoughness;
		dst->roughnessMask = src->roughnessMask;
		dst->roughnessMultiplier = src->roughnessMultiplier;
		ConvertShaderResourceTex(cs, &src->tex, &dst->tex, usedTaskContainer);
	}

	static void ConvertSpecularInput(InteropCacheSet* cs, const D3D11::RenderTask::SpecularInput* src, D3D12::RenderTask::SpecularInput* dst, intptr_t usedTaskContainer)
	{
		dst->globalMetalness = src->globalMetalness;
		ConvertShaderResourceTex(cs, &src->tex, &dst->tex, usedTaskContainer);
	}

	static void ConvertEnvironmentMapInput(InteropCacheSet* cs, const D3D11::RenderTask::EnvironmentMapInput* src, D3D12::RenderTask::EnvironmentMapInput* dst, intptr_t usedTaskContainer)
	{
		dst->envMapIntensity = src->envMapIntensity;
		dst->type = (D3D12::RenderTask::EnvMapType)src->type;
		dst->worldToEnvMapMatrix = src->worldToEnvMapMatrix;
		ConvertShaderResourceTex(cs, &src->tex, &dst->tex, usedTaskContainer);
	}

	static void ConvertMotionInput(InteropCacheSet* cs, const D3D11::RenderTask::MotionInput* src, D3D12::RenderTask::MotionInput* dst, intptr_t usedTaskContainer)
	{
		dst->type = (D3D12::RenderTask::MotionType)src->type;
		dst->scale = src->scale;
		ConvertShaderResourceTex(cs, &src->tex, &dst->tex, usedTaskContainer);
	}

	static void ConvertDebugParameters(const D3D11::RenderTask::DebugParameters &src, D3D12::RenderTask::DebugParameters &dst)
	{
		static_assert(sizeof(src) == sizeof(dst));

		memcpy(&dst, &src, sizeof(src));
	}

	static void ConvertRayOffset(const D3D11::RenderTask::RayOffset& src, D3D12::RenderTask::RayOffset& dst)
	{
		static_assert(sizeof(src) == sizeof(dst));

		memcpy(&dst, &src, sizeof(src));
	}

	static void ConvertLightInfos(const D3D11::RenderTask::LightInfo *src, D3D12::RenderTask::LightInfo *dst, size_t nbInfos)
	{
		static_assert(sizeof(*src) == sizeof(*dst));

		memcpy(dst, src, sizeof(*src) * nbInfos);
	}

	static void ConvertTraceTaskCommon(InteropCacheSet* cs, const D3D11::RenderTask::TraceTaskCommon* src, D3D12::RenderTask::TraceTaskCommon* dst, intptr_t usedTaskContainer)
	{
		ConvertDepthInput(cs, &src->depth, &dst->depth, usedTaskContainer);
		ConvertNormalInput(cs, &src->normal, &dst->normal, usedTaskContainer);
		ConvertInputMaskInput(cs, &src->inputMask, &dst->inputMask, usedTaskContainer);
		ConvertRoughnessInput(cs, &src->roughness, &dst->roughness, usedTaskContainer);
		ConvertSpecularInput(cs, &src->specular, &dst->specular, usedTaskContainer);

		ConvertShaderResourceTex(cs, &src->directLighting, &dst->directLighting, usedTaskContainer);
		ConvertEnvironmentMapInput(cs, &src->envMap, &dst->envMap, usedTaskContainer);

		ConvertViewport(src->viewport, dst->viewport);

		dst->halfResolutionMode = (D3D12::RenderTask::HalfResolutionMode)src->halfResolutionMode;

		ConvertRayOffset(src->rayOffset, dst->rayOffset);

		dst->viewToClipMatrix = src->viewToClipMatrix;
		dst->clipToViewMatrix = src->clipToViewMatrix;
		dst->viewToWorldMatrix = src->viewToWorldMatrix;
		dst->worldToViewMatrix = src->worldToViewMatrix;

		dst->useInlineRT = src->useInlineRT;
	}

	static void ConvertDenoisingTaskCommon(InteropCacheSet* cs, const D3D11::RenderTask::DenoisingTaskCommon * src, D3D12::RenderTask::DenoisingTaskCommon* dst, intptr_t usedTaskContainer)
	{
		dst->mode = (D3D12::RenderTask::DenoisingTaskCommon::Mode)src->mode;
		dst->halfResolutionMode = (D3D12::RenderTask::HalfResolutionMode)src->halfResolutionMode;
		ConvertViewport(src->viewport, dst->viewport);

		ConvertDepthInput(cs, &src->depth, &dst->depth, usedTaskContainer);
		ConvertNormalInput(cs, &src->normal, &dst->normal, usedTaskContainer);
		ConvertRoughnessInput(cs, &src->roughness, &dst->roughness, usedTaskContainer);
		ConvertMotionInput(cs, &src->motion, &dst->motion, usedTaskContainer);

		dst->clipToViewMatrix = src->clipToViewMatrix;
		dst->viewToClipMatrix = src->viewToClipMatrix;
		dst->viewToClipMatrixPrev = src->viewToClipMatrixPrev;

		dst->worldToViewMatrix = src->worldToViewMatrix;
		dst->worldToViewMatrixPrev = src->worldToViewMatrixPrev;
		dst->cameraJitter = src->cameraJitter;
	}

	Status TaskContainer_impl::ScheduleRenderTask(const RenderTask::Task* renderTask)
	{
		return ScheduleRenderTasks(&renderTask, 1);
	}

	Status TaskContainer_impl::ScheduleRenderTasks(const RenderTask::Task * const *renderTasks, uint32_t nbTasks)
	{
		intptr_t usedTaskContainer = reinterpret_cast<intptr_t>(this);
		Status sts;

		for (size_t i = 0; i < nbTasks; ++i) {
			const RenderTask::Task* task(renderTasks[i]);

			switch(task->type) {
			case RenderTask::Task::Type::DirectLightInjection:
			{
				const RenderTask::DirectLightingInjectionTask* rtTask_11 = static_cast<const RenderTask::DirectLightingInjectionTask*>(task);
				D3D12::RenderTask::DirectLightingInjectionTask rtTask_12;

				ConvertViewport(rtTask_11->viewport, rtTask_12.viewport);
				rtTask_12.averageWindow = rtTask_11->averageWindow;
				rtTask_12.clipToViewMatrix = rtTask_11->clipToViewMatrix;
				rtTask_12.viewToWorldMatrix = rtTask_11->viewToWorldMatrix;
				rtTask_12.useInlineRT = rtTask_11->useInlineRT;

				ConvertDepthInput(m_interopCacheSet, &rtTask_11->depth, &rtTask_12.depth, usedTaskContainer);
				ConvertShaderResourceTex(m_interopCacheSet, &rtTask_11->directLighting, &rtTask_12.directLighting, usedTaskContainer);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::TraceSpecular:
			{
				const RenderTask::TraceSpecularTask* rtTask_11 = static_cast<const RenderTask::TraceSpecularTask*>(task);
				D3D12::RenderTask::TraceSpecularTask rtTask_12;

				ConvertTraceTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				rtTask_12.demodulateSpecular = rtTask_11->demodulateSpecular;

				ConvertUnorderedAccessTex(m_interopCacheSet, &rtTask_11->out, &rtTask_12.out, usedTaskContainer);
				ConvertUnorderedAccessTex(m_interopCacheSet, &rtTask_11->outAux, &rtTask_12.outAux, usedTaskContainer);

				ConvertDebugParameters(rtTask_11->debugParameters, rtTask_12.debugParameters);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::TraceDiffuse:
			{
				const RenderTask::TraceDiffuseTask* rtTask_11 = static_cast<const RenderTask::TraceDiffuseTask*>(task);
				D3D12::RenderTask::TraceDiffuseTask rtTask_12;

				ConvertTraceTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				rtTask_12.diffuseBRDFType = (D3D12::RenderTask::DiffuseBRDFType)rtTask_11->diffuseBRDFType;

				ConvertUnorderedAccessTex(m_interopCacheSet, &rtTask_11->out, &rtTask_12.out, usedTaskContainer);

				ConvertDebugParameters(rtTask_11->debugParameters, rtTask_12.debugParameters);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::TraceAmbientOcclusion:
			{
				const RenderTask::TraceAmbientOcclusionTask* rtTask_11 = static_cast<const RenderTask::TraceAmbientOcclusionTask*>(task);
				D3D12::RenderTask::TraceAmbientOcclusionTask rtTask_12;

				ConvertTraceTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				rtTask_12.aoRadius = rtTask_11->aoRadius;

				ConvertUnorderedAccessTex(m_interopCacheSet, &rtTask_11->out, &rtTask_12.out, usedTaskContainer);

				ConvertDebugParameters(rtTask_11->debugParameters, rtTask_12.debugParameters);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::TraceShadow:
			{
				const RenderTask::TraceShadowTask* rtTask_11 = static_cast<const RenderTask::TraceShadowTask*>(task);
				D3D12::RenderTask::TraceShadowTask rtTask_12;

				ConvertTraceTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				ConvertLightInfos(&rtTask_11->lightInfo, &rtTask_12.lightInfo, 1);

				rtTask_12.enableFirstHitAndEndSearch = rtTask_11->enableFirstHitAndEndSearch;

				ConvertUnorderedAccessTex(m_interopCacheSet, &rtTask_11->out, &rtTask_12.out, usedTaskContainer);

				ConvertDebugParameters(rtTask_11->debugParameters, rtTask_12.debugParameters);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::TraceMultiShadow:
			{
				const RenderTask::TraceMultiShadowTask* rtTask_11 = static_cast<const RenderTask::TraceMultiShadowTask*>(task);
				D3D12::RenderTask::TraceMultiShadowTask rtTask_12;

				ConvertTraceTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				static_assert(rtTask_12.kMaxLightNum == rtTask_11->kMaxLightNum);
				ConvertLightInfos(rtTask_11->lightInfos, rtTask_12.lightInfos, rtTask_12.kMaxLightNum);

				rtTask_12.numLights = rtTask_11->numLights;
				rtTask_12.enableFirstHitAndEndSearch = rtTask_11->enableFirstHitAndEndSearch;

				ConvertUnorderedAccessTex(m_interopCacheSet, &rtTask_11->out0, &rtTask_12.out0, usedTaskContainer);
				ConvertUnorderedAccessTex(m_interopCacheSet, &rtTask_11->out1, &rtTask_12.out1, usedTaskContainer);

				ConvertDebugParameters(rtTask_11->debugParameters, rtTask_12.debugParameters);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::DenoiseSpecular:
			{
				const RenderTask::DenoiseSpecularTask* rtTask_11 = static_cast<const RenderTask::DenoiseSpecularTask*>(task);
				D3D12::RenderTask::DenoiseSpecularTask rtTask_12;

				ConvertDenoisingTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				rtTask_12.context = (D3D12::DenoisingContextHandle)rtTask_11->context;

				ConvertShaderResourceTex(m_interopCacheSet, &rtTask_11->inSpecular, &rtTask_12.inSpecular, usedTaskContainer);
				ConvertCombinedAccessTex(m_interopCacheSet, &rtTask_11->inOutSpecular, &rtTask_12.inOutSpecular, usedTaskContainer);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::DenoiseDiffuse:
			{
				const RenderTask::DenoiseDiffuseTask* rtTask_11 = static_cast<const RenderTask::DenoiseDiffuseTask*>(task);
				D3D12::RenderTask::DenoiseDiffuseTask rtTask_12;

				ConvertDenoisingTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				rtTask_12.context = (D3D12::DenoisingContextHandle)rtTask_11->context;

				ConvertShaderResourceTex(m_interopCacheSet, &rtTask_11->inDiffuse, &rtTask_12.inDiffuse, usedTaskContainer);
				ConvertCombinedAccessTex(m_interopCacheSet, &rtTask_11->inOutDiffuse, &rtTask_12.inOutDiffuse, usedTaskContainer);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::DenoiseSpecularAndDiffuse:
			{
				const RenderTask::DenoiseSpecularAndDiffuseTask* rtTask_11 = static_cast<const RenderTask::DenoiseSpecularAndDiffuseTask*>(task);
				D3D12::RenderTask::DenoiseSpecularAndDiffuseTask rtTask_12;

				ConvertDenoisingTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				rtTask_12.context = (D3D12::DenoisingContextHandle)rtTask_11->context;

				ConvertShaderResourceTex(m_interopCacheSet, &rtTask_11->inSpecular, &rtTask_12.inSpecular, usedTaskContainer);
				ConvertCombinedAccessTex(m_interopCacheSet, &rtTask_11->inOutSpecular, &rtTask_12.inOutSpecular, usedTaskContainer);

				ConvertShaderResourceTex(m_interopCacheSet, &rtTask_11->inDiffuse, &rtTask_12.inDiffuse, usedTaskContainer);
				ConvertCombinedAccessTex(m_interopCacheSet, &rtTask_11->inOutDiffuse, &rtTask_12.inOutDiffuse, usedTaskContainer);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::DenoiseDiffuseOcclusion:
			{
				const RenderTask::DenoiseDiffuseOcclusionTask* rtTask_11 = static_cast<const RenderTask::DenoiseDiffuseOcclusionTask*>(task);
				D3D12::RenderTask::DenoiseDiffuseOcclusionTask rtTask_12;

				ConvertDenoisingTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				rtTask_12.context = (D3D12::DenoisingContextHandle)rtTask_11->context;
				
				rtTask_12.hitTMask = rtTask_11->hitTMask;

				ConvertShaderResourceTex(m_interopCacheSet, &rtTask_11->inHitT, &rtTask_12.inHitT, usedTaskContainer);
				ConvertCombinedAccessTex(m_interopCacheSet, &rtTask_11->inOutOcclusion, &rtTask_12.inOutOcclusion, usedTaskContainer);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::DenoiseShadow:
			{
				const RenderTask::DenoiseShadowTask* rtTask_11 = static_cast<const RenderTask::DenoiseShadowTask*>(task);
				D3D12::RenderTask::DenoiseShadowTask rtTask_12;

				ConvertDenoisingTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				rtTask_12.context = (D3D12::DenoisingContextHandle)rtTask_11->context;

				ConvertShaderResourceTex(m_interopCacheSet, &rtTask_11->inShadow, &rtTask_12.inShadow, usedTaskContainer);
				ConvertCombinedAccessTex(m_interopCacheSet, &rtTask_11->inOutShadow, &rtTask_12.inOutShadow, usedTaskContainer);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::DenoiseMultiShadow:
			{
				const RenderTask::DenoiseMultiShadowTask* rtTask_11 = static_cast<const RenderTask::DenoiseMultiShadowTask*>(task);
				D3D12::RenderTask::DenoiseMultiShadowTask rtTask_12;

				ConvertDenoisingTaskCommon(m_interopCacheSet, &rtTask_11->common, &rtTask_12.common, usedTaskContainer);

				rtTask_12.context = (D3D12::DenoisingContextHandle)rtTask_11->context;

				ConvertShaderResourceTex(m_interopCacheSet, &rtTask_11->inShadow0, &rtTask_12.inShadow0, usedTaskContainer);
				ConvertShaderResourceTex(m_interopCacheSet, &rtTask_11->inShadow1, &rtTask_12.inShadow1, usedTaskContainer);
				ConvertCombinedAccessTex(m_interopCacheSet, &rtTask_11->inOutShadow, &rtTask_12.inOutShadow, usedTaskContainer);

				sts = m_taskContainer_12->ScheduleRenderTask(&rtTask_12);
				if (sts != Status::OK) {
					Log::Fatal(L"Failed to convert a render task in D3D11 layer.");
					return Status::ERROR_INTERNAL;
				}
			}
			break;

			case RenderTask::Task::Type::Unknown:
			default:
				Log::Fatal(L"Unknown render task type detected in D3D11 layer.");
				return Status::ERROR_INTERNAL;
				break;
			}
		}

		return Status::OK;
	}
}
