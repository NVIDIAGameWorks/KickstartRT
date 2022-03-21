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
#include <TaskTracker.h>
#include <Log.h>
#include <PersistentWorkingSet.h>
#include <TaskWorkingSet.h>

#include <cinttypes>
#include <algorithm>

namespace KickstartRT_NativeLayer
{
	TaskTracker::TaskTracker()
	{
	};

	TaskTracker::~TaskTracker()
	{
	};

	Status TaskTracker::Init(PersistentWorkingSet* pws, const ExecuteContext_InitSettings* initSettings)
	{
		std::scoped_lock mtx(m_mutex);

		for (size_t i = 0; i < initSettings->supportedWorkingsets; ++i) {
			std::unique_ptr<TaskWorkingSet> tws = std::make_unique<TaskWorkingSet>(pws);
			auto sts = tws->Init(initSettings);
			if (sts != Status::OK) {
				Log::Fatal(L"Failed to init task working set.");
				m_taskWorkingSets.clear();
				return sts;
			}
			m_taskWorkingSets.emplace_back(std::move(tws));
		}

		// all working sets are now idling.
		m_taskIndicesForWorkingSets.resize(m_taskWorkingSets.size(), 0ull);

		return Status::OK;
	}

	uint64_t TaskTracker::CurrentTaskIndex()
	{
		std::scoped_lock mtx(m_mutex);

		return m_currentTaskIndex;
	}

	uint64_t TaskTracker::FinishedTaskIndex()
	{
		std::scoped_lock mtx(m_mutex);

		return m_finishedTaskIndex;
	}

	Status TaskTracker::UpdateFinishedTaskIndex(uint64_t finishedTaskIndex)
	{
		std::scoped_lock mtx(m_mutex);

		if (finishedTaskIndex == 0)
			return Status::OK;

		auto itr = std::find(m_taskIndicesForWorkingSets.begin(), m_taskIndicesForWorkingSets.end(), finishedTaskIndex);
		if (itr == m_taskIndicesForWorkingSets.end()) {
			Log::Fatal(L"Invalid finished task index (GPUTaskHandle) detected. :%" PRIu64, finishedTaskIndex);
			return Status::ERROR_INVALID_PARAM;
		}
		*itr = 0ull;

		// update finished task index.
		uint64_t minInFlightIdx = 0xFFFF'FFFF'FFFF'FFFFull;
		std::for_each(m_taskIndicesForWorkingSets.begin(), m_taskIndicesForWorkingSets.end(),
			[&minInFlightIdx](auto&& i) { if (i != 0ull && i < minInFlightIdx) minInFlightIdx= i; });

		if (minInFlightIdx == 0xFFFF'FFFF'FFFF'FFFFull) {
			// If thre is no in-flight index, currentTask index has been finished.
			m_finishedTaskIndex = m_currentTaskIndex;
		}
		else {
			// If threre are in-flight indices, finished inidex should be (minimum of in-flight indices) - 1
			m_finishedTaskIndex = minInFlightIdx - 1;
		}

		return Status::OK;
	}

	bool TaskTracker::TaskWorkingSetIsAvailable()
	{
		std::scoped_lock mtx(m_mutex);

		auto itr = std::find(m_taskIndicesForWorkingSets.begin(), m_taskIndicesForWorkingSets.end(), 0ull);
		if (itr == m_taskIndicesForWorkingSets.end())
			return false;

		return true;
	}

	Status TaskTracker::AllocateTaskWorkingSet(TaskWorkingSet **ret_tws, uint64_t *ret_taskIndex)
	{
		std::scoped_lock mtx(m_mutex);

		*ret_tws = nullptr;
		*ret_taskIndex = 0xFFFF'FFFF'FFFF'FFFF;

		auto itr = std::find(m_taskIndicesForWorkingSets.begin(), m_taskIndicesForWorkingSets.end(), 0ull);
		if (itr == m_taskIndicesForWorkingSets.end()) {
			Log::Fatal(L"Failed to allocate TaskWorkingSet. All tasks are in-flight.");
			return Status::ERROR_INTERNAL;
		}
		size_t idx = itr - m_taskIndicesForWorkingSets.begin();

		// Advance the current task index.
		++m_currentTaskIndex;

		m_taskIndicesForWorkingSets[idx] = m_currentTaskIndex;

		*ret_tws = m_taskWorkingSets[idx].get();
		*ret_taskIndex = m_currentTaskIndex;

		return Status::OK;
	}
};

