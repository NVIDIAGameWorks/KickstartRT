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
#include <Scene.h>
#include <Platform.h>
#include <Utils.h>
#include <Log.h>
#include <PersistentWorkingSet.h>
#include <TaskWorkingSet.h>
#include <RenderPass_DirectLightingCacheAllocation.h>
#include <RenderPass_DirectLightingCacheInjection.h>
#include <RenderPass_DirectLightingCacheReflection.h>
#include <RenderPass_Denoising.h>
#include <TaskContainer.h>
#include <RenderTaskValidator.h>
#include <RenderPass_Common.h>

#include <cinttypes>

namespace KickstartRT_NativeLayer
{
	using namespace KickstartRT_NativeLayer::BVHTask;

	static bool TestInstanceInputMask(const InstanceInput& inputs, const InstanceInclusionMask testBits)
	{
		using uType = std::underlying_type<InstanceInclusionMask>::type;

		if (static_cast<uType>(inputs.instanceInclusionMask) & static_cast<uType>(testBits))
			return true;

		return false;
	}

	Status Scene::BuildTask(GPUTaskHandle* retHandle, TaskTracker* taskTracker, PersistentWorkingSet* pws, TaskContainer_impl* taskContainer, UpdateFromExecuteContext *updateFromExc, const BuildGPUTaskInput* input)
	{
		// Hold scene container's mutex until exit from this function.
		std::scoped_lock containerMutex(m_container.m_mutex);

		// the thread should already get the mutex in BuildGPUTask().
		// Hold task container's mutex until exit from this function.
		// std::scoped_lock taskContainerMutex(taskContainer->m_mutex);

		// Hold pws's mutex until exit from this function.
		std::scoped_lock pwsMutex(pws->m_mutex);

		Status sts;

		auto& buildInputs(*input);

		if (! buildInputs.geometryTaskFirst) {
			Log::Fatal(L"Currently not geometryTaskFirst isn't supproted. It will be supported soon...");
			return Status::ERROR_INTERNAL;
		}

		if (pws->HasTaskIndices()) {
			// Persistent working set holds a valid task index at the begging of the build gpu task, which shouldn't be happened.
			// Strongly susptected the last BuildGPUTask has been failed.
			Log::Fatal(L"Failed to start build gpu task since the last build gpu task has been failed.");
			return Status::ERROR_INTERNAL;
		}

		// Release expired device objects.
		pws->ReleaseDeferredReleasedDeviceObjects(taskTracker->FinishedTaskIndex());

		// Allocate task working set.
		TaskWorkingSet* taskWorkingSet = nullptr;
		{
			uint64_t	currentTaskIndex = (uint64_t)-1;
			sts = taskTracker->AllocateTaskWorkingSet(&taskWorkingSet, &currentTaskIndex);
			if (sts != Status::OK) {
				// Shouldn't be happend.
				Log::Fatal(L"Failed to allocate task workingset while building a gpu task.");
				return Status::ERROR_INTERNAL;
			}

			// set the current task index to pws.
			pws->SetTaskIndices(currentTaskIndex, taskTracker->FinishedTaskIndex());
		}

		// SDK does readback for direct lighting cache allocation in anytime.
		{
			bool allocationIsHappened = false;
			sts = DoReadbackAndTileAllocation(pws, allocationIsHappened);
			if (sts != Status::OK) {
				Log::Fatal(L"Failed to DoReadbackAndTileAllocation");
				return sts;
			}
		}

		// Set the user provided commandlist which has been opened already.
		std::unique_ptr<GraphicsAPI::CommandList> userCmdList;

#if defined(GRAPHICS_API_D3D12)
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> cl4;
		Microsoft::WRL::ComPtr<ID3D12DebugCommandList1> debugCl;
		{
			buildInputs.commandList->QueryInterface(IID_PPV_ARGS(&cl4));
			if (cl4 == nullptr) {
				Log::Fatal(L"Failed to get ID3D12GraphicsCommandList4 interface from user provided command list.");
				return Status::ERROR_INVALID_PARAM;
			}

#if _DEBUG
			buildInputs.commandList->QueryInterface(IID_PPV_ARGS(&debugCl));
#endif
			userCmdList = std::make_unique<GraphicsAPI::CommandList>();
			userCmdList->InitFromAPIData(cl4.Get(), debugCl.Get());
			userCmdList->BeginEvent({ 0, 255, 0 }, "KickStartSDK - User provided CommandList");
#if 0
			userCmdList->ClearState(); // clear state here.
#endif
			// the interface will be released with dtor of cmdList;
		}
#elif defined(GRAPHICS_API_VK)
		{
			userCmdList = std::make_unique<GraphicsAPI::CommandList>();
			userCmdList->InitFromAPIData(pws->m_device.m_apiData.m_device, buildInputs.commandBuffer);
			userCmdList->BeginEvent({ 0, 255, 0 }, "KickStartSDK - User provided CommandList");

			// TODO: VK don't have a good way to unbound all resources currently be bound.
			// userCmdList->ClearState();
		}
#endif

		// Process added / removed denoising contexts.
		sts = UpdateDenoisingContext(pws, updateFromExc);
		if (sts != Status::OK) {
			Log::Fatal(L"Failed to UpdateDenoisingContext");
			return sts;
		}

		// Process created / removed geometries and instances.
		// Registered and Updated geometries and instances are done in a different place.
		{
			bool isSceneChanged = false;
			sts = UpdateScenegraphFromExecuteContext(pws, updateFromExc, isSceneChanged);
			if (sts != Status::OK) {
				Log::Fatal(L"Failed to UpdateScenegraph");
				return sts;
			}
			m_TLASisDrity |= isSceneChanged;
		}

		{
			// This maps volatile constant buffer, sets descriptor heap.
			// Dtor automatically unmap buffer.
			// Ctor and Dtor don't open / close the command list.
			TaskWorkingSetCommandList cl(taskWorkingSet, userCmdList.get());

			// Do geometry taks is at the beginning or the end of entire process. 
			auto DoGeometyTask = [&]() {
				std::deque<Geometry*> addedGeometryPtrs;
				std::deque<Geometry*> updatedGeometryPtrs;
				std::deque<Instance*> addedInstancePtrs;
				std::deque<Instance*> updatedInstancePtrs;

				{
					bool SceneIsChanged = false;
					sts = UpdateScenegraphFromBVHTask(pws, taskContainer->m_bvhTask.get(),
						addedGeometryPtrs,
						updatedGeometryPtrs,
						addedInstancePtrs,
						updatedInstancePtrs,
						SceneIsChanged);
					if (sts != Status::OK) {
						Log::Fatal(L"Failed to update scene graph.");
						return Status::ERROR_INTERNAL;
					}
					m_TLASisDrity |= SceneIsChanged;
				}

				if (addedInstancePtrs.size() > 0 || addedGeometryPtrs.size() > 0 || updatedGeometryPtrs.size() > 0) {
					GraphicsAPI::Utils::ScopedEventObject sce(cl.m_commandList, { 0, 128, 0 }, DebugName("Geometry Task"));

					if (addedInstancePtrs.size() > 0) {
						bool allocationIsHappened = false;
						sts = DoAllocationForAddedInstances(pws, addedInstancePtrs, allocationIsHappened);
						if (sts != Status::OK) {
							Log::Fatal(L"Failed to DoAllocationForAddedInstances");
							return sts;
						}
					}

					if (addedGeometryPtrs.size() > 0 || updatedGeometryPtrs.size() > 0) {
						sts = BuildTransformAndTileAllocationCommands(
							cl.m_set, cl.m_commandList,
							addedGeometryPtrs, updatedGeometryPtrs);
						if (sts != Status::OK) {
							Log::Fatal(L"Failed to build Tansform and TileAllocation command list.");
							return Status::ERROR_INTERNAL;
						}
					}
				}

				// BLAS build process can be skipped when maxBlasBuildCount == 0 and no update geometry.
				if (taskContainer->m_bvhTask->m_maxBLASbuildCount > 0 || updatedGeometryPtrs.size() > 0) {
					GraphicsAPI::Utils::ScopedEventObject sce(cl.m_commandList, { 0, 128, 0 }, DebugName("BLAS Tasks"));

					bool BLASisChanged = false;
					// readback compacted blas size and allocate the packed buffer and copy the BLAS to it.
					sts = DoReadbackAndCompactBLASBuffers(pws, cl.m_commandList, BLASisChanged);
					if (sts != Status::OK) {
						Log::Fatal(L"Failed returned form DoReadbackAndCompactBLASBuffers() call");
						return sts;
					}

					if (m_container.m_buildBVHQueue.size() > 0 || updatedGeometryPtrs.size() > 0) {
						sts = BuildBLASCommands(cl.m_set, cl.m_commandList, updatedGeometryPtrs, taskContainer->m_bvhTask->m_maxBLASbuildCount, BLASisChanged);
						if (sts != Status::OK) {
							Log::Fatal(L"Failed to build BLAS task");
							return sts;
						}
					}
					m_TLASisDrity |= BLASisChanged;
				}

				if (taskContainer->m_bvhTask->m_buildTLAS && m_TLASisDrity) {
					sts = BuildTLASCommands(cl.m_set, cl.m_commandList);
					if (sts != Status::OK) {
						Log::Fatal(L"Failed to build TLAS task");
						return sts;
					}
					m_TLASisDrity = false;
				}

				return Status::OK;
			};

			if (buildInputs.geometryTaskFirst)
				DoGeometyTask();

			if (m_TLASisDrity && taskContainer->m_renderTask->m_renderTasks.size() > 0) {
				Log::Fatal(L"Tried to do a render task with a obsolete TLAS. Need to update TLAS first before any render task.");
				Log::Fatal(L"TLAS is always marked as obsolete when any scene change was happend.");
				return Status::ERROR_INVALID_PARAM;
			}

			bool NeedToUpdateDescTable = true;
			std::unique_ptr<GraphicsAPI::DescriptorTable>	lightingCache_descTable;
			std::deque<Instance*>						lightingCache_instances;

			RenderPass_ResourceRegistry resources(pws);

			for (auto& task : taskContainer->m_renderTask->m_renderTasks) {
				switch (task.GetType()) {
				case RenderTask::Task::Type::DirectLightInjection:
				case RenderTask::Task::Type::DirectLightTransfer:
				{
					GraphicsAPI::Utils::ScopedEventObject sce(cl.m_commandList, { 0, 128, 0 }, DebugName("Light Injection Task"));

					if (m_TLASBufferSrv) {
						// build desc table for all lighting cache.
						if (NeedToUpdateDescTable) {
							lightingCache_descTable = std::make_unique<GraphicsAPI::DescriptorTable>();
							sts = BuildDirectLightingCacheDescriptorTable(cl.m_set, &pws->m_RP_DirectLightingCacheInjection->m_descTableLayout2, lightingCache_descTable.get(), lightingCache_instances);
							if (sts != Status::OK) {
								Log::Fatal(L"Failed returned from BuildDirectLightingCacheDescriptorTable() call");
								return sts;
							}
							NeedToUpdateDescTable = false;
						}

						// Lighting Injection.
						if (m_enableInfoLog) {
							Log::Info(L"DirectLightingCacheInjection::BuildCommandList()");
						}

						// make clear lighting cache list and call CS.
						{
							std::deque<RenderPass_DirectLightingCacheInjection::CB_clear> clearList;
							std::deque<SharedBuffer::BufferEntry*> clearRes;

							// Check the instances for  clear request.
							for (size_t i = 0; i < lightingCache_instances.size(); ++i) {
								auto& ins(lightingCache_instances[i]);
								auto AddClearOp = [&ins, &clearRes, &clearList, i](uint32_t resourceIndex, SharedBuffer::BufferEntry* resource, size_t tileCount) {
									RenderPass_DirectLightingCacheInjection::CB_clear cbWrk = { };

									cbWrk.m_instanceIndex = (uint32_t)i;
									cbWrk.m_numberOfTiles = (uint32_t)tileCount;
									cbWrk.m_resourceIndex = resourceIndex;
									cbWrk.m_clearColor[0] = ins->m_input.initialTileColor[0];
									cbWrk.m_clearColor[1] = ins->m_input.initialTileColor[1];
									cbWrk.m_clearColor[2] = ins->m_input.initialTileColor[2];

									clearList.push_back(cbWrk);
									clearRes.push_back(resource);
								};

								if ((!ins->m_tileIsCleared) && ins->m_dynamicTileBuffer) {
									AddClearOp(1, ins->m_dynamicTileBuffer.get(), ins->m_dynamicTileBuffer->m_size / (2 * sizeof(uint32_t)));
									ins->m_tileIsCleared = true;
								}
							}
							if (clearList.size() > 0) {
								sts = pws->m_RP_DirectLightingCacheInjection->BuildCommandListClear(cl.m_set, cl.m_commandList, lightingCache_descTable.get(), clearList);
								if (sts != Status::OK) {
									Log::Fatal(L"Failed to build lighting injection command list");
									return sts;
								}

								// set uav barrier
								for (auto&& r : clearRes)
									r->RegisterBarrier();
								pws->m_sharedBufferForDirectLightingCache->UAVBarrier(cl.m_commandList);
							}
						}

						if (task.GetType() == RenderTask::Task::Type::DirectLightInjection)
						{
							auto& taskInj(task.Get<RenderTask::DirectLightingInjectionTask>());
							RETURN_IF_STATUS_FAILED(RenderTaskValidator::DirectLightingInjectionTask(&taskInj));

							sts = pws->m_RP_DirectLightingCacheInjection->BuildCommandListInject(cl.m_set, cl.m_commandList, &resources, lightingCache_descTable.get(), &taskInj);
							if (sts != Status::OK) {
								Log::Fatal(L"Failed to build lighting injection command list");
								return sts;
							}
						}
						else if (task.GetType() == RenderTask::Task::Type::DirectLightTransfer)
						{
							auto& taskTransfer(task.Get<RenderTask::DirectLightTransferTask>());
							RETURN_IF_STATUS_FAILED(RenderTaskValidator::DirectLightTransferTask(&taskTransfer));

							BVHTask::Instance* targetInstance = BVHTask::Instance::ToPtr(taskTransfer.target);

							if (!targetInstance->m_TLASInstanceListItr.has_value())
							{
								Log::Fatal(L"Instance is not part of TLAS.");
								return Status::ERROR_INVALID_INSTANCE_HANDLE;
							}

							const uint32_t targetInstanceIndex = (uint32_t)std::distance(m_container.m_TLASInstanceList.begin(), targetInstance->m_TLASInstanceListItr.value());

							RenderPass_DirectLightingCacheInjection::TransferParams params;
							params.targetInstanceIndex = targetInstanceIndex;

							sts = pws->m_RP_DirectLightingCacheInjection->BuildCommandListTransfer(cl.m_set, cl.m_commandList, &resources, lightingCache_descTable.get(), &taskTransfer, params);
							if (sts != Status::OK) {
								Log::Fatal(L"Failed to build lighting injection command list");
								return sts;
							}
						}
					}

					break;
				}
				case RenderTask::Task::Type::TraceSpecular:
				case RenderTask::Task::Type::TraceDiffuse:
				case RenderTask::Task::Type::TraceAmbientOcclusion:
				case RenderTask::Task::Type::TraceShadow:
				case RenderTask::Task::Type::TraceMultiShadow:
				{
					if (m_TLASBufferSrv) {
						RETURN_IF_STATUS_FAILED(RenderTaskValidator::TraceTask(&task.Get()));

						{ // Lighting
							GraphicsAPI::Utils::ScopedEventObject sce(cl.m_commandList, { 0, 128, 0 }, DebugName("Lighting Task"));

							// build desc table for all lighting cache.
							if (NeedToUpdateDescTable) {
								lightingCache_descTable = std::make_unique<GraphicsAPI::DescriptorTable>();
								sts = BuildDirectLightingCacheDescriptorTable(cl.m_set, &pws->m_RP_DirectLightingCacheInjection->m_descTableLayout2, lightingCache_descTable.get(), lightingCache_instances);
								if (sts != Status::OK) {
									Log::Fatal(L"Failed returned from BuildDirectLightingCacheDescriptorTable() call");
									return sts;
								}
								NeedToUpdateDescTable = false;
							}

							// Reflection
							if (m_enableInfoLog) {
								Log::Info(L"DirectLightingCacheReflection::BuildCommandList()");
							}
							sts = pws->m_RP_DirectLightingCacheReflection->BuildCommandList(cl.m_set, cl.m_commandList, &resources, lightingCache_descTable.get(), &task.Get());
							if (sts != Status::OK) {
								Log::Fatal(L"Failed to build reflection command list");
								return sts;
							}
						}
					}
					break;
				}
				case RenderTask::Task::Type::DenoiseSpecular:
				case RenderTask::Task::Type::DenoiseDiffuse:
				case RenderTask::Task::Type::DenoiseSpecularAndDiffuse:
				case RenderTask::Task::Type::DenoiseDiffuseOcclusion:
				case RenderTask::Task::Type::DenoiseShadow:
				case RenderTask::Task::Type::DenoiseMultiShadow:
				{
					RETURN_IF_STATUS_FAILED(RenderTaskValidator::DenoisingTask(&task.Get()));

					RenderTask::DenoisingOutput output;
					RETURN_IF_STATUS_FAILED(output.ConvertFromRenderTask(&task.Get()));

					GraphicsAPI::Utils::ScopedEventObject sce(cl.m_commandList, { 0, 128, 0 }, DebugName("Denoising Task"));
					DenoisingContext* context = DenoisingContext::ToPtr(output.context);
					sts = context->m_RP->BuildCommandList(cl.m_set, cl.m_commandList, &resources, output);
					if (sts != Status::OK) {
						Log::Fatal(L"Failed to build denoising command list");
						return sts;
					}

					break;
				}
				default:
					return Status::ERROR_INTERNAL;
				}
			}

			{
				resources.RestoreInitialStates(cl.m_commandList);
			}

			// release the dec table here.
			lightingCache_descTable.reset();
			lightingCache_instances.clear();

			// Do geometry task at the end.
			if (! buildInputs.geometryTaskFirst)
				DoGeometyTask();

			// clear state here to avoid state leaks from the SDK.
			// TODO VK does't have a good way to avoid state leaks, so we strongly encourage users to close it immediately.
#if defined(GRAPHICS_API_D3D12)
#if 0
			userCmdList->ClearState();
#endif
#endif
			userCmdList->EndEvent();
		}

		// return the task ticket.
		*retHandle = static_cast<GPUTaskHandle>(pws->GetCurrentTaskIndex());

		// Clear current task index. 
		pws->ClearTaskIndices();

		if (m_enableInfoLog) {
			Log::Info(L"Scene::BuildTask()  - end");
		}

		return Status::OK;
	}

