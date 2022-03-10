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

#include "Platform.h"
#include <ExecuteContext.h>
#include <PersistentWorkingSet.h>

namespace KickstartRT_NativeLayer
{
	inline void GetNormalUnpackConstants(
		RenderTask::NormalType normalType,
		uint32_t& outNormalType,
		float& outNormalNormalizationScale,
		float& outNormalNormalizationBias,
		Math::Float_4& outNormalChMask1,
		Math::Float_4& outNormalChMask2) {

		switch (normalType) {
		case RenderTask::NormalType::RGB_Vector:
		case RenderTask::NormalType::RG_Octahedron:
		case RenderTask::NormalType::BA_Octahedron:
			outNormalNormalizationScale = 1.f;
			outNormalNormalizationBias = 0.f;
			break;
		case RenderTask::NormalType::RGB_NormalizedVector:
		case RenderTask::NormalType::RG_NormalizedOctahedron:
		case RenderTask::NormalType::BA_NormalizedOctahedron:
			outNormalNormalizationScale = 2.f;
			outNormalNormalizationBias = -1.f;
			break;
		default:
			Log::Error(L"Invalid normal type detected.");
			assert(false);
			break;
		}
		switch (normalType) {
		case RenderTask::NormalType::RGB_Vector:
		case RenderTask::NormalType::RGB_NormalizedVector:
			outNormalType = 0; // vector and normalized vector.
			outNormalChMask1 = { 0.f, 0.f, 0.f, 0.f };	// unused
			outNormalChMask2 = { 0.f, 0.f, 0.f, 0.f };	// unused
			break;
		case RenderTask::NormalType::RG_Octahedron:
		case RenderTask::NormalType::RG_NormalizedOctahedron:
			outNormalType = 1; // octahedron
			outNormalChMask1 = { 1.f, 0.f, 0.f, 0.f };
			outNormalChMask2 = { 0.f, 1.f, 0.f, 0.f };
			break;
		case RenderTask::NormalType::BA_Octahedron:
		case RenderTask::NormalType::BA_NormalizedOctahedron:
			outNormalType = 1; // octahedron
			outNormalChMask1 = { 0.f, 0.f, 1.f, 0.f };
			outNormalChMask2 = { 0.f, 0.f, 0.f, 1.f };
			break;
		default:
			Log::Error(L"Invalid normal type detected.");
			assert(false);
			break;
		}
	}

	struct ResourceRef {
		ResourceRef(PersistentWorkingSet* p, const RenderTask::CombinedAccessTex& _mixed, GraphicsAPI::ResourceState::State _initialState) :
			pws(p),
			mixedRef(_mixed),
			initialState(_initialState)
		{
		}

		~ResourceRef() {
#if defined(GRAPHICS_API_D3D12)
			if (resource)
				resource->m_apiData.m_resource = nullptr;
#elif defined(GRAPHICS_API_VK)
			if (resource) {
				resource->m_apiData.m_device = nullptr;
				resource->m_apiData.m_image = nullptr;
			}
#endif
		}

		bool Valid() const {
#if defined(GRAPHICS_API_D3D12)
			return mixedRef.resource != nullptr;
#elif defined(GRAPHICS_API_VK)
			return mixedRef.image != nullptr;
#endif
		}

		GraphicsAPI::Texture* GetResource() {
			if (!resource.get()) {
#if defined(GRAPHICS_API_D3D12)
				GraphicsAPI::Resource::ApiData apiData;
				apiData.m_resource = mixedRef.resource;
				resource = std::make_unique<GraphicsAPI::Texture>();
				resource->InitFromApiData(apiData, initialState);
#elif defined(GRAPHICS_API_VK)
				resource = std::make_unique<GraphicsAPI::Texture>();
				resource->InitFromApiData(
					pws->m_device.m_apiData.m_device,
					mixedRef.image,
					mixedRef.imageViewType,
					mixedRef.format,
					1,
					1,
					initialState);
#endif
			}
			return resource.get();
		}

		PersistentWorkingSet* pws;
		const RenderTask::CombinedAccessTex mixedRef;
		GraphicsAPI::ResourceState::State initialState;
		std::unique_ptr<GraphicsAPI::Texture> resource;
	};

	struct RenderPass_ResourceStateTransition {

		Status RequestState(GraphicsAPI::Resource* resource, GraphicsAPI::ResourceState::State state) {

			const GraphicsAPI::Resource::ApiResourceID apiID = resource->GetApiResourceID();

			auto it = transResGuard.find(apiID);
			if (it == transResGuard.end()) {
				transRes.push_back(resource);
				transState.push_back(state);
				transResGuard[apiID] = std::make_pair(state, (uint32_t)transRes.size() - 1u);
			}
			else {
				const uint32_t resourceIndex = it->second.second;
				if (transState[resourceIndex] != state) {
					Log::Warning(L"Resource is already scheduled for state transition to %d, now expects transition to %d", it->second.second, state);
					return Status::ERROR_INTERNAL;
				}
			}
			return Status::OK;
		}

