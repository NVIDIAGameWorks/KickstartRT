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

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>
#include <wrl/client.h>
#include <stdint.h>

#include "KickstartRT_common.h"

#if defined (KickstartRT_Graphics_API_D3D11)

#include <d3d11_4.h>

namespace KickstartRT {
	namespace D3D11 {
		struct BuildGPUTaskInput {
			// If true, update BLAS and TLAS before doing any rendering task. 
			bool			geometryTaskFirst = true;
			// The max number of BLASes to be built from the build queue.
			uint32_t		maxBlasBuildCount = 4u;

			ID3D11Fence*	waitFence;
			uint64_t		waitFenceValue = (uint64_t)-1;
			ID3D11Fence*	signalFence;
			uint64_t		signalFenceValue = (uint64_t)-1;
		};

		// ==============================================================
		// RenderTask
		// ==============================================================
		namespace RenderTask {
			struct ShaderResourceTex {
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				ID3D11Resource* resource = nullptr;
			};

			struct UnorderedAccessTex {
				D3D11_UNORDERED_ACCESS_VIEW_DESC	uavDesc = {};
				ID3D11Resource* resource = nullptr;
			};

			struct CombinedAccessTex {
				D3D11_SHADER_RESOURCE_VIEW_DESC		srvDesc = {};
				D3D11_UNORDERED_ACCESS_VIEW_DESC	uavDesc = {};
				ID3D11Resource* resource = nullptr;
			};
		};

		// ==============================================================
		// BVHTask
		// ==============================================================
		// These data structures are for geometry inputs.
		// ==============================================================
		namespace BVHTask {
			struct VertexBufferInput {
				ID3D11Resource* resource = nullptr;
				DXGI_FORMAT				format = DXGI_FORMAT_UNKNOWN;
				UINT64					offsetInBytes = 0ull;
				UINT					strideInBytes = 0u;
				UINT					count = 0u;
			};
			struct IndexBufferInput {
				ID3D11Resource* resource = nullptr;
				DXGI_FORMAT				format = DXGI_FORMAT_UNKNOWN;
				UINT64					offsetInBytes = 0ull;
				UINT					count = 0u;
			};
		};

		struct ExecuteContext_InitSettings {
			IDXGIAdapter1* DXGIAdapter = nullptr;
			ID3D11Device* D3D11Device = nullptr;

			enum class UsingCommandQueue : uint32_t {
				Direct,
				Compute,
			};

			UsingCommandQueue usingCommandQueue = UsingCommandQueue::Direct;
			uint32_t		supportedWorkingSet = 4u;
			uint32_t		descHeapSize = 8192u;
			uint32_t		uploadHeapSizeForVolatileConstantBuffers = 64u * 1024u;

			uint32_t*		coldLoadShaderList = nullptr;
			uint32_t		coldLoadShaderListSize = 0u;
		};

#define KickstartRT_DECLSPEC_INL KickstartRT_Interop_D3D11_DECLSPEC
#define KickstartRT_ExecutionContext_Interop
#include "KickstartRT_inl.h"
#undef KickstartRT_ExecutionContext_Interop
#undef KickstartRT_DECLSPEC_INL
	};
};

#endif