	Status Scene::UpdateDenoisingContext(PersistentWorkingSet* pws, UpdateFromExecuteContext* updateFromExc)
	{
		// Delete expired instances...
		if (updateFromExc->m_destroyAllDenoisingContexts) {
			std::for_each(m_container.m_denoisingContexts.begin(), m_container.m_denoisingContexts.end(), [pws](auto&& itr) {itr->DeferredRelease(pws); });
			m_container.m_denoisingContexts.clear();
		}

		for (const DenoisingContextHandle removed : updateFromExc->m_destroyedDenoisingContexts) {

			auto addedIt = std::find_if(updateFromExc->m_createdDenoisingContexts.begin(), updateFromExc->m_createdDenoisingContexts.end(), [removed](std::unique_ptr<DenoisingContext>& context) {
				return context->ToHandle() == removed;
			});
			if (addedIt != updateFromExc->m_createdDenoisingContexts.end()) {
				updateFromExc->m_createdDenoisingContexts.erase(addedIt);
			}
			else {
				auto existingIt = std::find_if(m_container.m_denoisingContexts.begin(), m_container.m_denoisingContexts.end(), [removed](std::unique_ptr<DenoisingContext>& context) {
					return context->ToHandle() == removed;
				});
				if (existingIt != m_container.m_denoisingContexts.end()) {
					(*existingIt)->DeferredRelease(pws);
					m_container.m_denoisingContexts.erase(existingIt);
				}
				else {
					Log::Fatal(L"Invalid denoising context handle detected while destructing them.");
				}
			}
		}

		// Perform allocation of added denoising instances
		{
			for (std::unique_ptr<DenoisingContext>& added : updateFromExc->m_createdDenoisingContexts) {
				added->m_RP = std::make_unique<RenderPass_Denoising>();
				added->m_RP->Init(pws, added->m_input, pws->m_shaderFactory.get());
				m_container.m_denoisingContexts.push_back(std::move(added));
			}
		}

		return Status::OK;
	}

	Status Scene::UpdateScenegraphFromExecuteContext(PersistentWorkingSet* /*pws*/, UpdateFromExecuteContext* updateFromExc, bool& isSceneChanged)
	{
		auto RemoveInstanceFromGraph = [&](SceneContainer::InsMapIterator& itr)
		{
			//auto& ih(itr->first);
			auto& iPtr(itr->second);

			if (iPtr->m_registerStatus != BVHTask::RegisterStatus::Registered) {
				// This instance is created but not being registered to the scene graph.
				// Destrcut immediately.
				itr = m_container.m_instances.erase(itr);
				return;
			}

			// remove reference from the geometry.
			iPtr->m_geometry->m_instances.remove(iPtr.get());

			// invalidate the geometry reference.
			iPtr->m_geometry = nullptr;

			// Erase from TLAS instance list.
			if (iPtr->m_TLASInstanceListItr.has_value()) {
				m_container.m_TLASInstanceList.erase(iPtr->m_TLASInstanceListItr.value());
			}

			m_container.m_readyToDestructInstances.push_back(std::move(iPtr));
			itr = m_container.m_instances.erase(itr);

			isSceneChanged = true;
		};

		auto RemoveGeometryFromGraph = [&](SceneContainer::GeomMapIterator& itr)
		{
			auto& gh(itr->first);
			auto& ghPtr(itr->second);

			if (ghPtr->m_registerStatus != BVHTask::RegisterStatus::Registered) {
				// This geometry is created but not being registered to the scene graph.
				// Destrcut immediately.
				itr = m_container.m_geometries.erase(itr);
				return;
			}

			if (ghPtr->m_instances.size() > 0) {
				// the geometry is still being referenced by instances. Just hide from the scenegraph.
				m_container.m_removedGeometries.insert({ gh, std::move(ghPtr) });
			}
			else {
				// if it's not referenced from any instance, it's ready to destruct.
				m_container.m_readyToDestructGeometries.push_back(std::move(ghPtr));
			}
			itr = m_container.m_geometries.erase(itr);

			isSceneChanged = true;
		};

		// Destroy expired instances.
		if (updateFromExc->m_destroyAllInstances) {
			auto itr = m_container.m_instances.begin();
			while (itr != m_container.m_instances.end()) {
				RemoveInstanceFromGraph(itr);
			}
		}

		for (auto&& destIh : updateFromExc->m_destroyedInstances) {
			auto itr = m_container.m_instances.find(destIh);
			if (itr != m_container.m_instances.end()) {
				RemoveInstanceFromGraph(itr);
			}
			else {
				// Search in created instance list.
				Instance* destIhPtr = Instance::ToPtr(destIh);
				auto createdDestItr = std::find_if(
					updateFromExc->m_createdInstances.begin(),
					updateFromExc->m_createdInstances.end(),
					[destIhPtr](auto&& createdItr) { return createdItr.get() == destIhPtr; });
				if (createdDestItr != updateFromExc->m_createdInstances.end()) {
					// Found in created list. Destruct it immediately.
					createdDestItr->reset();
				}
				else {
					Log::Fatal(L"Invalid destructed instance handle dtected. %" PRIu64, destIh);
				}
			}
		}

		// Destroy expired geometries.
		if (updateFromExc->m_destroyAllGeometries) {
			auto itr = m_container.m_geometries.begin();
			while (itr != m_container.m_geometries.end()) {
				RemoveGeometryFromGraph(itr);
			}
		}
		for (auto&& destGh : updateFromExc->m_destroyedGeometries) {
			auto itr = m_container.m_geometries.find(destGh);
			if (itr != m_container.m_geometries.end()) {
				RemoveGeometryFromGraph(itr);
			}
			else {
				// Search in created geom list.
				Geometry* destGhPtr = Geometry::ToPtr(destGh);
				auto createdDestItr = std::find_if(
					updateFromExc->m_createdGeometries.begin(),
					updateFromExc->m_createdGeometries.end(),
					[destGhPtr](auto&& createdItr) { return createdItr.get() == destGhPtr; });
				if (createdDestItr != updateFromExc->m_createdGeometries.end()) {
					// Found in created list. Destruct it immediately.
					createdDestItr->reset();
				}
				else {
					Log::Fatal(L"Invalid destructed geometry handle dtected. %" PRIu64, destGh);
				}
			}
		}

		// Add all (valid) created geometries and instances to the SceneContainer.
		for (auto&& i : updateFromExc->m_createdGeometries) {
			if (!i)
				continue; // destructed.

			auto gh = i->ToHandle();
			m_container.m_geometries.insert({ gh, std::move(i) });
		}
		for (auto&& i : updateFromExc->m_createdInstances) {
			if (!i)
				continue; // destructed.

			auto ih = i->ToHandle();
			m_container.m_instances.insert({ ih, std::move(i) });
		}

		return Status::OK;
	}

