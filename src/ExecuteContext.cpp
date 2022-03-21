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
#include <ShaderFactory.h>

#include <cstring>
#include <inttypes.h>

namespace KickstartRT_NativeLayer
{
	// Counter for creating unique handles.
	static std::atomic<uint32_t>		s_geometryCounter(0ul);
	static std::atomic<uint32_t>		s_instanceCounter(0ul);
	static std::atomic<uint32_t>		s_denoisingContextCounter(0ul);

	static inline uint64_t IncrementHandleCounter(std::atomic<uint32_t>& c)
	{
		constexpr int HandleIDBits = 14;

		// atomic increment.
		return static_cast<uint64_t>(++c) << (64 - HandleIDBits);
	};

	namespace Version {
		KickstartRT::Version GetLibraryVersion()
		{
			return KickstartRT::Version();
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
		} else if (headerVersion.Minor != libVersion.Minor) {
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

		RETURN_IF_STATUS_FAILED(exc->Init(settings));

		*arg_exc = exc.release();
		return Status::OK;
	}

	Status ExecuteContext::Destruct(ExecuteContext* exc)
	{
		std::scoped_lock mtx(g_APIInterfaceMutex);

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
		m_scene = std::make_unique<Scene>();
	}

	ExecuteContext_impl::~ExecuteContext_impl()
	{
		m_scene.reset();
		m_taskTracker.reset();
		m_persistentWorkingSet.reset();
	}

