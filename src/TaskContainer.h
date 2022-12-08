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
#include <OS.h>
#include <Log.h>
#include <ExecuteContext.h>

namespace KickstartRT_NativeLayer
{
	class Scene;

	// A union of RenderTaskParams would work as well, but it runs in to compile issues due to non-trivial constructor!
	static constexpr size_t cexpr_max(size_t a, size_t b) { return std::max(a, b); }
	template <typename ... TArgs>
	static constexpr size_t cexpr_max(size_t a, size_t b, TArgs ... args) { return cexpr_max(cexpr_max(a, b), args...); }

	class RenderTaskCopy {
	public:
		static constexpr size_t kSize = cexpr_max(
			sizeof(RenderTask::DirectLightingInjectionTask),
			sizeof(RenderTask::DirectLightTransferTask),
			sizeof(RenderTask::TraceSpecularTask),
			sizeof(RenderTask::TraceDiffuseTask),
			sizeof(RenderTask::TraceAmbientOcclusionTask),
			sizeof(RenderTask::TraceShadowTask),
			sizeof(RenderTask::TraceMultiShadowTask),
			sizeof(RenderTask::DenoiseSpecularTask),
			sizeof(RenderTask::DenoiseDiffuseTask),
			sizeof(RenderTask::DenoiseSpecularAndDiffuseTask),
			sizeof(RenderTask::DenoiseShadowTask),
			sizeof(RenderTask::DenoiseMultiShadowTask)
		);

		template<class T>
		static constexpr void ValidateTask() {
			static_assert(std::is_trivially_destructible_v<T>, "Must be trivially destructable.");
			static_assert(std::is_trivially_copyable_v<T>, "Must be trivially copyable.");
		}

		static constexpr void ValidateTasks() {
			ValidateTask<RenderTask::DirectLightingInjectionTask>();
			ValidateTask<RenderTask::DirectLightTransferTask>();
			ValidateTask<RenderTask::TraceSpecularTask>();
			ValidateTask<RenderTask::TraceDiffuseTask>();
			ValidateTask<RenderTask::TraceAmbientOcclusionTask>();
			ValidateTask<RenderTask::TraceShadowTask>();
			ValidateTask<RenderTask::TraceMultiShadowTask>();
			ValidateTask<RenderTask::DenoiseSpecularTask>();
			ValidateTask<RenderTask::DenoiseDiffuseTask>();
			ValidateTask<RenderTask::DenoiseSpecularAndDiffuseTask>();
			ValidateTask<RenderTask::DenoiseDiffuseOcclusionTask>();
			ValidateTask<RenderTask::DenoiseShadowTask>();
			ValidateTask<RenderTask::DenoiseMultiShadowTask>();
		}

		template<class T>
		void Store(const T& task) {
			static_assert(sizeof(T) <= kSize, "Unexpected size, did you update RenderTaskCopy::kSize?");
			memset(_data, 0, sizeof(T));
			memcpy(_data, &task, sizeof(T));
		}

		template<class T>
		RenderTaskCopy(const T& task) {
			Store<T>(task);
		}

		template<class T>
		const T& Get() const {
			return reinterpret_cast<const T&>(_data);
		}

		const RenderTask::Task& Get() const {
			return reinterpret_cast<const RenderTask::Task&>(_data);
		}

		RenderTask::Task::Type GetType() const {
			return reinterpret_cast<const RenderTask::Task&>(_data).type;
		}

	private:
		char _data[kSize];
	};

	class RenderTasks {
		friend class Scene;
		std::deque<RenderTaskCopy>	m_renderTasks;
		bool						m_hasDenoisingTask = false;

	public:
		bool HasDenoisingTask() const { return m_hasDenoisingTask; };