	Status Scene::UpdateScenegraphFromBVHTask(PersistentWorkingSet * pws, BVHTasks* bvhTasks,
			std::deque<BVHTask::Geometry*>&addedGeometryPtrs,
			std::deque<BVHTask::Geometry*>&updatedGeometryPtrs,
			std::deque<BVHTask::Instance*>&addedInstancePtrs,
			std::deque<BVHTask::Instance*>&updatedInstancePtrs,
			bool& isSceneChanged)
	{
		isSceneChanged = false;

		auto RegisterGeometry = [&](GeometryHandle &gh) {
			auto ghItr = m_container.m_geometries.find(gh);
			if (ghItr == m_container.m_geometries.end()) {
				Log::Fatal(L"Invalid geometry handle dtected while registering. %" PRIu64, gh);
				return;
			}
			Geometry* gp = ghItr->second.get();
			addedGeometryPtrs.push_back(gp);
			gp->m_registerStatus = BVHTask::RegisterStatus::Registered;

			isSceneChanged = true;
		};
		auto RegisterInstance = [&](InstanceHandle& ih) {
			auto ihItr = m_container.m_instances.find(ih);
			if (ihItr == m_container.m_instances.end()) {
				Log::Fatal(L"Invalid instance handle dtected while registering. %" PRIu64, ih);
				return;
			}

			Instance* ip = ihItr->second.get();
			auto gItr = m_container.m_geometries.find(ip->m_input.geomHandle);
			if (gItr == m_container.m_geometries.end()) {
				Log::Fatal(L"Invalid geometry handle detected when registering an instance. %" PRIu64, (uint64_t)ip->m_input.geomHandle);
				return;
			}

			ip->m_geometry = Geometry::ToPtr(ip->m_input.geomHandle);
			ip->m_input.geomHandle = GeometryHandle::Null;

			// add reference from geometry.
			ip->m_geometry->m_instances.push_back(ip);

			addedInstancePtrs.push_back(ip);
			ip->m_registerStatus = BVHTask::RegisterStatus::Registered;

			isSceneChanged = true;
		};

		// Update all geometries and instances.
		for (auto&& upGeom : bvhTasks->m_updatedGeometries) {
			auto gItr = m_container.m_geometries.find(upGeom.m_gh);
			if (gItr == m_container.m_geometries.end()) {
				Log::Fatal(L"Invalid geometry handle detected when updating a geometry. %" PRIu64, (uint64_t)upGeom.m_gh);
				continue;
			}
			Geometry* gp = gItr->second.get();

#if 0
			if (RenderPass_DirectLightingCacheAllocation::CheckUpdateInputs(gp->m_input, upGeom.m_input) != Status::OK) {
				Log::Fatal(L"Invalid input values were detected when updating a geometry. %" PRIu64, (uint64_t)upGeom.m_gh);
				continue;
			}
#endif

			// only vertex buffer is going to be updated.
			assert(gp->m_input.components.size() == upGeom.m_input.components.size());
			for (size_t i = 0; i < upGeom.m_input.components.size(); ++i) {
				auto& src(upGeom.m_input.components[i]);
				auto& dst(gp->m_input.components[i]);
				dst.vertexBuffer = src.vertexBuffer;
				dst.useTransform = src.useTransform;
				dst.transform = src.transform;
			}

			// Do not add this to the update geom list when it's just registered.
			if (gp->m_registerStatus == BVHTask::RegisterStatus::Registered)
				updatedGeometryPtrs.push_back(gp);
		}
		if (updatedGeometryPtrs.size() > 0)
			isSceneChanged = true;

		for (auto&& upIns : bvhTasks->m_updatedInstances) {
			auto iItr = m_container.m_instances.find(upIns.m_ih);
			if (iItr == m_container.m_instances.end()) {
				Log::Fatal(L"Invalid instance handle detected when updating a instance. %" PRIu64, (uint64_t)upIns.m_ih);
				continue;
			}
			Instance* ip = iItr->second.get();

			// update transform and visiblity from input
			ip->m_input.transform = upIns.m_input.transform;
			ip->m_input.participatingInTLAS = upIns.m_input.participatingInTLAS;
			ip->m_input.instanceInclusionMask = upIns.m_input.instanceInclusionMask;

			// Do not add this to the update instance list when it's just registered.
			if (ip->m_registerStatus == BVHTask::RegisterStatus::Registered)
				updatedInstancePtrs.push_back(ip);

			// If its TLAS participating status is changed to disable and if it is participating in TLAS, remove it.
			if (! ip->m_input.participatingInTLAS) {
				if (ip->m_TLASInstanceListItr.has_value()) {
					m_container.m_TLASInstanceList.erase(ip->m_TLASInstanceListItr.value());
					ip->m_TLASInstanceListItr.reset();
				}
			}
		}
		if (updatedInstancePtrs.size() > 0)
			isSceneChanged = true;

		// Register all (valid) geometries and instances.
		for (auto&& addedGeom : bvhTasks->m_registeredGeometries) {
			if (addedGeom == GeometryHandle::Null)
				continue;
			RegisterGeometry(addedGeom);
		}
		for (auto&& addedIns : bvhTasks->m_registeredInstances) {
			if (addedIns == InstanceHandle::Null)
				continue;
			RegisterInstance(addedIns);
		}

		// Check if removed geometry is not referenced from any instance and move them to readyToDestructGeometries list.
		if (isSceneChanged) {
			for (auto itr = m_container.m_removedGeometries.begin(); itr != m_container.m_removedGeometries.end(); ) {
				if (itr->second->m_instances.size() == 0) {
					m_container.m_readyToDestructGeometries.push_back(std::move(itr->second));
					itr = m_container.m_removedGeometries.erase(itr);
				}
				else {
					++itr;
				}
			}
		}

		// Destruct geometries and instances.
		while (!m_container.m_readyToDestructInstances.empty()) {
			// use deferred release for device resources.
			m_container.m_readyToDestructInstances.begin()->get()->DeferredRelease(pws);
			m_container.m_readyToDestructInstances.pop_front();
		}
		while (!m_container.m_readyToDestructGeometries.empty()) {
			// use deferred release for device resources.
			m_container.m_readyToDestructGeometries.begin()->get()->DeferredRelease(pws);
			m_container.m_readyToDestructGeometries.pop_front();
		}

#if 0
		// Debug: to dump the current allocator status.
		if (pws->GetFrameIndex() % 120 == 119) {
			static int cnt;
			std::array<wchar_t, 1023>	sBuf;
			swprintf(sBuf.data(), sBuf.size(), L"C:\\users\\mtakeshige\\desktop\\Allocator_%02d.txt", cnt++);
			std::filesystem::path logPath(sBuf.data());

			std::ofstream of;
			of.open(logPath, std::ios::out);
			if (of.is_open()) {
				of << pws->m_sharedBufferForBLASPermanent->DumpAllocator(true, true, true) << std::endl;
			}
			of.close();

		}
#endif

		return Status::OK;
	};

	static Status AllocateTileForInstance(PersistentWorkingSet *pws, Instance* ip, uint32_t numOfTiles)
	{
		if (ip->m_geometry->m_input.surfelType == BVHTask::GeometryInput::SurfelType::WarpedBarycentricStorage) {
			ip->m_dynamicTileBuffer = pws->m_sharedBufferForDirectLightingCache->Allocate(
				pws, sizeof(uint32_t) * 2 * numOfTiles, true);
			if (!ip->m_dynamicTileBuffer) {
				Log::Fatal(L"Failed to allocate a direct lighting chache buffer NumTiles:%d", numOfTiles);
				return Status::ERROR_INTERNAL;
			}
		}
		else if (ip->m_geometry->m_input.surfelType == BVHTask::GeometryInput::SurfelType::MeshColors) {
			size_t size = 0;
			size += sizeof(uint32_t) * 2 * ip->m_geometry->m_totalNbIndices;
			size += sizeof(uint32_t) * numOfTiles;
			ip->m_dynamicTileBuffer = pws->m_sharedBufferForDirectLightingCache->Allocate(
				pws, size, true);
			if (!ip->m_dynamicTileBuffer) {
				Log::Fatal(L"Failed to allocate a direct lighting chache buffer NumTiles:%d", numOfTiles);
				return Status::ERROR_INTERNAL;
			}
		}
		else {
			return Status::ERROR_INTERNAL;
		}

		ip->m_numberOfTiles = numOfTiles;

#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
#else
		ip->m_needToUpdateUAV = true;
#endif

		return Status::OK;
	};

	Status Scene::DoReadbackAndTileAllocation(PersistentWorkingSet* pws, bool& AllocationHappened)
	{
		if (m_enableInfoLog) {
			Log::Info(L"DoReadbackAndTileAllocation()");
		}

		// readback tile cache buffer size if it's ready, and then allocate it.
		uint64_t completedFenceValue = pws->GetLastFinishedTaskIndex();
		std::vector<Geometry*>	readyToReadback;

		bool loggedMessage = false;
		while (!m_container.m_waitingForTileAllocationGeometries.empty()) {
			auto&& itr(m_container.m_waitingForTileAllocationGeometries.begin());
			auto& fenceValue(itr->first);

			if (fenceValue > completedFenceValue)
				break;

			GeometryHandle gh = itr->second;
			m_container.m_waitingForTileAllocationGeometries.pop_front();

			// Handes should be even safer than the raw pointer since it can find a different new -> delete -> new sctructures that have the same addresses.
			auto gItr = m_container.m_geometries.find(gh);
			if (gItr == m_container.m_geometries.end()) {
				// the geometry has been removed before doing redback.
				if (!loggedMessage) {
					Log::Warning(L"GeometryHandle was removed while calculating tile cache buffer size.");
					loggedMessage = true;
				}
				continue;
			}

			// the geometry is still alive.
			readyToReadback.push_back(gItr->second.get());
		}

		if (!readyToReadback.empty()) {
			// do readback
			for (auto&& gp : readyToReadback) {
				if (!gp->m_input.forceDirectTileMapping)
					gp->m_directLightingCacheCounter_Readback->RegisterBatchMap();
			}
			pws->m_sharedBufferForReadback->BatchMap(&pws->m_device, GraphicsAPI::Buffer::MapType::Read);

			for (auto&& gp : readyToReadback) {
				if (!gp->m_input.forceDirectTileMapping) { // direct tile mapping don't need to readback allocation info.
					void* ptr = gp->m_directLightingCacheCounter_Readback->GetMappedPtr();
					uint32_t nbTiles = *reinterpret_cast<uint32_t*>(ptr);

					if (nbTiles == 0 && gp->m_input.surfelType == BVHTask::GeometryInput::SurfelType::WarpedBarycentricStorage) {
						Log::Fatal(L"Invalid direct light cache size detected: %d. (possibly failed to read back the size. Fence overrun is suspected).", nbTiles);
					}

					if (nbTiles == kInvalidNumTiles) {
						Log::Fatal(L"Invalid direct light cache size detected: %d. (possibly failed to read back the size. Fence overrun is suspected).", nbTiles);
					}

					gp->m_numberOfTiles = nbTiles;

					// release counter buffer
					pws->DeferredRelease(std::move(gp->m_directLightingCacheCounter));
					pws->DeferredRelease(std::move(gp->m_directLightingCacheCounter_Readback));
				}
			}

			pws->m_sharedBufferForReadback->BatchUnmap(&pws->m_device, GraphicsAPI::Buffer::MapType::Read);

			for (auto&& gp : readyToReadback) {
				if (gp->m_input.surfelType == BVHTask::GeometryInput::SurfelType::WarpedBarycentricStorage) {
					uint32_t nbPrim = gp->m_totalNbIndices / 3;

					// Check if the geometry falls into direct tile mapping.
					if (!gp->m_input.forceDirectTileMapping)
					{
						const float tileRatio = (float)nbPrim / (float)gp->m_numberOfTiles;

						if (tileRatio > gp->m_input.directTileMappingThreshold) {
							// Log::Info(L"Direct maping: NbPrim %d  NbTile %d", gh->m_inputs.IndexBuffer.count / 3, gh->m_numberOfTiles);
							// release TLC indices and set a frag.
							gp->m_directTileMapping = true;
							gp->m_numberOfTiles = nbPrim;

							pws->DeferredRelease(std::move(gp->m_directLightingCacheIndices));
						}
					}
					else {
						// force direct tile mapping
						gp->m_directTileMapping = true;
						gp->m_numberOfTiles = nbPrim;
					}
				}

				// allocate direct lighting cache buffers of instances.
				for (auto&& ip : gp->m_instances) {
					AllocationHappened = true;
					RETURN_IF_STATUS_FAILED(AllocateTileForInstance(pws, ip, gp->m_numberOfTiles));
				}
			}

			if (m_enableInfoLog) {
				Log::Info(L"ReadbackCount: %d", readyToReadback.size());
			}
		}

		return Status::OK;
	}