	Status ExecuteContext_impl::Init(const ExecuteContext_InitSettings* settings)
	{
		if (settings == nullptr) {
			Log::Error(L"Invalid InitSettings detected");
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}
#if defined(GRAPHICS_API_D3D12)
		if (settings->D3D12Device == nullptr) {
			Log::Error(L"Invalid D3D12Device detected");
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}
#elif defined(GRAPHICS_API_VK)
		if (settings->device == nullptr || settings->physicalDevice == nullptr || settings->instance == nullptr) {
			Log::Error(L"Invalid vkDevice, vkPhysicalDevice or vkInstance detected");
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}
#endif

		if ((!settings->useInlineRaytracing) && (!settings->useShaderTableRaytracing)) {
			Log::Error(L"Either Inline or ShaderTable raytracing must be enabled.");
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}

		// check device's capabilities. 
#if defined(GRAPHICS_API_D3D12)
		{
			D3D12_FEATURE_DATA_D3D12_OPTIONS op = {};
			D3D12_FEATURE_DATA_D3D12_OPTIONS5 op5 = {};

			if (FAILED(settings->D3D12Device->CheckFeatureSupport(
				D3D12_FEATURE_D3D12_OPTIONS,
				&op,
				sizeof(op)))) {
				Log::Error(L"Failed to check feature support state.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
			if (FAILED(settings->D3D12Device->CheckFeatureSupport(
				D3D12_FEATURE_D3D12_OPTIONS5,
				&op5,
				sizeof(op5)))) {
				Log::Error(L"Failed to check feature support state.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
			if (op.ResourceBindingTier != D3D12_RESOURCE_BINDING_TIER_3) {
				Log::Error(L"Resource binding tier is not 3 on this device.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
			if (op5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
				Log::Error(L"Raytracing is not supported on this device.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
			if (settings->useShaderTableRaytracing && op5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
				Log::Error(L"Shader Table Raytracing is not supported on this device.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
			if (settings->useInlineRaytracing && op5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
				Log::Error(L"Inline Raytracing is not supported on this device.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
		}
#elif defined(GRAPHICS_API_VK)
		{
			VkPhysicalDeviceFeatures2 pdf2 = {};
			pdf2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

			VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtpf = {};
			rtpf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
			pdf2.pNext = &rtpf;

			VkPhysicalDeviceAccelerationStructureFeaturesKHR asf = {};
			asf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
			rtpf.pNext = &asf;

			VkPhysicalDeviceBufferDeviceAddressFeaturesEXT pdbdaf = {};
			pdbdaf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT;
			asf.pNext = &pdbdaf;

			vkGetPhysicalDeviceFeatures2(settings->physicalDevice, &pdf2);


			if (!asf.accelerationStructure) {
				Log::Error(L"VkPhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructure is not supported on this physical device.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
			if (!rtpf.rayTracingPipeline && settings->useShaderTableRaytracing) {
				Log::Error(L"VkPhysicalDeviceRayTracingPipelineFeaturesKHR::rayTracingPipeline is not supported on this physical device.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
			if (!pdbdaf.bufferDeviceAddress) {
				Log::Error(L"VkPhysicalDeviceBufferDeviceAddressFeaturesEXT::bufferDeviceAddress is not supported on this physical device.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}

			// instance extension.
			{
				uint32_t cnt = 0;
				if (vkEnumerateInstanceExtensionProperties(nullptr, &cnt, nullptr) != VK_SUCCESS) {
					Log::Error(L"vkEnumerateInstanceExtensionProperties() failed.");
					return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
				}
				if (cnt == 0) {
					Log::Error(L"There is no extension for this Vk instance. aborting..");
					return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
				}
				std::vector<VkExtensionProperties>	extList(cnt);
				if (vkEnumerateInstanceExtensionProperties(nullptr, &cnt, extList.data()) != VK_SUCCESS) {
					Log::Error(L"vkEnumerateInstanceExtensionProperties() failed.");
					return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
				}

				static constexpr std::array<const char*, 1> requiredExts = { "VK_EXT_debug_utils" };
				for (auto&& req : requiredExts) {
					bool found = false;
					for (auto&& e : extList) {
						if (strcmp(req, e.extensionName) == 0) {
							found = true;
							break;
						}
					}
					if (!found) {
						Log::Error(L"Required instance extension \"%s\" is not supported.", Log::ToWideString(req).c_str());
						return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
					}
				}
			}

			// device extension.
			{
				uint32_t cnt = 0;
				if (vkEnumerateDeviceExtensionProperties(settings->physicalDevice, nullptr, &cnt, nullptr) != VK_SUCCESS) {
					Log::Error(L"vkEnumerateDeviceExtensionProperties() failed.");
					return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
				}
				if (cnt == 0) {
					Log::Error(L"There is no extension for this Vk physical device. aborting..");
					return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
				}
				std::vector<VkExtensionProperties>	extList(cnt);
				if (vkEnumerateDeviceExtensionProperties(settings->physicalDevice, nullptr, &cnt, extList.data()) != VK_SUCCESS) {
					Log::Error(L"vkEnumerateDeviceExtensionProperties() failed.");
					return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
				}

				std::vector<const char*> requiredExts;
				if (settings->useShaderTableRaytracing)
					requiredExts.push_back("VK_KHR_ray_tracing_pipeline");
				if (settings->useInlineRaytracing)
					requiredExts.push_back("VK_KHR_ray_query");

				for (auto&& req : requiredExts) {
					bool found = false;
					for (auto&& e : extList) {
						if (strcmp(req, e.extensionName) == 0) {
							found = true;
							break;
						}
					}
					if (!found) {
						Log::Error(L"Required physical device extension \"%s\" is not supported.", Log::ToWideString(req).c_str());
						return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
					}
				}
			}
		}
#endif

		if (settings->supportedWorkingsets >= 10) {
			Log::Error(L"Supported working sets must be less than 10");
			return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
		}

		{
#if defined(GRAPHICS_API_D3D12)
			Microsoft::WRL::ComPtr<ID3D12Device5>   dev5;
			if (FAILED(settings->D3D12Device->QueryInterface(IID_PPV_ARGS(&dev5)))) {
				Log::Error(L"Failed to query ID3D12Device5 interface.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
			GraphicsAPI::Device::ApiData apidData = { dev5.Get() };
			std::unique_ptr<PersistentWorkingSet> pws = std::make_unique<PersistentWorkingSet>(apidData);
#elif defined(GRAPHICS_API_VK)
			GraphicsAPI::Device::ApiData apiData = {settings->device, settings->physicalDevice, settings->instance};
			std::unique_ptr<PersistentWorkingSet> pws = std::make_unique<PersistentWorkingSet>(apiData);
#endif
			RETURN_IF_STATUS_FAILED(pws->Init(settings));
			m_persistentWorkingSet = std::move(pws);
		}

		m_taskTracker = std::make_unique<TaskTracker>();
		{
			auto sts = m_taskTracker->Init(m_persistentWorkingSet.get(), settings);
			if (sts != Status::OK) {
				Log::Fatal(L"Failed to initialize task tracker.");
				return Status::ERROR_FAILED_TO_INIT_EXECUTE_CONTEXT;
			}
		}

		return Status::OK;
	}

	TaskContainer* ExecuteContext_impl::CreateTaskContainer()
	{
		return new TaskContainer_impl();
	}

	DenoisingContextHandle ExecuteContext_impl::CreateDenoisingContextHandle(const DenoisingContextInput* input)
	{
		if (input == nullptr) {
			Log::Fatal(L"DenoisingContextInput was null.");
			return DenoisingContextHandle::Null;
		}

#if !KickstartRT_SDK_WITH_NRD
		if (input->denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Reblur ||
			input->denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Relax ||
			input->denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Sigma) {
			Log::Fatal(L"Invalid denoising mode %s. Kickstart SDK was not built with NRD.", GetString(input->denoisingMethod));
			return DenoisingContextHandle::Null;
		}
#endif

		if (input->denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Reblur) {
			if (input->signalType != DenoisingContextInput::SignalType::Specular &&
				input->signalType != DenoisingContextInput::SignalType::Diffuse &&
				input->signalType != DenoisingContextInput::SignalType::SpecularAndDiffuse &&
				input->signalType != DenoisingContextInput::SignalType::DiffuseOcclusion) {
				Log::Fatal(L"Signal type %s not supported for denoising method %s", GetString(input->signalType), GetString(input->denoisingMethod));
				return DenoisingContextHandle::Null;
			}
		}

		if (input->denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Relax) {
			if (input->signalType != DenoisingContextInput::SignalType::Diffuse &&
				input->signalType != DenoisingContextInput::SignalType::Specular &&
				input->signalType != DenoisingContextInput::SignalType::SpecularAndDiffuse) {
				Log::Fatal(L"Signal type %s not supported for denoising method %s", GetString(input->signalType), GetString(input->denoisingMethod));
				return DenoisingContextHandle::Null;
			}
		}

		if (input->denoisingMethod == DenoisingContextInput::DenoisingMethod::NRD_Sigma) {
			if (input->signalType != DenoisingContextInput::SignalType::Shadow &&
				input->signalType != DenoisingContextInput::SignalType::MultiShadow) {
				Log::Fatal(L"Signal type %s not supported for denoising method %s", GetString(input->signalType), GetString(input->denoisingMethod));
				return DenoisingContextHandle::Null;
			}
		}

		DenoisingContextHandle ret;
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);
			m_updateFromExecuteContext.m_createdDenoisingContexts.emplace_back(
				std::make_unique<DenoisingContext>(IncrementHandleCounter(s_denoisingContextCounter), input));
			ret = m_updateFromExecuteContext.m_createdDenoisingContexts.back()->ToHandle();
		}
		return ret;
	}

	Status ExecuteContext_impl::DestroyDenoisingContextHandle(DenoisingContextHandle handle)
	{
		if (handle == DenoisingContextHandle::Null)
			return Status::OK;

		// SDK doesn't check if the handle is valid in the scene.
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);
			m_updateFromExecuteContext.m_destroyedDenoisingContexts.push_back(handle);
		}

		return Status::OK;
	}

	Status ExecuteContext_impl::DestroyAllDenoisingContextHandles()
	{
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);

			m_updateFromExecuteContext.m_createdDenoisingContexts.clear();
			m_updateFromExecuteContext.m_destroyedDenoisingContexts.clear();
			m_updateFromExecuteContext.m_destroyAllDenoisingContexts = true;
		}

		return Status::OK;
	}

	GeometryHandle ExecuteContext_impl::CreateGeometryHandle()
	{
		GeometryHandle ret;
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);
			m_updateFromExecuteContext.m_createdGeometries.emplace_back(
				std::make_unique<BVHTask::Geometry>(IncrementHandleCounter(s_geometryCounter)));
			ret = m_updateFromExecuteContext.m_createdGeometries.back()->ToHandle();
		}

		return ret;
	}

	Status ExecuteContext_impl::CreateGeometryHandles(GeometryHandle *handles, uint32_t nbHandles)
	{
		if (handles == nullptr) {
			Log::Fatal(L"Null pointer detected when creating geometry handles");
			return Status::ERROR_INVALID_PARAM;
		}
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);

			auto& d(m_updateFromExecuteContext.m_createdGeometries);
			for (size_t i = 0; i < nbHandles; ++i) {
				d.emplace_back(std::make_unique<BVHTask::Geometry>(IncrementHandleCounter(s_geometryCounter)));
				handles[i] = d.back()->ToHandle();
			}
		}

		return Status::OK;
	}

	Status ExecuteContext_impl::DestroyGeometryHandle(GeometryHandle handle)
	{
		if (handle == GeometryHandle::Null)
			return Status::OK;

		// SDK doesn't check if the handle is valid in the scene, since it requires mutex of the scene.
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);
			m_updateFromExecuteContext.m_destroyedGeometries.push_back(handle);
		}

		return Status::OK;
	}

	Status ExecuteContext_impl::DestroyGeometryHandles(const GeometryHandle* handles, uint32_t nbHandles)
	{
		if (handles == nullptr) {
			Log::Fatal(L"Null pointer detected when destorying geometry handles");
			return Status::ERROR_INVALID_PARAM;
		}
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);

			auto& d(m_updateFromExecuteContext.m_destroyedGeometries);
			d.insert(d.end(), handles + 0, handles + nbHandles);
		}

		return Status::OK;
	}

	Status ExecuteContext_impl::DestroyAllGeometryHandles()
	{
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);

			m_updateFromExecuteContext.m_createdGeometries.clear();
			m_updateFromExecuteContext.m_destroyedGeometries.clear();
			m_updateFromExecuteContext.m_destroyAllGeometries = true;
		}

		return Status::OK;
	}

	InstanceHandle ExecuteContext_impl::CreateInstanceHandle()
	{
		InstanceHandle ret;
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);
			m_updateFromExecuteContext.m_createdInstances.emplace_back(
				std::make_unique<BVHTask::Instance>(IncrementHandleCounter(s_instanceCounter)));
			ret = m_updateFromExecuteContext.m_createdInstances.back()->ToHandle();
		}

		return ret;
	}

	Status ExecuteContext_impl::CreateInstanceHandles(InstanceHandle* handles, uint32_t nbHandles)
	{
		if (handles == nullptr) {
			Log::Fatal(L"Null pointer detected when creating instance handles");
			return Status::ERROR_INVALID_PARAM;
		}
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);

			auto& d(m_updateFromExecuteContext.m_createdInstances);
			for (size_t i = 0; i < nbHandles; ++i) {
				d.emplace_back(std::make_unique<BVHTask::Instance>(IncrementHandleCounter(s_instanceCounter)));
				handles[i] = d.back()->ToHandle();
			}
		}

		return Status::OK;
	}