		Status Flush(GraphicsAPI::CommandList* cmdList) {
			bool res = cmdList->ResourceTransitionBarrier(transRes.data(), (size_t)transRes.size(), transState.data());
			transRes.clear();
			transState.clear();
			transResGuard.clear();
			return res ? Status::OK : Status::ERROR_INTERNAL;
		}

	private:
		std::vector<GraphicsAPI::Resource*>						transRes;
		std::vector<GraphicsAPI::ResourceState::State>			transState;
		// This is meant to catch any aliased resources to avid doing double (and incorrect) state transitions.
		// For instance when the same resource is used for normals and roughness.
		std::map<GraphicsAPI::Resource::ApiResourceID, std::pair<GraphicsAPI::ResourceState::State, uint32_t>>	transResGuard;
	};

	static const RenderTask::CombinedAccessTex AsCombined(const RenderTask::ShaderResourceTex& tex) {
#if defined(GRAPHICS_API_D3D12)
		return { tex.srvDesc,  {} ,tex.resource };
#else
		return { tex.image,   tex.imageViewType,  tex.format,  tex.aspectMask,  tex.baseMipLevel,  tex.mipCount,  tex.baseArrayLayer, tex.layerCount };
#endif
	}

	static const RenderTask::CombinedAccessTex AsCombined(const RenderTask::UnorderedAccessTex& tex) {
#if defined(GRAPHICS_API_D3D12)
		return { {}, tex.uavDesc, tex.resource };
#else
		return { tex.image,   tex.imageViewType,  tex.format,  tex.aspectMask,  tex.baseMipLevel,  1,  tex.baseArrayLayer, tex.layerCount };
#endif
	}

	struct RenderPass_ResourceRegistry {

		void TrackResource(const RenderTask::ShaderResourceTex& tex, GraphicsAPI::ResourceState::State initialState) {
			TrackResource(AsCombined(tex), initialState);
		}

		void TrackResource(const RenderTask::UnorderedAccessTex& tex, GraphicsAPI::ResourceState::State initialState) {
			TrackResource(AsCombined(tex), initialState);
		}

		void TrackResource(const RenderTask::CombinedAccessTex& tex, GraphicsAPI::ResourceState::State initialState) {
#if defined(GRAPHICS_API_D3D12)
			if (tex.resource == nullptr)
				return;
			GraphicsAPI::Resource::ApiResourceID id = reinterpret_cast<GraphicsAPI::Resource::ApiResourceID>(tex.resource);

#else
			if (tex.image == nullptr)
				return;
			GraphicsAPI::Resource::ApiResourceID id = reinterpret_cast<GraphicsAPI::Resource::ApiResourceID>(tex.image);
#endif
			auto it = m_resources.find(id);
			if (it == m_resources.end()) {
				m_resources[id] = std::make_unique<ResourceRef>(pws, tex, initialState);
		}
		}

		GraphicsAPI::Resource* GetResource(const RenderTask::ShaderResourceTex& tex) {
			return GetResource(AsCombined(tex));
		}

		GraphicsAPI::Resource* GetResource(const RenderTask::UnorderedAccessTex& tex) {
			return GetResource(AsCombined(tex));
		}

		GraphicsAPI::Resource* GetResource(const RenderTask::CombinedAccessTex& tex) {
#if defined(GRAPHICS_API_D3D12)
			GraphicsAPI::Resource::ApiResourceID id = reinterpret_cast<GraphicsAPI::Resource::ApiResourceID>(tex.resource);
#else
			GraphicsAPI::Resource::ApiResourceID id = reinterpret_cast<GraphicsAPI::Resource::ApiResourceID>(tex.image);
#endif
			auto it = m_resources.find(id);
			if (it == m_resources.end())
				return nullptr;

			return it->second->GetResource();
		}

		GraphicsAPI::Texture* GetTexture(const RenderTask::ShaderResourceTex& tex) {
			return GetTexture(AsCombined(tex));
		}

		GraphicsAPI::Texture* GetTexture(const RenderTask::UnorderedAccessTex& tex) {
			return GetTexture(AsCombined(tex));
		}

		GraphicsAPI::Texture* GetTexture(const RenderTask::CombinedAccessTex& tex) {
#if defined(GRAPHICS_API_D3D12)
			GraphicsAPI::Resource::ApiResourceID id = reinterpret_cast<GraphicsAPI::Resource::ApiResourceID>(tex.resource);
#else
			GraphicsAPI::Resource::ApiResourceID id = reinterpret_cast<GraphicsAPI::Resource::ApiResourceID>(tex.image);
#endif
			auto it = m_resources.find(id);
			if (it == m_resources.end()) {
				return nullptr;
			}
			GraphicsAPI::Resource* resource = it->second->GetResource();
			assert(resource->m_type == GraphicsAPI::Resource::Type::Texture2D);
			return (GraphicsAPI::Texture*)resource;
		}