	Status Scene::DoAllocationForAddedInstances(PersistentWorkingSet* pws, std::deque<Instance*>& addedInstancePtrs, bool& AllocationHappened)
	{
		if (m_enableInfoLog) {
			Log::Info(L"DoAllocationForAddedInstances : Cnt: %d", addedInstancePtrs.size());
		}

		for (auto* ip : addedInstancePtrs) {
			auto* gp = ip->m_geometry;

			// it already have lighting cache buffer.
			if (ip->m_dynamicTileBuffer)
				continue;
			// the geometry is now calculating tile budget.
			if (gp->m_numberOfTiles == kInvalidNumTiles)
				continue;

			AllocationHappened = true;
			RETURN_IF_STATUS_FAILED(AllocateTileForInstance(pws, ip, gp->m_numberOfTiles));
		}

		return Status::OK;
	}

	Status Scene::BuildTransformAndTileAllocationCommands(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList,
		std::deque<Geometry *>&	addedGeometries,
		std::deque<Geometry *>& updatedGeometries)
	{
		if (m_enableInfoLog) {
			Log::Info(L"BuildTransformAndTileAllocationCommands : AddedCnt: %d, UpdatedCnt: %d", addedGeometries.size(), updatedGeometries.size());
		}

		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);
		uint64_t currentFenceValue = pws->GetCurrentTaskIndex();
		GraphicsAPI::Utils::ScopedEventObject sce(cmdList, { 0, 128, 0 }, DebugName("Transform And Tile Allocation"));

		// add
		if (addedGeometries.size() > 0) {
			// allocate resources for transforming vertex buffers (and tile allocation).
			RETURN_IF_STATUS_FAILED(RenderPass_DirectLightingCacheAllocation::AllocateResourcesForGeometry(tws, addedGeometries));

			// dispatch CS
			RETURN_IF_STATUS_FAILED(pws->m_RP_DirectLightingCacheAllocation->BuildCommandListForAdd(tws, cmdList, addedGeometries));

			for (auto gp : addedGeometries) {
				auto gh = gp->ToHandle();
				// queue up for readbacking calculated tile size.
				m_container.m_waitingForTileAllocationGeometries.push_back({ currentFenceValue, gh });
				// queue up for building BVH process
				m_container.m_buildBVHQueue.push_back(gh);
			}
		}

		// update
		if (updatedGeometries.size() > 0) {
			// dispatch CS
			RETURN_IF_STATUS_FAILED(pws->m_RP_DirectLightingCacheAllocation->BuildCommandListForUpdate(tws, cmdList, updatedGeometries));
		}

		// clear input resources to makes sure not to touch them anymore.
		for (auto&& gh : addedGeometries) {
			for (auto&& cmp : gh->m_input.components) {
#if defined(GRAPHICS_API_D3D12)
				cmp.vertexBuffer.resource = nullptr;
				cmp.indexBuffer.resource = nullptr;
#elif defined(GRAPHICS_API_VK)
				cmp.vertexBuffer.typedBuffer = nullptr;
				cmp.indexBuffer.typedBuffer = nullptr;
#endif
			}
		}
		for (auto&& gh : updatedGeometries) {
			for (auto&& cmp : gh->m_input.components) {
#if defined(GRAPHICS_API_D3D12)
				cmp.vertexBuffer.resource = nullptr;
				cmp.indexBuffer.resource = nullptr;
#elif defined(GRAPHICS_API_VK)
				cmp.vertexBuffer.typedBuffer = nullptr;
				cmp.indexBuffer.typedBuffer = nullptr;
#endif
			}
		}

