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

#include <assert.h>
#include <memory>
#include <mutex>
#include <vector>

namespace KickstartRT_NativeLayer
{
	class PersistentWorkingSet;
	class TaskWorkingSet;

	// task tracker is separated from Scene or PersistentWorkingSet, as it need to be updated from user side anytime (e.g. during task building.)
	// otherwise it can be dead locked.
	struct TaskTracker {
	protected:
		std::mutex	m_mutex;
		uint64_t	m_currentTaskIndex = 0ull;
		uint64_t	m_finishedTaskIndex = 0ull;

		std::vector<std::unique_ptr<TaskWorkingSet>>		m_taskWorkingSets;
		std::vector<uint64_t>								m_taskIndicesForWorkingSets;

	public:
		TaskTracker();
		~TaskTracker();

		Status Init(PersistentWorkingSet* pws, const ExecuteContext_InitSettings* initSettings);

		uint64_t CurrentTaskIndex();
		uint64_t FinishedTaskIndex();

		bool TaskWorkingSetIsAvailable();
		Status UpdateFinishedTaskIndex(uint64_t finishedTaskIndex);
		Status AllocateTaskWorkingSet(TaskWorkingSet** ret_tws, uint64_t *ret_taskIndex);
	};
};

