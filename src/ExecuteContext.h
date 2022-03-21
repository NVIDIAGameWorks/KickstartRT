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
#include <TaskTracker.h>
#include <Geometry.h>
#include <DenoisingContext.h>

#include <assert.h>
#include <vector>
#include <deque>
#include <list>
#include <array>
#include <memory>
#include <mutex>
#include <thread>

namespace KickstartRT_NativeLayer
{
	class PersistentWorkingSet;
	class TaskWorkingSet;
	class Scene;
};

namespace KickstartRT
{
	extern std::mutex	g_APIInterfaceMutex; // DLLMain
};

namespace KickstartRT_NativeLayer
{
	struct UpdateFromExecuteContext {
		std::deque<std::unique_ptr<BVHTask::Geometry>>	m_createdGeometries;
		std::deque<std::unique_ptr<BVHTask::Instance>>	m_createdInstances;
		std::deque<std::unique_ptr<DenoisingContext>>			m_createdDenoisingContexts;
		std::deque<GeometryHandle>								m_destroyedGeometries;
		std::deque<InstanceHandle>								m_destroyedInstances;
		std::deque<DenoisingContextHandle>						m_destroyedDenoisingContexts;
		bool													m_destroyAllGeometries = false;
		bool													m_destroyAllInstances = false;
		bool													m_destroyAllDenoisingContexts = false;
	};

	class ExecuteContext_impl : public ExecuteContext {
	protected:
		std::unique_ptr<TaskTracker>					m_taskTracker;

		ExecuteContext_InitSettings						m_initSettings;

		std::unique_ptr<Scene>							m_scene;
		std::unique_ptr<PersistentWorkingSet>			m_persistentWorkingSet;

		std::mutex										m_updateFromExecuteContextMutex;
		UpdateFromExecuteContext						m_updateFromExecuteContext;

	public:
		ExecuteContext_impl();
		virtual ~ExecuteContext_impl();

		Status Init(const ExecuteContext_InitSettings *settings);

		TaskContainer* CreateTaskContainer() override;

		DenoisingContextHandle CreateDenoisingContextHandle(const DenoisingContextInput* input) override;
		Status DestroyDenoisingContextHandle(DenoisingContextHandle handle) override;
		Status DestroyAllDenoisingContextHandles() override;

		GeometryHandle CreateGeometryHandle() override;
		Status CreateGeometryHandles(GeometryHandle* handles, uint32_t nbHandles) override;
		Status DestroyGeometryHandle(GeometryHandle handle) override;
		Status DestroyGeometryHandles(const GeometryHandle* handles, uint32_t nbHandles) override;
		Status DestroyAllGeometryHandles() override;

		InstanceHandle CreateInstanceHandle() override;
		Status CreateInstanceHandles(InstanceHandle* handles, uint32_t nbHandles) override;
		Status DestroyInstanceHandle(InstanceHandle handle) override;
		Status DestroyInstanceHandles(const InstanceHandle* handles, uint32_t nbHandles) override;
		Status DestroyAllInstanceHandles() override;

		Status BuildGPUTask(GPUTaskHandle *retHandle, TaskContainer *container, const BuildGPUTaskInput* input) override;
		Status MarkGPUTaskAsCompleted(GPUTaskHandle handle) override;
		Status ReleaseDeviceResourcesImmediately() override;

		Status GetLoadedShaderList(uint32_t* loadedListBuffer, size_t bufferSize, size_t* retListSize) override;

		Status GetCurrentResourceAllocations(ResourceAllocations* retStatus) override;
		Status BeginLoggingResourceAllocations(const wchar_t * filePath) override;
		Status EndLoggingResourceAllocations() override;
	};
};