		return Status::OK;
	}

	Status Scene::DoReadbackAndCompactBLASBuffers(PersistentWorkingSet* pws, GraphicsAPI::CommandList* cmdList, bool& BLASChanged)
	{
		// readback compacted BLAS size when it's ready, and then allocate it.
		uint64_t completedFenceValue = pws->GetLastFinishedTaskIndex();
		std::deque<Geometry *>	readyToReadback;

		if (m_enableInfoLog) {
			Log::Info(L"DoReadbackAndCompactBLASBuffers()");
		}

		bool loggedMessage = false;
		while (!m_container.m_waitingForBVHCompactionGeometries.empty()) {
			auto&& itr(m_container.m_waitingForBVHCompactionGeometries.begin());
			auto& fenceValue(itr->first);
			if (fenceValue > completedFenceValue)
				break;

			GeometryHandle gh = itr->second;
			m_container.m_waitingForBVHCompactionGeometries.pop_front();

			auto gItr = m_container.m_geometries.find(gh);
			if (gItr == m_container.m_geometries.end()) {
				// The geometry has been removed before doing redback.
				if (!loggedMessage) {
					Log::Warning(L"GeometryHandle has been removed while calculating compacted BVH size.");
					loggedMessage = true;
				}
				continue;
			}

			readyToReadback.push_back(gItr->second.get());
		}

		// nothing to do.
		if (readyToReadback.empty())
			return Status::OK;

		if (m_enableInfoLog) {
			Log::Info(L"ReadyToReadback(CompactedSize) : %d", readyToReadback.size());
		}

		// BLAS was modified, changed address so TLAS is need to rebuild.
		BLASChanged = true;

		// do readback
		std::deque<uint64_t> packedBLASSize;

#if defined(GRAPHICS_API_D3D12)
		for (auto&& gp : readyToReadback) {
			gp->m_BLASCompactionSizeBuffer_Readback->RegisterBatchMap();
		}
		pws->m_sharedBufferForReadback->BatchMap(&pws->m_device, GraphicsAPI::Buffer::MapType::Read);

		for (auto&& gp : readyToReadback) {
			void* ptr = nullptr;

			ptr = gp->m_BLASCompactionSizeBuffer_Readback->GetMappedPtr();
			packedBLASSize.push_back(*reinterpret_cast<uint64_t*>(ptr));

			// release counter buffer
			pws->DeferredRelease(std::move(gp->m_BLASCompactionSizeBuffer_Readback));
		}

		pws->m_sharedBufferForReadback->BatchUnmap(&pws->m_device, GraphicsAPI::Buffer::MapType::Read);

#elif defined(GRAPHICS_API_VK)
		for (auto&& gp : readyToReadback) {
			VkDeviceSize	devSize;

			auto sts = vkGetQueryPoolResults(
				pws->m_device.m_apiData.m_device,
				gp->m_BLASCompactionSizeQueryPool->m_apiData.m_queryPool,
				0, 1,
				sizeof(VkDeviceSize),
				&devSize,
				sizeof(VkDeviceSize),
				VK_QUERY_RESULT_64_BIT);

			if (sts == VK_NOT_READY) {
				Log::Fatal(L"BLAS compaction size query was not ready to read.");
				return (Status::ERROR_INTERNAL);
			}
			else if (sts != VK_SUCCESS) {
				Log::Fatal(L"Failed to read BLAS compaction size query.");
				return (Status::ERROR_INTERNAL);
			}

			packedBLASSize.push_back((uint64_t)devSize);

			// release query pool.
			pws->DeferredRelease(std::move(gp->m_BLASCompactionSizeQueryPool)); // this resource is not tracked.
		}
#endif
		// allocate compacted BLAS buffer
		std::deque<std::unique_ptr<SharedBuffer::BufferEntry>>	packedBuffers;
		for (auto&& siz : packedBLASSize) {
			if (siz == 0) {
				Log::Fatal(L"Invalid compacted BLAS size detected : %" PRIu64 " bytes. (possibly failed to read back the size. fence overrun?)", siz);
				return (Status::ERROR_INTERNAL);
			}
			auto b = pws->m_sharedBufferForBLASPermanent->Allocate(pws, siz, true);
			if (!b) {
				Log::Fatal(L"Failed to allocate a compacted sized BLAS buffer: " PRIu64, siz);
				return (Status::ERROR_INTERNAL);
			}
			packedBuffers.emplace_back(std::move(b));
		}

		// copy BLAS into the packed size.
		for (size_t i = 0; i < readyToReadback.size(); ++i) {
			auto& gp(readyToReadback[i]);
			auto& b(packedBuffers[i]);

			if (m_enableInfoLog) {
				Log::Info(L"BLAS compaction done: [%d] -> [%d].", gp->m_BLASBuffer->m_size, b->m_size);
			}

#if defined(GRAPHICS_API_D3D12)
			{
				D3D12_GPU_VIRTUAL_ADDRESS src = { gp->m_BLASBuffer->GetGpuPtr() };
				D3D12_GPU_VIRTUAL_ADDRESS dst = { b->GetGpuPtr() };

				cmdList->m_apiData.m_commandList->CopyRaytracingAccelerationStructure(dst, src, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
			}
#elif defined(GRAPHICS_API_VK)
			{
				VkCopyAccelerationStructureInfoKHR copyInfo = {};
				copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
				copyInfo.src = gp->m_BLASBuffer->m_uav->m_apiData.m_accelerationStructure;
				copyInfo.dst = b->m_uav->m_apiData.m_accelerationStructure;
				copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;

				GraphicsAPI::VK::vkCmdCopyAccelerationStructureKHR(cmdList->m_apiData.m_commandBuffer, &copyInfo);
			}
#endif

			std::swap(gp->m_BLASBuffer, b);

			pws->DeferredRelease(std::move(b));
		}

		// D3D12 needs barriers to make sure the finish of the copies before building a TLAS.
		// VK needs to set barriers for VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT state.
		{
			for (auto&& gp : readyToReadback)
				gp->m_BLASBuffer->RegisterBarrier();
			pws->m_sharedBufferForBLASPermanent->UAVBarrier(cmdList);
		}

		return Status::OK;
	}

	Status Scene::BuildBLASCommands(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList,
		std::deque<Geometry *>& updatedGeometryPtrs,
		uint32_t maxBlasBuildTasks, bool& BLASChanged)
	{
#if defined(GRAPHICS_API_VK)
		namespace VK = KickstartRT_NativeLayer::GraphicsAPI::VK;
#endif

		if (m_enableInfoLog) {
			Log::Info(L"BuildBLASCommands(): UpdateCnt: %d, Queued: %d", updatedGeometryPtrs.size(), m_container.m_buildBVHQueue.size());
		}

		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);

		GraphicsAPI::Utils::ScopedEventObject sce(cmdList, { 0, 128, 0 }, DebugName("Build BLAS"));

		std::deque<Geometry*> updatedGeometries;
		for (size_t i = 0; i < updatedGeometryPtrs.size(); ++i) {
			// if updated geometry still doesn't have a valid BLAS, it doesn't need to do update BVH processs.
			// Just need to do create BVH process with the updated vertex buffer.
			if (!updatedGeometryPtrs[i]->m_BLASBuffer)
				continue;
			updatedGeometries.push_back(updatedGeometryPtrs[i]);
		}

		bool loggedMessage = false;
		std::deque<Geometry *> buildGeometries;
		while (m_container.m_buildBVHQueue.size() > 0) {
			if (buildGeometries.size() >= maxBlasBuildTasks)
				break;

			GeometryHandle gh = m_container.m_buildBVHQueue.front();
			m_container.m_buildBVHQueue.pop_front();

			// Handle is even better than the raw pointer to detect new -> delete -> new senario.
			auto gItr = m_container.m_geometries.find(gh);
			if (gItr == m_container.m_geometries.end()) {
				if (!loggedMessage) {
					Log::Info(L"A geometry has been removed before building BVH.");
					loggedMessage = true;
				}
				continue;
			}

			buildGeometries.push_back(gItr->second.get());
		}

		size_t nbGeomsToProcesss = buildGeometries.size() + updatedGeometries.size();

		// nothing to do.
		if (nbGeomsToProcesss == 0)
			return Status::OK;

		if (m_enableInfoLog) {
			Log::Info(L"NbGeomsToProcess: %d", nbGeomsToProcesss);
		}

		BLASChanged = true;

#if defined(GRAPHICS_API_D3D12)
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> rtGeomDescs(nbGeomsToProcesss);
		std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> asInputs(nbGeomsToProcesss);
		std::vector<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO> asPreBuildInfo(nbGeomsToProcesss);

		// allocate resources for BLAS
		auto GetPBInfo = [&](
			Geometry *gp,
			D3D12_RAYTRACING_GEOMETRY_DESC& desc,
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& asInput,
			D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& asPBInfo,
			bool performUpdate) {
				desc = {};
				desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
				desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

				desc.Triangles.VertexBuffer.StartAddress = gp->m_index_vertexBuffer->GetGpuPtr() + gp->m_vertexBufferOffsetInBytes;
				desc.Triangles.IndexBuffer = gp->m_index_vertexBuffer->GetGpuPtr();

				desc.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
				desc.Triangles.VertexCount = gp->m_totalNbVertices;
				desc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
				desc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
				desc.Triangles.IndexCount = gp->m_totalNbIndices;

				asInput = {};
				asInput.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
				asInput.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
				asInput.pGeometryDescs = &desc;
				asInput.NumDescs = 1;

				if (gp->m_input.buildHint == decltype(gp->m_input)::BuildHint::Auto) {
					if (gp->m_input.allowUpdate)
						asInput.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
					else
						asInput.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
				}
				else if (gp->m_input.buildHint == decltype(gp->m_input)::BuildHint::PreferFastBuild) {
					asInput.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
				}
				else if (gp->m_input.buildHint == decltype(gp->m_input)::BuildHint::PreferFastTrace) {
					asInput.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
				}

				if (gp->m_input.allowUpdate)
					asInput.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
				else
					asInput.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;

				if (performUpdate)
					asInput.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;

				// Get the size requirements for the BLAS buffer
				pws->m_device.m_apiData.m_device->GetRaytracingAccelerationStructurePrebuildInfo(&asInput, &asPBInfo);

				asPBInfo.ScratchDataSizeInBytes = GraphicsAPI::ALIGN((UINT64)D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, asPBInfo.ScratchDataSizeInBytes);
				asPBInfo.ResultDataMaxSizeInBytes = GraphicsAPI::ALIGN((UINT64)D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, asPBInfo.ResultDataMaxSizeInBytes);
		};

		{
			size_t i = 0;
			for (auto&& gp : updatedGeometries) {
				GetPBInfo(gp, rtGeomDescs[i], asInputs[i], asPreBuildInfo[i], true);
				++i;
			}
			for (auto&& gp : buildGeometries) {
				GetPBInfo(gp, rtGeomDescs[i], asInputs[i], asPreBuildInfo[i], false);
				++i;
			}
		}

#elif defined(GRAPHICS_API_VK)
		std::vector<VkAccelerationStructureGeometryKHR> asGeomArr(nbGeomsToProcesss);
		std::vector<VkAccelerationStructureBuildGeometryInfoKHR> geomInfoArr(nbGeomsToProcesss);
		std::vector<VkAccelerationStructureBuildRangeInfoKHR> rangeInfoArr(nbGeomsToProcesss);
		std::vector<VkAccelerationStructureBuildSizesInfoKHR> sizeInfoArr(nbGeomsToProcesss);

		auto GetPBInfo = [&](
			BVHTask::Geometry *gp,
			VkAccelerationStructureGeometryKHR& asGeom,
			VkAccelerationStructureBuildGeometryInfoKHR& geomInfo,
			VkAccelerationStructureBuildRangeInfoKHR& rangeInfo,
			VkAccelerationStructureBuildSizesInfoKHR& sizeInfo,
			bool performUpdate) {
				// BLAS builder requires raw device addresses.
				VkDeviceAddress vertexAddress = gp->m_index_vertexBuffer->GetGpuPtr() + gp->m_vertexBufferOffsetInBytes;
				VkDeviceAddress indexAddress = gp->m_index_vertexBuffer->GetGpuPtr();

				uint32_t maxPrimitiveCount = gp->m_totalNbIndices / 3;

				// Describe buffer as array of VertexObj.
				VkAccelerationStructureGeometryTrianglesDataKHR triangles = {};
				triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
				triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;  // flaot3 for position.
				triangles.vertexData.deviceAddress = vertexAddress;
				triangles.vertexStride = sizeof(float) * 3;
				// Describe index data (32-bit unsigned int)
				triangles.indexType = VK_INDEX_TYPE_UINT32;
				triangles.indexData.deviceAddress = indexAddress;
				triangles.maxVertex = (uint32_t)gp->m_totalNbVertices;

				// Identify the above data as containing opaque triangles.
				asGeom = {};
				asGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
				asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
				asGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
				asGeom.geometry.triangles = triangles;

				geomInfo = {};
				geomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
				geomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
				geomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
				
				if (gp->m_input.buildHint == decltype(gp->m_input)::BuildHint::Auto) {
					if (gp->m_input.allowUpdate)
						geomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
					else
						geomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
				}
				else if (gp->m_input.buildHint == decltype(gp->m_input)::BuildHint::PreferFastBuild) {
					geomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
				}
				else if (gp->m_input.buildHint == decltype(gp->m_input)::BuildHint::PreferFastTrace) {
					geomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
				}

				if (gp->m_input.allowUpdate)
					geomInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
				else
					geomInfo.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;

				geomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
				if (performUpdate)
					geomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;

				geomInfo.srcAccelerationStructure = {};
				geomInfo.dstAccelerationStructure = {};
				geomInfo.geometryCount = 1;
				geomInfo.pGeometries = &asGeom;

				geomInfo.scratchData = {};

				// The entire array will be used to build the BLAS.
				rangeInfo = {};
				rangeInfo.firstVertex = 0;
				rangeInfo.primitiveCount = maxPrimitiveCount;
				rangeInfo.primitiveOffset = 0;
				rangeInfo.transformOffset = 0;

				sizeInfo = {};
				sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
				GraphicsAPI::VK::vkGetAccelerationStructureBuildSizesKHR(pws->m_device.m_apiData.m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &geomInfo, &maxPrimitiveCount, &sizeInfo);
		};

		{
			size_t i = 0;
			for (auto gp : updatedGeometries) {
				GetPBInfo(gp, asGeomArr[i], geomInfoArr[i], rangeInfoArr[i], sizeInfoArr[i], true);
				++i;
			}
			for (auto gp : buildGeometries) {
				GetPBInfo(gp, asGeomArr[i], geomInfoArr[i], rangeInfoArr[i], sizeInfoArr[i], false);
				++i;
			}
		}

#endif

		auto AllocateBLAS = [&](
			Geometry *gp,
			uint64_t scratchBufferSize,
			uint64_t bufferSize)
		{
			// if allow update was not set for a BLAS, it's going to be compacted in following frames, so use placed resource to avoid fragmentation. 
			bool usePlaced = !gp->m_input.allowUpdate;

			// Create the BLAS scratch buffer and a buffer for BLAS
			if ((!gp->m_BLASScratchBuffer) || gp->m_BLASScratchBuffer->m_size < scratchBufferSize) {
				if (gp->m_BLASScratchBuffer)
					pws->DeferredRelease(std::move(gp->m_BLASScratchBuffer));

				if (usePlaced) {
					gp->m_BLASScratchBuffer = pws->m_sharedBufferForBLASScratchTemporal->Allocate(pws, scratchBufferSize, false);
				}
				else {
					gp->m_BLASScratchBuffer = pws->m_sharedBufferForBLASScratchPermanent->Allocate(pws, scratchBufferSize, false);
				}
			}
			if ((!gp->m_BLASBuffer) || gp->m_BLASBuffer->m_size < bufferSize) {
				if (gp->m_BLASBuffer)
					pws->DeferredRelease(std::move(gp->m_BLASBuffer));

				if (usePlaced) {
					gp->m_BLASBuffer = pws->m_sharedBufferForBLASTemporal->Allocate(pws, bufferSize, true);
				}
				else {
					gp->m_BLASBuffer = pws->m_sharedBufferForBLASPermanent->Allocate(pws, bufferSize, true);
				}
			}

#if defined(GRAPHICS_API_D3D12)
			if (!gp->m_input.allowUpdate) {
				if (!gp->m_BLASCompactionSizeBuffer) {
					gp->m_BLASCompactionSizeBuffer = pws->m_sharedBufferForCounter->Allocate(pws, sizeof(uint64_t), false);
				}
				if (!gp->m_BLASCompactionSizeBuffer_Readback) {
					gp->m_BLASCompactionSizeBuffer_Readback = pws->m_sharedBufferForReadback->Allocate(pws, sizeof(uint64_t), false);
				}
			}
#elif defined(GRAPHICS_API_VK)
			// Allocate a query pool for querying BLAS compacted size.
			if (!gp->m_input.allowUpdate) {
				gp->m_BLASCompactionSizeQueryPool = std::make_unique<GraphicsAPI::QueryPool_VK>();
				if (!gp->m_BLASCompactionSizeQueryPool->Create(&pws->m_device, { 0, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR , 1 })) {
					Log::Fatal(L"Failed to create a query pool for BLAS compaction size.");
					return (Status::ERROR_INTERNAL);
				}
				vkCmdResetQueryPool(cmdList->m_apiData.m_commandBuffer, gp->m_BLASCompactionSizeQueryPool->m_apiData.m_queryPool, 0, 1);
			}
#endif

			return Status::OK;
		};

#if defined(GRAPHICS_API_D3D12)
		{
			size_t i = 0;
			for (auto&& gp : updatedGeometries) {
				auto& asPBInfo(asPreBuildInfo[i]);
				RETURN_IF_STATUS_FAILED(AllocateBLAS(gp, asPBInfo.ScratchDataSizeInBytes, asPBInfo.ResultDataMaxSizeInBytes));
				++i;
			}
			for (auto&& gp : buildGeometries) {
				auto& asPBInfo(asPreBuildInfo[i]);
				RETURN_IF_STATUS_FAILED(AllocateBLAS(gp, asPBInfo.ScratchDataSizeInBytes, asPBInfo.ResultDataMaxSizeInBytes));
				++i;
			}
		}
#elif defined(GRAPHICS_API_VK)
		{
			size_t i = 0;
			for (auto&& gp : updatedGeometries) {
				auto& sizeInfo(sizeInfoArr[i]);
				RETURN_IF_STATUS_FAILED(AllocateBLAS(gp, std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize), sizeInfo.accelerationStructureSize));
				++i;
			}
			for (auto&& gp : buildGeometries) {
				auto& sizeInfo(sizeInfoArr[i]);
				RETURN_IF_STATUS_FAILED(AllocateBLAS(gp, std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize), sizeInfo.accelerationStructureSize));
				++i;
			}
		}
#endif

		// build BLAS
#if defined(GRAPHICS_API_D3D12)
		auto BuildBLAS = [&](
			Geometry *gp,
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& asInput, bool update) {
				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
				buildDesc.Inputs = asInput;

				if (update)
					buildDesc.SourceAccelerationStructureData = gp->m_BLASBuffer->GetGpuPtr();
				buildDesc.ScratchAccelerationStructureData = gp->m_BLASScratchBuffer->GetGpuPtr();
				buildDesc.DestAccelerationStructureData = gp->m_BLASBuffer->GetGpuPtr();

				if (!gp->m_input.allowUpdate) {
					D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC pbInfo = {};
					pbInfo.DestBuffer = gp->m_BLASCompactionSizeBuffer->GetGpuPtr();
					pbInfo.InfoType = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE;

					cmdList->m_apiData.m_commandList->BuildRaytracingAccelerationStructure(&buildDesc, 1, &pbInfo);
				}
				else {
					cmdList->m_apiData.m_commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
				}

		};

		{
			size_t i = 0;
			for (auto&& gp : updatedGeometries) {
				BuildBLAS(gp, asInputs[i], true);
				++i;
			}
			for (auto&& gp : buildGeometries) {
				BuildBLAS(gp, asInputs[i], false);
				++i;
			}
		}
