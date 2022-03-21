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
	namespace Version {
		KickstartRT::Version GetLibraryVersion()
		{
			return KickstartRT::Version::Version();
		}
	};

	Status ExecuteContext::Init(const ExecuteContext_InitSettings* settings, ExecuteContext** arg_exc, const KickstartRT::Version headerVersion)
	{
		std::scoped_lock mtx(g_APIInterfaceMutex);

		const auto libVersion = Version::GetLibraryVersion();
		if (headerVersion.Major != libVersion.Major || headerVersion.Minor > libVersion.Minor) {
			Log::Fatal(L"KickstartRT SDK header version and library version was different. (LIB):%d.%d.%d, (Header):%d.%d.%d",
				libVersion.Major, libVersion.Minor, libVersion.Patch, headerVersion.Major, headerVersion.Minor, headerVersion.Patch);
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}
		else if (headerVersion.Minor != libVersion.Minor) {
			// headerVersion.Minor < libVersion.Minor
			Log::Warning(L"KickstartRT SDK lib version was newer than header version. (LIB):%d.%d.%d, (Header):%d.%d.%d",
				libVersion.Major, libVersion.Minor, libVersion.Patch, headerVersion.Major, headerVersion.Minor, headerVersion.Patch);
		}
		else if (headerVersion.Patch != libVersion.Patch) {
			Log::Info(L"KickstartRT SDK different Patch version was detected. (LIB):%d.%d.%d, (Header):%d.%d.%d",
				libVersion.Major, libVersion.Minor, libVersion.Patch, headerVersion.Major, headerVersion.Minor, headerVersion.Patch);
		}

		*arg_exc = nullptr;

		std::unique_ptr<ExecuteContext_impl> exc = std::make_unique<ExecuteContext_impl>();

		auto sts = exc->Init(settings);
		if (sts != Status::OK) {
			Log::Fatal(L"Failed to init execute context.");
			return sts;
		}

		*arg_exc = exc.release();
		return Status::OK;
	}

	Status ExecuteContext::Destruct(ExecuteContext* exc)
	{
		std::scoped_lock mtx(g_APIInterfaceMutex);

		//Log::Info(L"ExecuteContext::Destruct() called");

		ExecuteContext_impl* exc_impl = static_cast<ExecuteContext_impl*>(exc);

		delete exc_impl;

		return Status::OK;
	};

	ExecuteContext::ExecuteContext()
	{
	}

	ExecuteContext::~ExecuteContext()
	{
	}

	ExecuteContext_impl::ExecuteContext_impl()
	{
	}

	ExecuteContext_impl::~ExecuteContext_impl()
	{
		m_persistentWorkingSet.reset();
	}

	Status ExecuteContext_impl::Init(const ExecuteContext_InitSettings* settings)
	{
		if (settings == nullptr) {
			Log::Error(L"Invalid InitSettings detected");
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}

		if (settings->D3D11Device == nullptr) {
			Log::Error(L"Invalid D3D11Device detected");
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}

		if (settings->DXGIAdapter == nullptr) {
			Log::Error(L"Invalid DXGIAdapter detected");
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}

		if (settings->supportedWorkingSet >= 10) {
			Log::Error(L"Supported working set must be less than 10");
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}

		m_numberOfWorkingSets = settings->supportedWorkingSet;

		// initialize PWS
		{
			std::unique_ptr<PersistentWorkingSet> pws = std::make_unique<PersistentWorkingSet>();
			auto sts = pws->Init(settings);
			if (sts != Status::OK) {
				Log::Fatal(L"Failed to init persistent working set.");
				return sts;
			}

			m_persistentWorkingSet = std::move(pws);
		}

		return Status::OK;
	}

	Status ExecuteContext_impl::InvokeGPUTask(TaskContainer* arg_container, const BuildGPUTaskInput *input)
	{
		std::scoped_lock mtx(m_mutex);

		TaskContainer_impl* container = static_cast<TaskContainer_impl *>(arg_container);

		if (input->waitFence == nullptr) {
			Log::Fatal(L"Wait fence cannot be null when calling InvokeGPUTask()");
			return Status::ERROR_INVALID_PARAM;
		}
		if (input->signalFence == nullptr) {
			Log::Fatal(L"Signal fence cannot be null when calling InvokeGPUTask()");
			return Status::ERROR_INVALID_PARAM;
		}

		auto* pws(m_persistentWorkingSet.get());
		auto* cs(pws->m_interopCacheSet.get());
		ID3D12Fence* waitFence_12 = nullptr;
		ID3D12Fence* signalFence_12 = nullptr;

		if (cs->ConvertFence(input->waitFence, &waitFence_12, reinterpret_cast<intptr_t>(container)) != Status::OK) {
			Log::Fatal(L"Failed to convert D3D11 wait fence to 12.");
			return Status::ERROR_FAILED_TO_INIT_FENCE;
		}
		if (cs->ConvertFence(input->signalFence, &signalFence_12, reinterpret_cast<intptr_t>(container)) != Status::OK) {
			Log::Fatal(L"Failed to convert D3D11 signal fence to 12.");
			return Status::ERROR_FAILED_TO_INIT_FENCE;
		}

		// Secure at least one empty TaskWorkingSet in D3D12 layer and a command allocator/list.
		if (pws->m_fence.WaitForIdleTaskWorkingSet(pws->m_SDK_12, m_numberOfWorkingSets) != Status::OK) {
			Log::Fatal(L"Failed to wait for GPU task completion..");
			return Status::ERROR_INTERNAL;
		}

		// Set last used index in the caches with the current task index.
		// This checks all interpol cache entries if m_lastUsedTaskContainerPtr is this container.
		// If so, set m_lastUsedFenceValue with this taskIndex and clear m_lastUsedTaskContainerPtr
		// This cache entry will be cleared when the corresponded task is finished on GPU.
		if (cs->SetLastUsedFenceValue(m_taskIndex, reinterpret_cast<intptr_t>(container)) != Status::OK) {
			Log::Fatal(L"Failed to set last used task index..");
			return Status::ERROR_INTERNAL;
		}

		{
			auto* sdk12(pws->m_SDK_12);

			ID3D12CommandAllocator* ca = nullptr;
			ID3D12GraphicsCommandList5* cl = nullptr;
			size_t				caIdx = (size_t)-1;
			pws->m_fence.GetIdleCommandList(&caIdx, &ca, &cl);
			if (caIdx == -1) {
				Log::Fatal(L"Failed to allocate a D3D12 command list.");
				return Status::ERROR_INTERNAL;
			}
			if (FAILED(ca->Reset())) {
				Log::Fatal(L"Failed to reset D3D12 command allocator.");
				return Status::ERROR_INTERNAL;
			}
			if (FAILED(cl->Reset(ca, nullptr))) {
				Log::Fatal(L"Failed to reset D3D12 command list.");
				return Status::ERROR_INTERNAL;
			}

			// set the native layer's task inputs.
			{
				D3D12::GPUTaskHandle hTask;
				D3D12::BuildGPUTaskInput taskInput_12 = {};

				taskInput_12.commandList = cl;
				taskInput_12.geometryTaskFirst = input->geometryTaskFirst;
				auto sts = sdk12->BuildGPUTask(&hTask, container->m_taskContainer_12, &taskInput_12);
				container->m_taskContainer_12 = nullptr; // clear the d3d12 container ptr.
				delete container;
				container = nullptr;

				// Everything has been recorded in to command list, now close it before calling ExecuteCommandLists.
				cl->Close();

				if (sts != Status::OK) {
					Log::Fatal(L"Failed to build task");
					return sts;
				}

				auto* queue_12(pws->m_queue_12.Get());

				// user wait fence to interop d3D11 layer
				queue_12->Wait(waitFence_12, input->waitFenceValue);

				ID3D12CommandList* clArr[1] = { cl };
				queue_12->ExecuteCommandLists(1, clArr);

				// set an internal signal fence to find out when the SDK task has been finished on GPU.
				queue_12->Signal(pws->m_fence.m_taskFence_12.Get(), m_taskIndex);

				// user signal fence to interop d3D11 layer
				queue_12->Signal(signalFence_12, input->signalFenceValue);

				// Record in-flight task. Update last submitted fence value.
				pws->m_fence.RecordInflightTask(caIdx, m_taskIndex, hTask);
			}

			// Update completed fence value by checking the fence object.
			pws->m_fence.UpdateCompltedValue();

			// Release interop device objects with updated fence value.
			cs->ReleaseCacheResources(pws->m_resourceLogger, pws->m_fence.m_lastSubmittedFenceValue_12, pws->m_fence.m_completedFenceValue_12);
		}

		m_taskIndex++;

		return Status::OK;
	}

	Status ExecuteContext_impl::ReleaseDeviceResourcesImmediately()
	{
		std::scoped_lock mtx(m_mutex);

		// Wait for all in-flight GPU tasks' completion.
		{
			auto* pws(m_persistentWorkingSet.get());
			auto* cs(pws->m_interopCacheSet.get());

			if (pws->m_fence.WaitForIdleTaskWorkingSet(pws->m_SDK_12, 1) != Status::OK) {
				Log::Fatal(L"Failed to wait for GPU task completion..");
				return Status::ERROR_INTERNAL;
			}

			// Update completed fence value by checking the fence object.
			pws->m_fence.UpdateCompltedValue();

			// Release interop device objects with updated fence value.
			cs->ReleaseCacheResources(pws->m_resourceLogger, pws->m_fence.m_lastSubmittedFenceValue_12, pws->m_fence.m_completedFenceValue_12);
		}

		auto sts = m_persistentWorkingSet->m_SDK_12->ReleaseDeviceResourcesImmediately();
		if (sts != Status::OK) {
			Log::Fatal(L"Failed to ReleaseDeviceResourcesImmediately() in D3D12 layer.");
			return Status::ERROR_INTERNAL;
		}

		return Status::OK;
	}

	DenoisingContextHandle ExecuteContext_impl::CreateDenoisingContextHandle(const DenoisingContextInput* input)
	{
		auto ConvertDenoisingContextInput = [](const DenoisingContextInput* context_11, D3D12::DenoisingContextInput* context_12) {
			auto& src(*context_11);
			auto& dst(*context_12);

			static_assert(sizeof(dst.denoisingMethod) == sizeof(src.denoisingMethod));
			memcpy(&dst.denoisingMethod, &src.denoisingMethod, sizeof(dst.denoisingMethod));
			dst.maxHeight = src.maxHeight;
			dst.maxWidth = src.maxWidth;
			static_assert(sizeof(dst.signalType) == sizeof(src.signalType));
			memcpy(&dst.signalType, &src.signalType, sizeof(dst.signalType));
		};

		D3D12::DenoisingContextInput input_12;
		ConvertDenoisingContextInput(input, &input_12);

		return (DenoisingContextHandle)m_persistentWorkingSet->m_SDK_12->CreateDenoisingContextHandle(&input_12);
	}

	Status ExecuteContext_impl::DestroyDenoisingContextHandle(DenoisingContextHandle handle)
	{
		return m_persistentWorkingSet->m_SDK_12->DestroyDenoisingContextHandle((D3D12::DenoisingContextHandle)handle);
	}

	Status ExecuteContext_impl::DestroyAllDenoisingContextHandles()
	{
		return m_persistentWorkingSet->m_SDK_12->DestroyAllDenoisingContextHandles();
	}

	GeometryHandle ExecuteContext_impl::CreateGeometryHandle()
	{
		return (GeometryHandle)m_persistentWorkingSet->m_SDK_12->CreateGeometryHandle();
	}

	Status ExecuteContext_impl::CreateGeometryHandles(GeometryHandle *handles, uint32_t nbHandles)
	{
		if (handles == nullptr) {
			Log::Fatal(L"Null pointer detected when creating geometry handles.");
			return Status::ERROR_INVALID_PARAM;
		}
		return m_persistentWorkingSet->m_SDK_12->CreateGeometryHandles(reinterpret_cast<D3D12::GeometryHandle*>(handles), nbHandles);
	}

	Status ExecuteContext_impl::DestroyGeometryHandle(GeometryHandle handle)
	{
		return m_persistentWorkingSet->m_SDK_12->DestroyGeometryHandle((D3D12::GeometryHandle)handle);
	}

	Status ExecuteContext_impl::DestroyGeometryHandles(const GeometryHandle *handles, uint32_t nbHandles)
	{
		return m_persistentWorkingSet->m_SDK_12->DestroyGeometryHandles(reinterpret_cast<const D3D12::GeometryHandle*>(handles), nbHandles);
	}

	Status ExecuteContext_impl::DestroyAllGeometryHandles()
	{
		return m_persistentWorkingSet->m_SDK_12->DestroyAllGeometryHandles();
	}

	InstanceHandle ExecuteContext_impl::CreateInstanceHandle()
	{
		return (InstanceHandle)m_persistentWorkingSet->m_SDK_12->CreateInstanceHandle();
	}

	Status ExecuteContext_impl::CreateInstanceHandles(InstanceHandle* handles, uint32_t nbHandles)
	{
		if (handles == nullptr) {
			Log::Fatal(L"Null pointer detected when creating instance handles.");
			return Status::ERROR_INVALID_PARAM;
		}
		return m_persistentWorkingSet->m_SDK_12->CreateInstanceHandles(reinterpret_cast<D3D12::InstanceHandle*>(handles), nbHandles);
	}

	Status ExecuteContext_impl::DestroyInstanceHandle(InstanceHandle handle)
	{
		return m_persistentWorkingSet->m_SDK_12->DestroyInstanceHandle((D3D12::InstanceHandle)handle);
	}

	Status ExecuteContext_impl::DestroyInstanceHandles(const InstanceHandle* handles, uint32_t nbHandles)
	{
		return m_persistentWorkingSet->m_SDK_12->DestroyInstanceHandles(reinterpret_cast<const D3D12::InstanceHandle*>(handles), nbHandles);
	}

	Status ExecuteContext_impl::DestroyAllInstanceHandles()
	{
		return m_persistentWorkingSet->m_SDK_12->DestroyAllInstanceHandles();
	}

	TaskContainer* ExecuteContext_impl::CreateTaskContainer()
	{
		D3D12::TaskContainer* container_12 = nullptr;

		container_12 = m_persistentWorkingSet->m_SDK_12->CreateTaskContainer();
		if (container_12 == nullptr) {
			Log::Fatal(L"Failed to create taskcontainer in D3D12 layer.");
			return nullptr;
		}

		return new TaskContainer_impl(m_persistentWorkingSet->m_interopCacheSet.get(), container_12);
	}

	Status ExecuteContext_impl::GetLoadedShaderList(uint32_t* loadedListBuffer, size_t bufferSize, size_t* retListSize)
	{
		return m_persistentWorkingSet->m_SDK_12->GetLoadedShaderList(loadedListBuffer, bufferSize, retListSize);
	}

	Status ExecuteContext_impl::GetCurrentResourceAllocations(ResourceAllocations* retStatus)
	{
		return m_persistentWorkingSet->m_SDK_12->GetCurrentResourceAllocations(retStatus);
	}

	Status ExecuteContext_impl::BeginLoggingResourceAllocations(const wchar_t* filePath)
	{
		return m_persistentWorkingSet->m_SDK_12->BeginLoggingResourceAllocations(filePath);
	}

	Status ExecuteContext_impl::EndLoggingResourceAllocations()
	{
		return m_persistentWorkingSet->m_SDK_12->EndLoggingResourceAllocations();
	}
}
