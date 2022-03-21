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

namespace KickstartRT_ExportLayer
{
	class InteropCacheSet;

	struct TaskContainer_impl : public TaskContainer {
		friend class ExecuteContext_impl;

	protected:
		InteropCacheSet*		m_interopCacheSet;
		D3D12::TaskContainer*	m_taskContainer_12;

	public:
		TaskContainer_impl(InteropCacheSet *cs, D3D12::TaskContainer* container_12) :
			m_interopCacheSet(cs),
			m_taskContainer_12(container_12)
		{};
		virtual ~TaskContainer_impl();

		Status ScheduleRenderTask(const RenderTask::Task* renderTask) override;
		Status ScheduleRenderTasks(const RenderTask::Task* const* renderTaskPtrArr, uint32_t nbTasks) override;
		Status ScheduleBVHTask(const BVHTask::Task* bvhTask) override;
		Status ScheduleBVHTasks(const BVHTask::Task* const* bvhTaskPtrArr, uint32_t nbTasks) override;
	};
};