		std::unique_ptr<GraphicsAPI::ShaderResourceView> GetSRV(const RenderTask::CombinedAccessTex& tex, RenderPass_ResourceStateTransition& stateTransitions, GraphicsAPI::ResourceState::State state) {
			GraphicsAPI::Resource* res = GetResource(tex);
			if (!res)
				return nullptr;

			stateTransitions.RequestState(res, state);

			std::unique_ptr<GraphicsAPI::ShaderResourceView> srv = std::make_unique<GraphicsAPI::ShaderResourceView>();
#if defined(GRAPHICS_API_D3D12)
			srv->InitFromApiData(tex.resource, &tex.srvDesc);
#else
			if (!srv->InitFromApiData(&pws->m_device, tex.image, tex.imageViewType, tex.format,
				tex.aspectMask, tex.baseMipLevel, tex.mipCount, tex.baseArrayLayer, tex.layerCount)) {
				Log::Fatal(L"Failed to create a SRV for texture");
			}
#endif
			return std::move(srv);
		};

		std::unique_ptr<GraphicsAPI::UnorderedAccessView> GetUAV(const RenderTask::CombinedAccessTex& tex, RenderPass_ResourceStateTransition& stateTransitions, GraphicsAPI::ResourceState::State state) {
			GraphicsAPI::Resource* res = GetResource(tex);
			if (!res)
				return nullptr;

			stateTransitions.RequestState(res, state);

			std::unique_ptr<GraphicsAPI::UnorderedAccessView> uav = std::make_unique<GraphicsAPI::UnorderedAccessView>();
#if defined(GRAPHICS_API_D3D12)
			uav->InitFromApiData(tex.resource, &tex.uavDesc);
#else
			if (!uav->InitFromApiData(&pws->m_device, tex.image, tex.imageViewType, tex.format,
				tex.aspectMask, tex.baseMipLevel, tex.baseArrayLayer, tex.layerCount)) {
				Log::Fatal(L"Failed to create a SRV for texture");
			}
#endif
			return std::move(uav);
		};

		std::unique_ptr<GraphicsAPI::UnorderedAccessView> GetUAV(const RenderTask::UnorderedAccessTex& tex, RenderPass_ResourceStateTransition& stateTransitions, GraphicsAPI::ResourceState::State state) {
			return std::move(GetUAV(AsCombined(tex), stateTransitions, state));
		};

		std::unique_ptr<GraphicsAPI::ShaderResourceView> GetSRV(const RenderTask::ShaderResourceTex& tex, RenderPass_ResourceStateTransition& stateTransitions, GraphicsAPI::ResourceState::State state) {
			return std::move(GetSRV(AsCombined(tex), stateTransitions, state));
		};

		std::unique_ptr<GraphicsAPI::ShaderResourceView> GetSRV(const RenderTask::CombinedAccessTex& tex) {
			std::unique_ptr<GraphicsAPI::ShaderResourceView> srv = std::make_unique<GraphicsAPI::ShaderResourceView>();
#if defined(GRAPHICS_API_D3D12)
			srv->InitFromApiData(tex.resource, &tex.srvDesc);
#else
			if (!srv->InitFromApiData(&pws->m_device, tex.image, tex.imageViewType, tex.format,
				tex.aspectMask, tex.baseMipLevel, tex.mipCount, tex.baseArrayLayer, tex.layerCount)) {
				Log::Fatal(L"Failed to create a SRV for texture");
			}
#endif
			return std::move(srv);
		};

		std::unique_ptr<GraphicsAPI::UnorderedAccessView> GetUAV(const RenderTask::CombinedAccessTex& tex) {
			std::unique_ptr<GraphicsAPI::UnorderedAccessView> uav = std::make_unique<GraphicsAPI::UnorderedAccessView>();
#if defined(GRAPHICS_API_D3D12)
			uav->InitFromApiData(tex.resource, &tex.uavDesc);
#else
			if (!uav->InitFromApiData(&pws->m_device, tex.image, tex.imageViewType, tex.format,
				tex.aspectMask, tex.baseMipLevel, tex.baseArrayLayer, tex.layerCount)) {
				Log::Fatal(L"Failed to create a SRV for texture");
			}
#endif
			return std::move(uav);
		};

		std::unique_ptr<GraphicsAPI::UnorderedAccessView> GetUAV(const RenderTask::UnorderedAccessTex& tex) {
			return std::move(GetUAV(AsCombined(tex)));
		};

		std::unique_ptr<GraphicsAPI::ShaderResourceView> GetSRV(const RenderTask::ShaderResourceTex& tex) {
			return std::move(GetSRV(AsCombined(tex)));
		};

		void RestoreInitialStates(GraphicsAPI::CommandList* cmdList) {
			RenderPass_ResourceStateTransition trans;
			for (auto& res : m_resources) {
				trans.RequestState(res.second->GetResource(), res.second->initialState);
			}
			trans.Flush(cmdList);
		}

		RenderPass_ResourceRegistry(PersistentWorkingSet* p) :pws(p) {}

	private:
		PersistentWorkingSet* pws;
		std::map<GraphicsAPI::Resource::ApiResourceID, std::unique_ptr<ResourceRef>> m_resources;
	};
};