#elif defined(GRAPHICS_API_VK)
		auto BuildBLAS = [&](
			BVHTask::Geometry *gp,
			VkAccelerationStructureGeometryKHR& asGeom,
			VkAccelerationStructureBuildGeometryInfoKHR& geomInfo,
			VkAccelerationStructureBuildRangeInfoKHR& rangeInfo,
			bool performUpdate) {

				geomInfo.dstAccelerationStructure = gp->m_BLASBuffer->m_uav->m_apiData.m_accelerationStructure;
				if (performUpdate)
					geomInfo.srcAccelerationStructure = gp->m_BLASBuffer->m_uav->m_apiData.m_accelerationStructure;
				else
					geomInfo.srcAccelerationStructure = {};

				geomInfo.geometryCount = 1;
				geomInfo.pGeometries = &asGeom;

				geomInfo.scratchData.hostAddress = nullptr;

				geomInfo.scratchData.deviceAddress = gp->m_BLASScratchBuffer->GetGpuPtr();

				VkAccelerationStructureBuildRangeInfoKHR* rangeArr[1] = { &rangeInfo };

				VK::vkCmdBuildAccelerationStructuresKHR(
					cmdList->m_apiData.m_commandBuffer,
					1,
					&geomInfo,
					rangeArr);
		};

		{
			size_t i = 0;
			for (auto gp : updatedGeometries) {
				BuildBLAS(gp, asGeomArr[i], geomInfoArr[i], rangeInfoArr[i], true);
				++i;
			}
			for (auto gp : buildGeometries) {
				BuildBLAS(gp, asGeomArr[i], geomInfoArr[i], rangeInfoArr[i], false);
				++i;
			}
		}
#endif

		// set uav barrier
		// After here, ASs in VK are moved to VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT state.
		{
			for (auto&& gp : updatedGeometries) {
				gp->m_BLASBuffer->RegisterBarrier();
			}
			for (auto&& gp : buildGeometries) {
				gp->m_BLASBuffer->RegisterBarrier();
			}
			pws->m_sharedBufferForBLASTemporal->UAVBarrier(cmdList);
			pws->m_sharedBufferForBLASPermanent->UAVBarrier(cmdList);
		}

#if defined(GRAPHICS_API_D3D12)
		// copy compacted size buffer to readback
		if (buildGeometries.size() > 0) {
			std::vector<SharedBuffer::BufferEntry*>		srcArr;
			std::vector<SharedBuffer::BufferEntry*>		dstArr;

			for (auto&& gp : buildGeometries) {
				if (gp->m_input.allowUpdate)
					continue;

				auto* src(gp->m_BLASCompactionSizeBuffer.get());
				auto* dst(gp->m_BLASCompactionSizeBuffer_Readback.get());
				if (dst == nullptr || src == nullptr) {
					Log::Fatal(L"Failed to set a copy command for readback.");
					return (Status::ERROR_INTERNAL);
				}
				srcArr.push_back(src);
				dstArr.push_back(dst);
			}

			if (srcArr.size() > 0) {
				for (auto&& s : srcArr)
					s->RegisterBarrier();
				if (pws->m_sharedBufferForCounter->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::CopySource) != Status::OK) {
					Log::Fatal(L"Failed to set transition barrier.");
					return Status::ERROR_INTERNAL;
				}

				// copy compaced size to readback
				for (size_t i = 0; i < srcArr.size(); ++i) {
					cmdList->CopyBufferRegion(
						dstArr[i]->m_block->m_buffer.get(), dstArr[i]->m_offset,
						srcArr[i]->m_block->m_buffer.get(), srcArr[i]->m_offset, sizeof(uint64_t));
				}

				for (auto&& s : srcArr)
					s->RegisterBarrier();
				if (pws->m_sharedBufferForCounter->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::UnorderedAccess) != Status::OK) {
					Log::Fatal(L"Failed to set transition barrier.");
					return Status::ERROR_INTERNAL;
				}

				// CopyDest -> CopyDest.
				// This is hacky but D3D12 doesn't need any barrier for host read. Nothing will happen in D3D12.
				// VK need some pipeline barrier to read data from host side after copy OP.
				// (Here is D3D12 only code path so skip this.)
				// for (auto&& d : dstArr)
				// 	d->RegisterTransition();
				// if (pws->m_sharedBufferForReadback->TransitionBarrier(cmdList, GraphicsAPI::ResourceState::State::CopyDest) != Status::OK) {
				// 	Log::Fatal(L"Failed to set transition barrier.");
				// 	return Status::ERROR_INTERNAL;
				// }
			}
		}
#elif defined(GRAPHICS_API_VK)
		// set query for compacted size.
		for (auto&& gp : buildGeometries) {
			if (gp->m_input.allowUpdate)
				continue;

			VK::vkCmdWriteAccelerationStructuresPropertiesKHR(
				cmdList->m_apiData.m_commandBuffer,
				1,
				& gp->m_BLASBuffer->m_uav->m_apiData.m_accelerationStructure,
				VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
				gp->m_BLASCompactionSizeQueryPool->m_apiData.m_queryPool,
				0);
		}
#endif

		// adding to waiting list for readback.
		{
			uint64_t currentFenceValue = pws->GetCurrentTaskIndex();
			for (auto&& gp : buildGeometries) {
				if (gp->m_input.allowUpdate)
					continue;

				m_container.m_waitingForBVHCompactionGeometries.push_back({ currentFenceValue, gp->ToHandle() });
			}
		}

		// Deferred release BLAS scratch buffer and compacted size buffer for static objects.
		// Deferred release index_vertex buffer for static objects.
		{
			for (auto&& gp : buildGeometries) {
				pws->DeferredRelease(std::move(gp->m_edgeTableBuffer));
				if (gp->m_input.allowUpdate)
					continue;
				pws->DeferredRelease(std::move(gp->m_BLASScratchBuffer));
#if defined(GRAPHICS_API_D3D12)
				pws->DeferredRelease(std::move(gp->m_BLASCompactionSizeBuffer));
#endif
				// Light transfer requires index_vertexbuffer to compute the geometric normal.
				if (!gp->m_input.allowLightTransferTarget)
					pws->DeferredRelease(std::move(gp->m_index_vertexBuffer));
			}
		}

		return Status::OK;
	}

	Status Scene::BuildTLASCommands(TaskWorkingSet* tws, GraphicsAPI::CommandList* cmdList)
	{
#if defined(GRAPHICS_API_VK)
		namespace VK = KickstartRT_NativeLayer::GraphicsAPI::VK;
#endif

		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);
		GraphicsAPI::Utils::ScopedEventObject sce(cmdList, { 0, 128, 0 }, DebugName("Build TLAS"));

		uint32_t nbInstanceParticipated = 0;
#if defined(GRAPHICS_API_D3D12)
		std::vector<D3D12_RAYTRACING_INSTANCE_DESC> iDescs(m_container.m_instances.size());
#elif defined(GRAPHICS_API_VK)
		std::vector<VkAccelerationStructureInstanceKHR> iDescs(m_container.m_instances.size());