		Status ScheduleRenderTasks(const RenderTask::Task* const* tasks, uint32_t nbTasks) {
			for (uint32_t i = 0; i < nbTasks; ++i) {
				const auto& task(*(tasks[i]));

				if (task.type == RenderTask::Task::Type::DirectLightInjection) {
					auto& input(*static_cast<const RenderTask::DirectLightingInjectionTask*>(&task));
					m_renderTasks.emplace_back(input);
				}
				else if (task.type == RenderTask::Task::Type::DirectLightTransfer) {
					auto& input(*static_cast<const RenderTask::DirectLightTransferTask*>(&task));
					m_renderTasks.emplace_back(input);
				}
				else if (task.type == RenderTask::Task::Type::TraceSpecular) {
					auto& input(*static_cast<const RenderTask::TraceSpecularTask*>(&task));
					m_renderTasks.emplace_back(input);
				}
				else if (task.type == RenderTask::Task::Type::TraceDiffuse) {
					auto& input(*static_cast<const RenderTask::TraceDiffuseTask*>(&task));
					m_renderTasks.emplace_back(input);
				}
				else if (task.type == RenderTask::Task::Type::TraceAmbientOcclusion) {
					auto& input(*static_cast<const RenderTask::TraceAmbientOcclusionTask*>(&task));
					m_renderTasks.emplace_back(input);
				}
				else if (task.type == RenderTask::Task::Type::TraceShadow) {
					auto& input(*static_cast<const RenderTask::TraceShadowTask*>(&task));
					m_renderTasks.emplace_back(input);
				}
				else if (task.type == RenderTask::Task::Type::TraceMultiShadow) {
					auto& input(*static_cast<const RenderTask::TraceMultiShadowTask*>(&task));
					m_renderTasks.emplace_back(input);
				}
				else if (task.type == RenderTask::Task::Type::DenoiseSpecular) {
					auto& input(*static_cast<const RenderTask::DenoiseSpecularTask*>(&task));
					m_renderTasks.emplace_back(input);
					m_hasDenoisingTask = true;
				}
				else if (task.type == RenderTask::Task::Type::DenoiseDiffuse) {
					auto& input(*static_cast<const RenderTask::DenoiseDiffuseTask*>(&task));
					m_renderTasks.emplace_back(input);
					m_hasDenoisingTask = true;
				}
				else if (task.type == RenderTask::Task::Type::DenoiseSpecularAndDiffuse) {
					auto& input(*static_cast<const RenderTask::DenoiseSpecularAndDiffuseTask*>(&task));
					m_renderTasks.emplace_back(input);
					m_hasDenoisingTask = true;
				}
				else if (task.type == RenderTask::Task::Type::DenoiseDiffuseOcclusion) {
					auto& input(*static_cast<const RenderTask::DenoiseDiffuseOcclusionTask*>(&task));
					m_renderTasks.emplace_back(input);
					m_hasDenoisingTask = true;
				}
				else if (task.type == RenderTask::Task::Type::DenoiseShadow) {
					auto& input(*static_cast<const RenderTask::DenoiseShadowTask*>(&task));
					m_renderTasks.emplace_back(input);
					m_hasDenoisingTask = true;
				}
				else if (task.type == RenderTask::Task::Type::DenoiseMultiShadow) {
					auto& input(*static_cast<const RenderTask::DenoiseMultiShadowTask*>(&task));
					m_renderTasks.emplace_back(input);
					m_hasDenoisingTask = true;
				}
				else {
					Log::Fatal(L"Unknown render task detected.");
					return Status::ERROR_INTERNAL;
				}
			}

			return Status::OK;
		}
	};

	class BVHTasks {
		friend class Scene;

		bool						m_hasUpdate = false;

		std::deque<GeometryHandle>	m_registeredGeometries;
		struct GeomInfo {
			GeometryHandle m_gh = GeometryHandle::Null;
			BVHTask::GeometryInput	m_input = {};
		};
		std::deque<GeomInfo>		m_updatedGeometries;

		std::deque<InstanceHandle>	m_registeredInstances;
		struct InsInfo {
			InstanceHandle	m_ih = InstanceHandle::Null;
			BVHTask::InstanceInput	m_input = {};
		};
		std::deque<InsInfo>			m_updatedInstances;

		uint32_t					m_maxBLASbuildCount = 0u;
		bool						m_buildTLAS = false;

	public:
		Status RegisterGeometry(GeometryHandle gHandle, const BVHTask::GeometryInput* input);
		Status UpdateGeometry(GeometryHandle gHandle, const BVHTask::GeometryInput* newInput);
		Status RegisterInstance(InstanceHandle iHandle, const BVHTask::InstanceInput* input);
		Status UpdateInstance(InstanceHandle iHandle, const BVHTask::InstanceInput* newInput);
		Status SetBVHBuildTask(const BVHTask::BVHBuildTask* task);
		bool HasUpdate() const { return m_hasUpdate; };
	};

	class TaskContainer_impl : public TaskContainer {
	public:
		std::mutex					m_mutex;

		std::unique_ptr<BVHTasks>			m_bvhTask;
		std::unique_ptr<RenderTasks>		m_renderTask;

	public:
		TaskContainer_impl();
		virtual ~TaskContainer_impl();

		Status ScheduleRenderTask(const RenderTask::Task* tasks) override;
		Status ScheduleRenderTasks(const RenderTask::Task* const* tasks, uint32_t nbTasks) override;
		Status ScheduleBVHTask(const BVHTask::Task* tasks) override;
		Status ScheduleBVHTasks(const BVHTask::Task* const* tasks, uint32_t nbTasks) override;
	};
};
