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
#include <GraphicsAPI/GraphicsAPI.h>
#include <ShaderFactory.h>
#include <PersistentWorkingSet.h>

#include <memory>

namespace KickstartRT_NativeLayer
{
	// This supports only a simple case of shader table RT. 

	class ShaderTableRT : public GraphicsAPI::DeviceObject {
	public:
		std::unique_ptr<GraphicsAPI::RaytracingPipelineState>		m_rtPSO;
		std::unique_ptr<GraphicsAPI::Buffer>						m_uploadBuf;
		std::unique_ptr<GraphicsAPI::Buffer>						m_deviceBuf;
		bool														m_needToCoyBuffer = false;
#if defined(GRAPHICS_API_D3D12)
		D3D12_GPU_VIRTUAL_ADDRESS_RANGE				m_RG_Addr = {};
		D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE	m_MS_Addr = {};
		D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE	m_HG_Addr = {};
#elif defined(GRAPHICS_API_VK)
		VkStridedDeviceAddressRegionKHR			m_RG_Addr = {};
		VkStridedDeviceAddressRegionKHR			m_MS_Addr = {};
		VkStridedDeviceAddressRegionKHR			m_HG_Addr = {};
		VkStridedDeviceAddressRegionKHR			m_CL_Addr = {};
#endif
	public:
		virtual ~ShaderTableRT();
		static std::unique_ptr<ShaderTableRT> Init(PersistentWorkingSet* pws, const GraphicsAPI::RootSignature* globalRootSig, std::shared_ptr<ShaderBlob::Blob::IBlob> blob);
		static Status BatchCopy(GraphicsAPI::CommandList* cmdList, std::vector<ShaderTableRT*> stArr);

		void DispatchRays(GraphicsAPI::CommandList* cmdList, uint32_t width, uint32_t height);
	};
};
