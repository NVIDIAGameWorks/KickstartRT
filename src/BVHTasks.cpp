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
#include <Scene.h>
#include <Utils.h>
#include <Log.h>
#include <Geometry.h>
#include <RenderPass_DirectLightingCacheAllocation.h>

#include <cstring>

namespace KickstartRT_NativeLayer
{
	using namespace KickstartRT_NativeLayer::BVHTask;

	Status BVHTasks::RegisterGeometry(GeometryHandle gHandle, const GeometryInput* input)
	{
		auto* gh = BVHTask::Geometry::ToPtr(gHandle);

		if (gHandle == GeometryHandle::Null) {
			Log::Fatal(L"Geometry handle was null.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (input == nullptr) {
			Log::Fatal(L"GeometryInput was null.");
			return Status::ERROR_INVALID_PARAM;
		}

		if (gh->m_registerStatus != BVHTask::RegisterStatus::NotRegistered) {
			Log::Fatal(L"Geometry handle was tried to be registerd multiple times.");
			return Status::ERROR_INVALID_PARAM;
		}

		auto sts = RenderPass_DirectLightingCacheAllocation::CheckInputs(*input);
		if (sts != Status::OK) {
			Log::Fatal(L"Invaid geometry input detected.");
			return Status::ERROR_INVALID_PARAM;
		}

		// memberwise copy, except debug string
		gh->m_input = *input;
		if (input->name != nullptr) {
			gh->m_name = input->name;
			gh->m_input.name = nullptr;
		}

		gh->m_registerStatus = BVHTask::RegisterStatus::Registering;
		m_registeredGeometries.push_back(gHandle);
		m_hasUpdate = true;

		return Status::OK;
	}

	Status BVHTasks::UpdateGeometry(GeometryHandle gHandle, const GeometryInput* newInput)
	{
		auto* gh = Geometry::ToPtr(gHandle);

		if (gHandle == GeometryHandle::Null) {
			Log::Fatal(L"Geometry handle was null.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (newInput == nullptr) {
			Log::Fatal(L"New GeometryInput was null.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (gh->m_registerStatus != BVHTask::RegisterStatus::Registering &&
			gh->m_registerStatus != BVHTask::RegisterStatus::Registered) {
			Log::Fatal(L"Geometry handle was tried to be updated without registering it.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (gh->m_input.components.size() != (size_t)newInput->components.size()) {
			Log::Fatal(L"The number of geometry components are different.");
			return Status::ERROR_INVALID_PARAM;
		}

		auto sts = RenderPass_DirectLightingCacheAllocation::CheckUpdateInputs(gh->m_input, *newInput);
		if (sts != Status::OK) {
			Log::Fatal(L"Invaid geometry input detected.");
			return Status::ERROR_INVALID_PARAM;
		}

		m_updatedGeometries.push_back({ gHandle });
		{
			auto& upd(m_updatedGeometries.back());
			upd.m_input = *newInput;
			upd.m_input.name = nullptr;
		}

		m_hasUpdate = true;

		return Status::OK;
	}

	Status BVHTasks::RegisterInstance(InstanceHandle iHandle, const InstanceInput* input)
	{
		auto* ih = Instance::ToPtr(iHandle);

		if (iHandle == InstanceHandle::Null) {
			Log::Fatal(L"Instance handle was null.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (input == nullptr) {
			Log::Fatal(L"InstanceInput was null.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (ih->m_registerStatus != BVHTask::RegisterStatus::NotRegistered) {
			Log::Fatal(L"Instance handle was tried to be registered multiple times.");
			return Status::ERROR_INVALID_PARAM;
		}

		// memberwise copy except debug name.
		ih->m_input = *input;
		if (input->name != nullptr) {
			ih->m_name = input->name;
			ih->m_input.name = nullptr;
		}

		ih->m_registerStatus = BVHTask::RegisterStatus::Registering;
		m_registeredInstances.push_back(iHandle);
		m_hasUpdate = true;

		return Status::OK;
	}

	Status BVHTasks::UpdateInstance(InstanceHandle iHandle, const InstanceInput* newInput)
	{
		auto* ih = Instance::ToPtr(iHandle);

		if (iHandle == InstanceHandle::Null) {
			Log::Fatal(L"Instance handle was null.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (newInput == nullptr) {
			Log::Fatal(L"New InstanceInput was null.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (ih->m_registerStatus != BVHTask::RegisterStatus::Registering &&
			ih->m_registerStatus != BVHTask::RegisterStatus::Registered) {
			Log::Fatal(L"Instance handle was tried to be updated without registering.");
			return Status::ERROR_INVALID_PARAM;
		}

		// update transform from input
		m_updatedInstances.push_back({ iHandle, *newInput });
		m_hasUpdate = true;

		return Status::OK;
	}

	Status BVHTasks::SetBVHBuildTask(const BVHTask::BVHBuildTask *task)
	{
		m_maxBLASbuildCount = task->maxBlasBuildCount;
		m_buildTLAS = task->buildTLAS;

		m_hasUpdate |= (task->maxBlasBuildCount > 0 || m_buildTLAS);

		return Status::OK;
	};
};