#endif

		{
			// Update tlas instance list with valid instances. SDK try to keep the order of the list as much as possible so that we can minimize the desc copy OP.
			if (m_container.m_instances.size() > 0) {
				// Fill instance desc and upload it to a GPU visible buffer 

				for (auto&& itr : m_container.m_instances) {
					if (! itr.second->m_input.participatingInTLAS) {
						continue;
					}

					auto gp = itr.second->m_geometry;

					if (gp == nullptr)
						Log::Fatal(L"Invalid geometry reference held by an instance found.");

					if (!gp->m_BLASBuffer) {
						//Log::Info(L"Null BLAS is detected.. will be created soon..");
						continue;
					}

					// A valid instance and visible, but not on the list.
					if (!itr.second->m_TLASInstanceListItr.has_value()) {
						// Put the instance at the last of instance list for TLAS so that long-living instances should be stay the same place of the list longer.
						m_container.m_TLASInstanceList.push_back(itr.second->ToHandle());
						itr.second->m_TLASInstanceListItr = --m_container.m_TLASInstanceList.end();
					}
				}

#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
				{
					// Update indirection table here.
					std::vector<uint32_t> indirectionTable(m_container.m_TLASInstanceList.size() * 4);
					std::map<SharedBuffer::BufferBlock*, uint32_t> sharedBlockEntriesMap;
					m_directLightingCacheIndirectionTableSharedBlockEntries.clear();

					// Build indirect table buffer.
					// [Zero UAV], [Null UAV], [BufferBlock UAVs...]
					uint32_t indirectionTableEntryIdx = 2;
					uint32_t insIdx = 0;
					for (auto&& itr : m_container.m_TLASInstanceList) {
						auto ip = Instance::ToPtr(itr);
						auto& gp(ip->m_geometry);

						// One instace occupies 4 DWORD. [UAVIndex for DLC index][Offset for DLC index], [UAVIndex for DLC][Offset for DLC]
						size_t idx = (size_t)insIdx++ * 4;

						auto WriteEntry = [&](std::unique_ptr<SharedBuffer::BufferEntry> &bufferEntry) {
							auto* bPtr = bufferEntry->m_block;
							auto tItr = sharedBlockEntriesMap.find(bPtr);
							uint32_t	tableEntryIdx;
							if (tItr == sharedBlockEntriesMap.end()) {
								tableEntryIdx = indirectionTableEntryIdx++;
								sharedBlockEntriesMap.insert({ bPtr, tableEntryIdx });
								m_directLightingCacheIndirectionTableSharedBlockEntries.push_back(bPtr);
							}
							else {
								tableEntryIdx = tItr->second;
							}

							indirectionTable[idx++] = tableEntryIdx; // Buffer Block UAV index for direct lighting cache Buffer.
							indirectionTable[idx++] = (uint32_t)(bufferEntry->m_offset / sizeof(uint32_t)); // Buffer Block offset (DWORD) for direct lighting cache Buffer.
						};

						if (gp->m_directTileMapping) {
							// DirectTileMapping doesn't have an index buffer for DLC.
							indirectionTable[idx++] = 0; // Buffer Block UAV index for direct lighting cache index Buffer. One is Zero UAV.
							indirectionTable[idx++] = 0; // Buffer Block offset (DWORD) for direct lighting cache index Buffer.
						}
						else {
							WriteEntry(gp->m_directLightingCacheIndices);
						}
						if (! ip->m_dynamicTileBuffer) {
							// Dynamic Tile buffer is not allocated yet.
							indirectionTable[idx++] = 1; // Buffer Block UAV for direct lighting cache Buffer. Two is null UAV.
							indirectionTable[idx++] = 0; // Buffer Block offset (DWORD) for direct lighting cache Buffer.
						}
						else {
							WriteEntry(ip->m_dynamicTileBuffer);
						}
					}

					// Upload indirection table.
					if (m_container.m_TLASInstanceList.size() > 0) {
						size_t requiredSize = sizeof(uint32_t) * 4 * m_container.m_TLASInstanceList.size();
						size_t allocationSize = requiredSize + sizeof(uint32_t) * 4 * 50;

						if ((!tws->m_directLightingCacheIndirectionTableUploadBuffer) || tws->m_directLightingCacheIndirectionTableUploadBuffer->m_sizeInBytes < requiredSize) {
							if (tws->m_directLightingCacheIndirectionTableUploadBuffer && tws->m_directLightingCacheIndirectionTableUploadBuffer->m_sizeInBytes > 0) {
								pws->DeferredRelease(std::move(tws->m_directLightingCacheIndirectionTableUploadBuffer));
							}

							tws->m_directLightingCacheIndirectionTableUploadBuffer = pws->CreateBufferResource(
								allocationSize / (sizeof(uint32_t) * 4), GraphicsAPI::Resource::Format::RGBA32Uint,
								GraphicsAPI::Resource::BindFlags::None, GraphicsAPI::Buffer::CpuAccess::Write,
								ResourceLogger::ResourceKind::e_Other);
							if (!tws->m_directLightingCacheIndirectionTableUploadBuffer) {
								Log::Fatal(L"Failed to allocate directLightingCacheIndirectionTableUploadBuffer: %" PRIu64, allocationSize);
								return (Status::ERROR_INTERNAL);
							}
							tws->m_directLightingCacheIndirectionTableUploadBuffer->SetName(DebugName(L"DLC table indirection - upload"));
						}

						if ((!m_directLightingCacheIndirectionTableBuffer) || m_directLightingCacheIndirectionTableBuffer->m_sizeInBytes < requiredSize) {
							if (m_directLightingCacheIndirectionTableBuffer && m_directLightingCacheIndirectionTableBuffer->m_sizeInBytes > 0) {
								pws->DeferredRelease(std::move(m_directLightingCacheIndirectionTableBuffer));
								pws->DeferredRelease(std::move(m_directLightingCacheIndirectionTableBufferUAV));
							}

							m_directLightingCacheIndirectionTableBuffer = pws->CreateBufferResource(
								allocationSize / (sizeof(uint32_t) * 4), GraphicsAPI::Resource::Format::RGBA32Uint,
								GraphicsAPI::Resource::BindFlags::UnorderedAccess, GraphicsAPI::Buffer::CpuAccess::None,
								ResourceLogger::ResourceKind::e_Other);
							if (!m_directLightingCacheIndirectionTableBuffer) {
								Log::Fatal(L"Failed to allocate a TileTable uploadbuffer %" PRIu64, allocationSize);
								return (Status::ERROR_INTERNAL);
							}
							m_directLightingCacheIndirectionTableBuffer->SetName(DebugName(L"DLC table indirection"));

							m_directLightingCacheIndirectionTableBufferUAV = std::make_unique<GraphicsAPI::UnorderedAccessView>();
							if (!m_directLightingCacheIndirectionTableBufferUAV->Init(&pws->m_device, m_directLightingCacheIndirectionTableBuffer.get())) {
								Log::Fatal(L"Failed to create UAV for direct lighting cache indirection table buffer %" PRIu64, allocationSize);
								return (Status::ERROR_INTERNAL);
							}
						}

						{
							void* ptr = tws->m_directLightingCacheIndirectionTableUploadBuffer->Map(&pws->m_device, GraphicsAPI::Buffer::MapType::WriteDiscard, 0, 0, 0);
							memcpy(ptr, indirectionTable.data(), requiredSize);
							tws->m_directLightingCacheIndirectionTableUploadBuffer->Unmap(&pws->m_device, 0, 0, requiredSize);
						}

						{
							//set transition from UAV to copy dest
							std::vector<GraphicsAPI::Resource*> rArr{ m_directLightingCacheIndirectionTableBuffer.get() };
							std::vector<GraphicsAPI::ResourceState::State> sArr{ GraphicsAPI::ResourceState::State::CopyDest };
							cmdList->ResourceTransitionBarrier(rArr.data(), rArr.size(), sArr.data());
						}
						{
							// copy to the device buffer.
							cmdList->CopyBufferRegion(
								m_directLightingCacheIndirectionTableBuffer.get(), 0,
								tws->m_directLightingCacheIndirectionTableUploadBuffer.get(), 0, requiredSize);
						}
						{
							//set transition from CopyDest to UAV
							std::vector<GraphicsAPI::Resource*> rArr{ m_directLightingCacheIndirectionTableBuffer.get() };
							std::vector<GraphicsAPI::ResourceState::State> sArr{ GraphicsAPI::ResourceState::State::UnorderedAccess };
							cmdList->ResourceTransitionBarrier(rArr.data(), rArr.size(), sArr.data());
						}
					}
				}
#endif
			}

			// Fill instance desc and upload it to a GPU visible buffer 
			for (auto&& itr : m_container.m_TLASInstanceList) {
				auto ip = Instance::ToPtr(itr);
				auto& gp(ip->m_geometry);
				
#if defined(GRAPHICS_API_D3D12)
				D3D12_RAYTRACING_INSTANCE_DESC& iDesc(iDescs[nbInstanceParticipated]);
				iDesc = {};
				ip->m_input.transform.CopyTo(&iDesc.Transform[0][0]);
				iDesc.InstanceID = nbInstanceParticipated;
				iDesc.InstanceContributionToHitGroupIndex = 0; // since we only use inline raytracing.
				iDesc.InstanceMask = uint8_t(ip->m_input.instanceInclusionMask);
				iDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE | D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
				iDesc.AccelerationStructure = gp->m_BLASBuffer->GetGpuPtr();
#elif defined(GRAPHICS_API_VK)
				VkAccelerationStructureInstanceKHR& iDesc(iDescs[nbInstanceParticipated]);
				iDesc = {};
				ip->m_input.transform.CopyTo(&iDesc.transform);
				iDesc.instanceCustomIndex = nbInstanceParticipated;
				iDesc.mask = uint8_t(ip->m_input.instanceInclusionMask);
				iDesc.instanceShaderBindingTableRecordOffset = 0;
				iDesc.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR;
				iDesc.accelerationStructureReference = gp->m_BLASBuffer->GetGpuPtr();
#endif
				++nbInstanceParticipated;
			}
		}

		if (m_enableInfoLog) {
			Log::Info(L"BuildTLASCommand() NbInstancesParticipated: %d", nbInstanceParticipated);
		}

		// shrink the array to cut unused region.
		iDescs.resize(nbInstanceParticipated);

		// upload TLAS desc.
		{
			size_t requiredUploadSize = sizeof(iDescs[0]) * iDescs.size();
			size_t allocationSize = requiredUploadSize + sizeof(iDescs[0]) * 50;

			if (tws->m_TLASUploadBuffer->m_sizeInBytes < requiredUploadSize) {
				if (tws->m_TLASUploadBuffer->m_sizeInBytes > 0)
					pws->DeferredRelease(std::move(tws->m_TLASUploadBuffer));

				tws->m_TLASUploadBuffer = pws->CreateBufferResource(
					(uint32_t)allocationSize, GraphicsAPI::Resource::Format::Unknown,
					GraphicsAPI::Resource::BindFlags::Constant | GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress | GraphicsAPI::Resource::BindFlags::AccelerationStructureBuildInput,
					GraphicsAPI::Buffer::CpuAccess::Write,
					ResourceLogger::ResourceKind::e_TLAS);
				if (!tws->m_TLASUploadBuffer) {
					Log::Fatal(L"Failed to allocate a TLAS upload buffer %" PRIu64, allocationSize);
					return (Status::ERROR_INTERNAL);
				}
				tws->m_TLASUploadBuffer->SetName(DebugName(L"TLAS upload"));
			}

			// copy instance info to the upload buffer.
			if (requiredUploadSize > 0) {
				void* ptr = tws->m_TLASUploadBuffer->Map(&pws->m_device, GraphicsAPI::Buffer::MapType::WriteDiscard, 0, 0, 0);
				if (ptr != nullptr) {
					memcpy(ptr, &iDescs[0], requiredUploadSize);
					tws->m_TLASUploadBuffer->Unmap(&pws->m_device, 0, 0, requiredUploadSize);
				}
				else {
					Log::Fatal(L"Failed to map TLAS upload buffer, device removal state is suspected.");
					return (Status::ERROR_INTERNAL);
				}
			}
		}

		// Allocate TLAS buffer and Scratch buffer then build TLAS
		{
			uint32_t	instanceCount = nbInstanceParticipated;
			uint64_t	scratchBufferSize = (uint64_t)-1;
			uint64_t	TLASBufferSize = (uint64_t)-1;
#if defined(GRAPHICS_API_D3D12)
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS ASInputs = {};
			ASInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
			ASInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			ASInputs.InstanceDescs = instanceCount > 0 ? tws->m_TLASUploadBuffer->GetGpuAddress() : 0;
			ASInputs.NumDescs = instanceCount;
			ASInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

			// Get the size requirements for the TLAS buffers
			{
				D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO ASPreBuildInfo = {};
				pws->m_device.m_apiData.m_device->GetRaytracingAccelerationStructurePrebuildInfo(&ASInputs, &ASPreBuildInfo);

				TLASBufferSize = GraphicsAPI::ALIGN((UINT64)D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ResultDataMaxSizeInBytes);
				scratchBufferSize = GraphicsAPI::ALIGN((UINT64)D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, ASPreBuildInfo.ScratchDataSizeInBytes);
			}

#elif defined(GRAPHICS_API_VK)
			VkAccelerationStructureGeometryInstancesDataKHR instances = {};
			instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
			instances.arrayOfPointers = false;
			instances.data.deviceAddress = instanceCount > 0 ? tws->m_TLASUploadBuffer->GetGpuAddress() : 0;

			// Identify the above data as containing opaque triangles.
			VkAccelerationStructureGeometryKHR asGeom = {};
			asGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
			asGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
			asGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
			asGeom.geometry.instances = instances;

			VkAccelerationStructureBuildGeometryInfoKHR geomInfo = {};
			geomInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
			geomInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
			geomInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
			geomInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			geomInfo.srcAccelerationStructure = {};
			geomInfo.dstAccelerationStructure = {};
			geomInfo.geometryCount = 1;
			geomInfo.pGeometries = &asGeom;
			geomInfo.scratchData = {};

			// Get the size requirements for the TLAS buffers
			{
				VkAccelerationStructureBuildSizesInfoKHR sizeInfo = {};
				sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
				VK::vkGetAccelerationStructureBuildSizesKHR(pws->m_device.m_apiData.m_device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &geomInfo, &instanceCount, &sizeInfo);

				TLASBufferSize = sizeInfo.accelerationStructureSize;
				scratchBufferSize = std::max(sizeInfo.buildScratchSize, sizeInfo.updateScratchSize);
			}
#endif

			// Allocate TLAS buffer and Scratch buffer
			if ((!m_TLASScratchBuffer) || m_TLASScratchBuffer->m_sizeInBytes < scratchBufferSize) {
				pws->DeferredRelease(std::move(m_TLASScratchBuffer));

				uint64_t allocationSize = scratchBufferSize + 256 * 4 * 16; // + 16KB
				m_TLASScratchBuffer = pws->CreateBufferResource(
					allocationSize, GraphicsAPI::Resource::Format::Unknown,
					GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::ShaderDeviceAddress,
					GraphicsAPI::Buffer::CpuAccess::None,
					ResourceLogger::ResourceKind::e_TLAS);
				if (!m_TLASScratchBuffer) {
					Log::Fatal(L"Failed to allocate a TLAS scratch buffer %" PRIu64, allocationSize);
					return (Status::ERROR_INTERNAL);
				}
				m_TLASScratchBuffer->SetName(DebugName(L"TLAS scratch"));
			}
			if ((!m_TLASBuffer) || m_TLASBuffer->m_sizeInBytes < TLASBufferSize) {
				pws->DeferredRelease(std::move(m_TLASBuffer));
				pws->DeferredRelease(std::move(m_TLASBufferSrv));

				uint64_t allocationSize = TLASBufferSize + 256 * 4 * 16; // + 16KB

				m_TLASBuffer = pws->CreateBufferResource(
					allocationSize, GraphicsAPI::Resource::Format::Unknown,
					GraphicsAPI::Resource::BindFlags::UnorderedAccess | GraphicsAPI::Resource::BindFlags::AccelerationStructure,
					GraphicsAPI::Buffer::CpuAccess::None,
					ResourceLogger::ResourceKind::e_TLAS);
				if (!m_TLASBuffer) {
					Log::Fatal(L"Failed to allocate a TLAS buffer %" PRIu64, allocationSize);
					return (Status::ERROR_INTERNAL);
				}
				m_TLASBuffer->SetName(DebugName(L"TLAS"));

				m_TLASBufferSrv = std::make_unique<GraphicsAPI::ShaderResourceView>();
				if (!m_TLASBufferSrv->Init(&pws->m_device, m_TLASBuffer.get())) {
					Log::Fatal(L"Failed to create SRV for a TLAS buffer %" PRIu64, allocationSize);
					return (Status::ERROR_INTERNAL);
				}
			}

			// Build TLAS
#if defined(GRAPHICS_API_D3D12)
			{
				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
				buildDesc.Inputs = ASInputs;
				buildDesc.ScratchAccelerationStructureData = m_TLASScratchBuffer->GetGpuAddress();
				buildDesc.DestAccelerationStructureData = m_TLASBuffer->GetGpuAddress();

				cmdList->m_apiData.m_commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
			}
#elif defined(GRAPHICS_API_VK)
			{
				// The all instances will be used to build the TLAS.
				VkAccelerationStructureBuildRangeInfoKHR rangeInfo = {};
				rangeInfo.firstVertex = 0;
				rangeInfo.primitiveCount = instanceCount;
				rangeInfo.primitiveOffset = 0;
				rangeInfo.transformOffset = 0;

				geomInfo.dstAccelerationStructure = m_TLASBufferSrv->m_apiData.m_accelerationStructure;
				geomInfo.scratchData.deviceAddress = m_TLASScratchBuffer->GetGpuAddress();

				VkAccelerationStructureBuildRangeInfoKHR* rangeArr[1] = { &rangeInfo };
				VK::vkCmdBuildAccelerationStructuresKHR(
					cmdList->m_apiData.m_commandBuffer,
					1,
					&geomInfo,
					rangeArr);
			}
#endif

			// set uav barrier to use it
			{
				std::vector<GraphicsAPI::Resource*> rArr{ m_TLASBuffer.get() };
				cmdList->ResourceUAVBarrier(rArr.data(), rArr.size());
			}
		}

		return Status::OK;
	}

	Status Scene::BuildDirectLightingCacheDescriptorTable(TaskWorkingSet* tws, GraphicsAPI::DescriptorTableLayout* srcLayout, GraphicsAPI::DescriptorTable* destDescTable, std::deque<Instance*>& retInstances)
	{
		PersistentWorkingSet* pws(tws->m_persistentWorkingSet);

#if KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE
		{
			size_t descTableSize = m_directLightingCacheIndirectionTableSharedBlockEntries.size() + 2; // zero view, null view, shared block...

			// the last one is unbound descTableLayout so we need to specify its size.
			if (!destDescTable->Allocate(&tws->m_CBVSRVUAVHeap, srcLayout, (uint32_t)descTableSize)) {
				Log::Fatal(L"Faild to allocate a portion of desc heap.");
				return Status::ERROR_INTERNAL;
			}

			{
				// First entry is for TLAS
				if (!destDescTable->SetSrv(&pws->m_device, 0, 0, m_TLASBufferSrv.get())) {
					Log::Fatal(L"Failed to set Srv");
					return Status::ERROR_INTERNAL;
				}

				// Second one is for direct lighting cache indirection table.
				if (!destDescTable->SetUav(&pws->m_device, 1, 0, m_directLightingCacheIndirectionTableBufferUAV.get())) {
					Log::Fatal(L"Failed to set Uav");
					return Status::ERROR_INTERNAL;
				}

				// The rest is for indirection table.
				// Copy CPU -> GPU visible.
				// 1st entry is reserved for zero view.
				uint32_t tableIndex = 0;
				if (!destDescTable->SetUav(&pws->m_device, 2, tableIndex++, pws->m_zeroBufferUAV.get())) {
					Log::Fatal(L"Failed to set UAV");
					return Status::ERROR_INTERNAL;
				}
				// 2nd entry is reserved for null view.
				if (!destDescTable->SetUav(&pws->m_device, 2, tableIndex++, pws->m_nullBufferUAV.get())) {
					Log::Fatal(L"Failed to set UAV");
					return Status::ERROR_INTERNAL;
				}
				for (auto&& itr : m_directLightingCacheIndirectionTableSharedBlockEntries) {
					if (!destDescTable->SetUav(&pws->m_device, 2, tableIndex++, itr->m_uav.get())) {
						Log::Fatal(L"Failed to set UAV");
						return Status::ERROR_INTERNAL;
					}
				}
			}

			// return valid isntance list.
			{
				std::deque<Instance*>		validIp(m_container.m_TLASInstanceList.size(), nullptr);
				uint32_t instanceIdx = 0;
				for (auto&& itr : m_container.m_TLASInstanceList) {
					validIp[instanceIdx++] = Instance::ToPtr(itr);
				}
				std::swap(validIp, retInstances);
			}
		}

#else
		{
			// Check if the CPU desc heap has sufficient buffer size, then allocate it. 
			uint32_t requestedSize = (uint32_t)m_container.m_TLASInstanceList.size() * 2;

			if (requestedSize > m_cpuLightCacheDescs.m_allocatedDescTableSize) {
				uint32_t allocationSize = requestedSize + 128;

				// Create a new CPU desc layout for a bufferUAV array.
				{
					pws->DeferredRelease(std::move(m_cpuLightCacheDescs.m_descLayout));

					m_cpuLightCacheDescs.m_descLayout = std::make_unique<GraphicsAPI::DescriptorTableLayout>();
					m_cpuLightCacheDescs.m_descLayout->AddRange(GraphicsAPI::DescriptorHeap::Type::TypedBufferUav, 0, allocationSize, 0); // b0, cb
					m_cpuLightCacheDescs.m_descLayout->SetAPIData(&pws->m_device);
				}

				// Create a new CPU desc heap.
				{
					pws->DeferredRelease(std::move(m_cpuLightCacheDescs.m_descHeap));

					using DH = GraphicsAPI::DescriptorHeap;
					DH::Desc	desc = {};
					desc.m_descCount[DH::value(DH::Type::TypedBufferUav)] = allocationSize;
					desc.m_totalDescCount = allocationSize;
					m_cpuLightCacheDescs.m_descHeap = std::make_unique<GraphicsAPI::DescriptorHeap>();

					if (!m_cpuLightCacheDescs.m_descHeap->Create(&pws->m_device, desc, false)) {
						Log::Fatal(L"Failed to create a CPU descriptor heap");
						return Status::ERROR_INTERNAL;
					}
					m_cpuLightCacheDescs.m_descHeap->SetName(DebugName(L"CPU LightCacheDescHeap"));
				}

				m_cpuLightCacheDescs.m_descTable = std::make_unique<GraphicsAPI::DescriptorTable>();
				if (!m_cpuLightCacheDescs.m_descTable->Allocate(m_cpuLightCacheDescs.m_descHeap.get(), m_cpuLightCacheDescs.m_descLayout.get())) {
					Log::Fatal(L"Faild to allocate a desc table from CPU LightCacheDescHeap.");
					return Status::ERROR_INTERNAL;
				}
				m_cpuLightCacheDescs.m_allocatedDescTableSize = allocationSize;

				// Allocate instance handle list for the new table.
				m_cpuLightCacheDescs.m_instanceList.clear();
				m_cpuLightCacheDescs.m_instanceList.resize(allocationSize, InstanceHandle(-1));
			}
		}

		auto UpdateCPUDescs = [pws, this](Instance* ip, Geometry* gp, bool &isUpdated) -> Status
		{
			isUpdated = false;

			if (!ip->m_cpuDescTableAllocation) {
				ip->m_cpuDescTableAllocation = pws->m_UAVCPUDescHeap2->Allocate(&pws->m_device);
				if (!ip->m_cpuDescTableAllocation) {
					Log::Fatal(L"Faild to allocate desc heap.");
					return Status::ERROR_INTERNAL;
				}
				ip->m_needToUpdateUAV = true; // to avoid using an obsoleted desc table entry by accident.
			}

			auto& cpuDescTable(ip->m_cpuDescTableAllocation->m_table);

			if (ip->m_needToUpdateUAV) {
				// need to update UAV.

				// directLightingCacheIndex, directLightingCacheBuffer
				{
					// this will be empty in direct mapping mode.
					if (!gp->m_directLightingCacheIndices) {
						// This is not null but zero especially in VK, sinc all bits need to be zero to detect direct mapping mode in shader.
						cpuDescTable->SetUav(&pws->m_device, 0, 0, pws->m_zeroBufferUAV.get());	// l1: 3+
					}
					else {
						cpuDescTable->SetUav(&pws->m_device, 0, 0, gp->m_directLightingCacheIndices->m_uav.get());	// l1: 3+
					}

					// m_DynamicTileBuffer will be allocated later after calculating tile cache size, so check if it's null.
					if (!ip->m_dynamicTileBuffer) {
						cpuDescTable->SetUav(&pws->m_device, 0, 1, pws->m_nullBufferUAV.get());	// l1: 4+
					}
					else {
						cpuDescTable->SetUav(&pws->m_device, 0, 1, ip->m_dynamicTileBuffer->m_uav.get());	// l1: 4+
					}
				}

				ip->m_needToUpdateUAV = false;
				isUpdated = true;
			}

			return Status::OK;
		};

		// update CPU desc table array
		std::deque<Instance*>		validIp(m_container.m_TLASInstanceList.size(), nullptr);
		uint32_t instanceIdx = 0;

		for (auto&& itr : m_container.m_TLASInstanceList) {
			auto* ip = Instance::ToPtr(itr);
			auto* gp = ip->m_geometry;

			bool isUpdated = false;
			if (UpdateCPUDescs(ip, gp, isUpdated) != Status::OK) {
				Log::Fatal(L"Failed to update CPU desc heap for a instance.");
				return Status::ERROR_INTERNAL;
			}

			if (isUpdated || m_cpuLightCacheDescs.m_instanceList[instanceIdx] != itr) {
				// copy CPU -> CPU desc array
				m_cpuLightCacheDescs.m_descTable->Copy(&pws->m_device, 0, instanceIdx * 2, ip->m_cpuDescTableAllocation->m_table);
				m_cpuLightCacheDescs.m_instanceList[instanceIdx] = itr;
			}
			validIp[instanceIdx] = ip;

			++instanceIdx;
		}

		uint32_t descTableSize = (uint32_t)validIp.size() * 2;

		if (m_enableInfoLog) {
			Log::Info(L"BuildDirectLightingCacheDescriptorTable() : DesctableSize: %d", descTableSize);
		}

		// the last one is unbound descTableLayout so we need to specify its size.
		if (!destDescTable->Allocate(tws->m_CBVSRVUAVHeap.get(), srcLayout, descTableSize)) {
			Log::Fatal(L"Faild to allocate a portion of desc heap.");
			return Status::ERROR_INTERNAL;
		}

		{
			// first entry is for TLAS
			if (!destDescTable->SetSrv(&pws->m_device, 0, 0, m_TLASBufferSrv.get())) {
				Log::Fatal(L"Failed to set Srv");
				return Status::ERROR_INTERNAL;
			}

			// second one is for tile Table. (obsoleted.)
			if (!destDescTable->SetUav(&pws->m_device, 1, 0, pws->m_zeroBufferUAV.get())) {
				Log::Fatal(L"Failed to set Uav");
				return Status::ERROR_INTERNAL;
			}

			// the rest is for direct lighting cache. it's an array of (directLightingCacheIndex, directLightingCacheBuffer)
			// Copy CPU -> GPU visible.
			if (descTableSize > 0) {
				if (!destDescTable->Copy(&pws->m_device, 2, 0, m_cpuLightCacheDescs.m_descTable.get(), descTableSize)) {
					Log::Fatal(L"Failed to Copy descriptors");
					return Status::ERROR_INTERNAL;
				}
			}
		}

		// return valid isntance list.
		std::swap(validIp, retInstances);
#endif

		return Status::OK;
	}

	Status Scene::ReleaseDeviceResourcesImmediately(TaskTracker* taskTracker, PersistentWorkingSet* pws, UpdateFromExecuteContext* updateFromExc)
	{
		// Hold scene container's mutex until exit from this function.
		std::scoped_lock containerMutex(m_container.m_mutex);

		// Hold pws's mutex until exit from this function.
		std::scoped_lock pwsMutex(pws->m_mutex);

		Status sts;

		if (pws->HasTaskIndices()) {
			// Persistent working set holds a valid task index at the begging of the build gpu task, which shouldn't be happened.
			// Strongly susptected the last BuildGPUTask has been failed.
			Log::Fatal(L"Failed to start ReleaseDeviceResourcesImmediately since the last build gpu task has been failed.");
			return Status::ERROR_INTERNAL;
		}

		// set the current task index to pws to do deferred release.
		pws->SetTaskIndices(taskTracker->CurrentTaskIndex(), taskTracker->FinishedTaskIndex());

		sts = UpdateDenoisingContext(pws, updateFromExc);
		if (sts != Status::OK)
			return sts;
		
		bool isSceneChanged = false;
		sts = UpdateScenegraphFromExecuteContext(pws, updateFromExc, isSceneChanged);
		m_TLASisDrity |= isSceneChanged;
		if (sts != Status::OK)
			return sts;

		pws->ClearTaskIndices();

		// Release expired device objects.
		pws->ReleaseDeferredReleasedDeviceObjects(taskTracker->FinishedTaskIndex());

		return Status::OK;
	};
};

