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

#include <assert.h>
#include <vector>
#include <list>
#include <array>
#include <memory>
#include <mutex>

namespace KickstartRT_ExportLayer
{
	extern std::mutex	g_APIInterfaceMutex; // DLLMain
};

struct ID3D12Fence;

namespace KickstartRT_ExportLayer
{
	class PersistentWorkingSet;
	struct TaskContainer;
	struct TaskContainer_impl;

	class ExecuteContext_impl : public ExecuteContext {
		std::mutex										m_mutex;
		uint32_t										m_numberOfWorkingSets = 0;
		uint64_t										m_taskIndex = 1;

	public:
		std::unique_ptr<PersistentWorkingSet>			m_persistentWorkingSet;

	public:
		ExecuteContext_impl();
		~ExecuteContext_impl();

		Status Init(const ExecuteContext_InitSettings* settings);

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

		Status InvokeGPUTask(TaskContainer* container, const BuildGPUTaskInput* input) override;
		Status ReleaseDeviceResourcesImmediately() override;

		Status GetLoadedShaderList(uint32_t* loadedListBuffer, size_t bufferSize, size_t* retListSize) override;

		Status GetCurrentResourceAllocations(ResourceAllocations* retStatus) override;
		Status BeginLoggingResourceAllocations(const wchar_t * filePath) override;
		Status EndLoggingResourceAllocations() override;
	};
};