	Status ExecuteContext_impl::DestroyInstanceHandle(InstanceHandle handle)
	{
		if (handle == InstanceHandle::Null)
			return Status::OK;

		// SDK doesn't check if the handle is valid in the scene, since it requires mutex of the scene.
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);
			m_updateFromExecuteContext.m_destroyedInstances.push_back(handle);
		}

		return Status::OK;
	}

	Status ExecuteContext_impl::DestroyInstanceHandles(const InstanceHandle* handles, uint32_t nbHandles)
	{
		if (handles == nullptr) {
			Log::Fatal(L"Null pointer detected when destroying instance handles");
			return Status::ERROR_INVALID_PARAM;
		}
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);

			auto& d(m_updateFromExecuteContext.m_destroyedInstances);
			d.insert(d.end(), handles + 0, handles + nbHandles);
		}

		return Status::OK;
	}

	Status ExecuteContext_impl::DestroyAllInstanceHandles()
	{
		{
			std::scoped_lock mtx(m_updateFromExecuteContextMutex);

			m_updateFromExecuteContext.m_createdInstances.clear();
			m_updateFromExecuteContext.m_destroyedInstances.clear();
			m_updateFromExecuteContext.m_destroyAllInstances = true;
		}

		return Status::OK;
	}

	Status ExecuteContext_impl::MarkGPUTaskAsCompleted(GPUTaskHandle handle)
	{
		return m_taskTracker->UpdateFinishedTaskIndex(static_cast<uint64_t>(handle));
	}

	Status ExecuteContext_impl::BuildGPUTask(GPUTaskHandle *retHandle, TaskContainer *container, const BuildGPUTaskInput *input)
	{
		std::scoped_lock api_mtx(g_APIInterfaceMutex);

		*retHandle = GPUTaskHandle::Null;
		TaskContainer_impl* container_impl = static_cast<TaskContainer_impl*>(container);
		Status sts = Status::ERROR_INTERNAL;

		if (retHandle == nullptr) {
			Log::Fatal(L"Null task handle pointer detected.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (container_impl == nullptr) {
			Log::Fatal(L"Null TaskContainer pointer detected.");
			return Status::ERROR_INVALID_PARAM;
		}
		if (input == nullptr) {
			Log::Fatal(L"Null BuildGPUTaskInput pointer detected.");
			return Status::ERROR_INVALID_PARAM;
		}

		// Check if a TaskWorkingSet is available for this GPU task, otherwise SDK should return an error immediately without any state change,
		// To give a chance to the app to mark GPUTaskHandle as finished and retry building a GPU task again.
		if (! m_taskTracker->TaskWorkingSetIsAvailable()) {
			Log::Fatal(L"All task working sets are in-flight, consider increasing the number of task working set, or call MarkGPUTTaskAsCompleted() as early as possible.");
			return Status::ERROR_INTERNAL;
		}

		{
			std::scoped_lock mtx(container_impl->m_mutex);

			UpdateFromExecuteContext updateFromExc;
			{
				std::scoped_lock u_mtx(m_updateFromExecuteContextMutex);

				std::swap(updateFromExc.m_createdGeometries, m_updateFromExecuteContext.m_createdGeometries);
				std::swap(updateFromExc.m_createdInstances, m_updateFromExecuteContext.m_createdInstances);
				std::swap(updateFromExc.m_destroyedGeometries, m_updateFromExecuteContext.m_destroyedGeometries);
				std::swap(updateFromExc.m_destroyedInstances, m_updateFromExecuteContext.m_destroyedInstances);
				std::swap(updateFromExc.m_destroyAllGeometries, m_updateFromExecuteContext.m_destroyAllGeometries);
				std::swap(updateFromExc.m_destroyAllInstances, m_updateFromExecuteContext.m_destroyAllInstances);

				std::swap(updateFromExc.m_createdDenoisingContexts, m_updateFromExecuteContext.m_createdDenoisingContexts);
				std::swap(updateFromExc.m_destroyedDenoisingContexts, m_updateFromExecuteContext.m_destroyedDenoisingContexts);
				std::swap(updateFromExc.m_destroyAllDenoisingContexts, m_updateFromExecuteContext.m_destroyAllDenoisingContexts);
			}

			// build task
			sts = m_scene->BuildTask(retHandle, m_taskTracker.get(), m_persistentWorkingSet.get(), container_impl, &updateFromExc, input);
		}

		// delete task container here.
		delete container_impl;
		container_impl = nullptr;

		if (sts != Status::OK) {
			Log::Fatal(L"Failed to build task.");
		}
		return sts;
	}

	Status ExecuteContext_impl::ReleaseDeviceResourcesImmediately()
	{
		std::scoped_lock mtx(g_APIInterfaceMutex);

		if (m_taskTracker->CurrentTaskIndex() != m_taskTracker->FinishedTaskIndex()) {
			Log::Fatal(L"There are in-flight GPUTask when calling ReleaseDeviceResourcesImmediately(). This API need to be called after all GPUTaskHandles has been marked as completed.");
			return Status::ERROR_INVALID_PARAM;
		}

		Status sts = Status::ERROR_INTERNAL;

		{
			UpdateFromExecuteContext updateFromExc;
			{
				std::scoped_lock u_mtx(m_updateFromExecuteContextMutex);

				std::swap(updateFromExc.m_createdGeometries, m_updateFromExecuteContext.m_createdGeometries);
				std::swap(updateFromExc.m_createdInstances, m_updateFromExecuteContext.m_createdInstances);
				std::swap(updateFromExc.m_destroyedGeometries, m_updateFromExecuteContext.m_destroyedGeometries);
				std::swap(updateFromExc.m_destroyedInstances, m_updateFromExecuteContext.m_destroyedInstances);
				std::swap(updateFromExc.m_destroyAllGeometries, m_updateFromExecuteContext.m_destroyAllGeometries);
				std::swap(updateFromExc.m_destroyAllInstances, m_updateFromExecuteContext.m_destroyAllInstances);

				std::swap(updateFromExc.m_createdDenoisingContexts, m_updateFromExecuteContext.m_createdDenoisingContexts);
				std::swap(updateFromExc.m_destroyedDenoisingContexts, m_updateFromExecuteContext.m_destroyedDenoisingContexts);
				std::swap(updateFromExc.m_destroyAllDenoisingContexts, m_updateFromExecuteContext.m_destroyAllDenoisingContexts);
			}

			// build task
			sts = m_scene->ReleaseDeviceResourcesImmediately(m_taskTracker.get(), m_persistentWorkingSet.get(), &updateFromExc);
		}

		if (sts != Status::OK) {
			Log::Fatal(L"Failed to ReleaseDeviceResourcesImmediately.");
		}
		return sts;
	}

	Status ExecuteContext_impl::GetLoadedShaderList(uint32_t* loadedListBuffer, size_t bufferSize, size_t* retListSize)
	{
		if (loadedListBuffer == nullptr || loadedListBuffer == nullptr) {
			Log::Fatal(L"Null pointer detected in GetLoadedShaderList().");
			return Status::ERROR_INVALID_PARAM;
		}
		if (bufferSize < 16) {
			Log::Fatal(L"Loaded shader list buffer size must be larger than 16 elements.");
			return Status::ERROR_INVALID_PARAM;
		}

		{
			// Hold pws's mutex until exit from this function.
			std::scoped_lock pwsMutex(m_persistentWorkingSet->m_mutex);

			return m_persistentWorkingSet->m_shaderFactory->GetLoadedShaderList(loadedListBuffer, bufferSize, retListSize);
		}
	};

	Status ExecuteContext_impl::GetCurrentResourceAllocations(ResourceAllocations* retStatus)
	{
		return m_persistentWorkingSet->GetResourceAllocations(retStatus);
	}

	Status ExecuteContext_impl::BeginLoggingResourceAllocations(const wchar_t* filePath)
	{
		return m_persistentWorkingSet->BeginLoggingResourceAllocations(filePath);
	}

	Status ExecuteContext_impl::EndLoggingResourceAllocations()
	{
		return m_persistentWorkingSet->EndLoggingResourceAllocations();
	}
}
