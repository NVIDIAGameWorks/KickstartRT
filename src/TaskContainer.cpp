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
#include <Utils.h>
#include <TaskWorkingSet.h>
#include <Scene.h>
#include <TaskContainer.h>

namespace KickstartRT_NativeLayer
{
	static const char* GetRenderTaskName(RenderTask::Task::Type type) {
		switch (type) {
		case RenderTask::Task::Type::DirectLightInjection:		return "DirectLightInjection";
		case RenderTask::Task::Type::TraceSpecular:				return "TraceSpecular";
		case RenderTask::Task::Type::TraceDiffuse:				return "TraceDiffuse";
		case RenderTask::Task::Type::TraceAmbientOcclusion:		return "TraceAmbientOcclusion";
		case RenderTask::Task::Type::TraceShadow:				return "TraceShadow";
		case RenderTask::Task::Type::TraceMultiShadow:			return "TraceMultiShadow";
		case RenderTask::Task::Type::DenoiseSpecular:			return "DenoiseSpecular";
		case RenderTask::Task::Type::DenoiseDiffuse:			return "DenoiseDiffuse";
		case RenderTask::Task::Type::DenoiseSpecularAndDiffuse:	return "DenoiseSpecularAndDiffuse";
		case RenderTask::Task::Type::DenoiseDiffuseOcclusion:	return "DenoiseDiffuseOcclusion";
		case RenderTask::Task::Type::DenoiseShadow:				return "DenoiseShadow";
		case RenderTask::Task::Type::DenoiseMultiShadow:		return "DenoiseMultiShadow";
		default:
			return "Unknown";
		}
	}

	TaskContainer::TaskContainer()
	{
	};

	TaskContainer::~TaskContainer()
	{
	};

	TaskContainer_impl::TaskContainer_impl()
	{
		m_bvhTask = std::make_unique<BVHTasks>();
		m_renderTask = std::make_unique<RenderTasks>();
	//	m_sceneDenoising = std::make_unique<SceneDenoising>();
	};

	TaskContainer_impl::~TaskContainer_impl()
	{
		std::scoped_lock mtx(m_mutex);

		m_bvhTask.reset();
		m_renderTask.reset();
	//	m_sceneDenoising.reset();
	};

	Status TaskContainer_impl::ScheduleRenderTask(const RenderTask::Task* task)
	{
		std::scoped_lock mtx(m_mutex);

		return m_renderTask->ScheduleRenderTasks(&task, 1);
	}

	Status TaskContainer_impl::ScheduleRenderTasks(const RenderTask::Task* const *tasks, uint32_t nbTasks)
	{
		std::scoped_lock mtx(m_mutex);

		return m_renderTask->ScheduleRenderTasks(tasks, nbTasks);
	}

	Status TaskContainer_impl::ScheduleBVHTask(const BVHTask::Task* task)
	{
		return ScheduleBVHTasks(&task, 1);
	}

	Status TaskContainer_impl::ScheduleBVHTasks(const BVHTask::Task* const* tasks, uint32_t nbTasks)
	{
		std::scoped_lock mtx(m_mutex);

		Status sts = Status::OK;

		for (size_t i = 0; i < nbTasks; ++i) {
			const auto* t(tasks[i]);

			if (t->type == BVHTask::Task::Type::Geometry) {
				const BVHTask::GeometryTask* gt = static_cast<const BVHTask::GeometryTask*>(t);
				switch (gt->taskOperation) {
				case BVHTask::TaskOperation::Register:
				{
					sts = m_bvhTask->RegisterGeometry(gt->handle, &gt->input);
				}
				break;
				case BVHTask::TaskOperation::Update:
				{
					sts = m_bvhTask->UpdateGeometry(gt->handle, &gt->input);
				}
				break;
				default:
					Log::Fatal(L"Unknown task kingd detected.");
					sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
				}
			}
			else if (t->type == BVHTask::Task::Type::Instance) {
				const BVHTask::InstanceTask* it = static_cast<const BVHTask::InstanceTask*>(t);
				switch (it->taskOperation) {
				case BVHTask::TaskOperation::Register:
				{
					sts = m_bvhTask->RegisterInstance(it->handle, &it->input);
				}
				break;
				case BVHTask::TaskOperation::Update:
				{
					sts = m_bvhTask->UpdateInstance(it->handle, &it->input);
				}
				break;
				default:
					Log::Fatal(L"Unknown task kingd detected.");
					sts = Status::ERROR_INVALID_GEOMETRY_INPUTS;
				}
			}
			else if (t->type == BVHTask::Task::Type::BVHBuild) {
				const BVHTask::BVHBuildTask* bt = static_cast<const BVHTask::BVHBuildTask*>(t);
				sts = m_bvhTask->SetBVHBuildTask(bt);
			}
			else {
				Log::Fatal(L"Unknown task type detected.");
				sts = Status::ERROR_INVALID_PARAM;
			}
			if (sts != Status::OK)
				break;
		}
		if (sts != Status::OK) {
			Log::Fatal(L"Failed to ScheduleBVHTask.");
		}

		return sts;
	}
};