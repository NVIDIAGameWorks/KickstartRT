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

#include "KickstartRT_common.h"

#if defined (KickstartRT_Graphics_API_D3D12)

#include <d3d12.h>

namespace KickstartRT {
	namespace D3D12 {
		struct BuildGPUTaskInput {
			// If true, update BLAS and TLAS before doing any rendering task. 
			bool					geometryTaskFirst = true;

			// Users always need to provide a opened commandlist and SDK doesn't close the commandlist. Users cannot touch the command list unti it recieves a GPUTaskHandle from the SDK.
			ID3D12CommandList* commandList = nullptr;
		};

		// ==============================================================
		// RenderTask
		// ==============================================================
		// These data structures are for lighting inputs and reflection output.
		// ==============================================================
		namespace RenderTask {
			struct ShaderResourceTex {
				// SDK doesn't change the resource state in it and SDK always expects that ShaderResourceTex is readable from compute shaders and raytracing shaders during SDK commandlist's execution.
				// So, its state should be either D3D12_RESOURCE_STATE_COMMON or NON_PIXEL_SHADER_RESOURCE.
				D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				ID3D12Resource* resource = nullptr;
			};

			struct UnorderedAccessTex {
				// SDK doesn't change the resource state in it and SDK always expects that UnorderedAccessTex is writable from compute shaders and raytracing shaders during SDK commandlist's execution.
				// So, its state should be D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
				D3D12_UNORDERED_ACCESS_VIEW_DESC	uavDesc = {};
				ID3D12Resource* resource = nullptr;
			};

			struct CombinedAccessTex {
				// SDK doesn't change the resource state in it and SDK always expects that CombinedAccessTex is readable from compute shaders and raytracing shaders during SDK commandlist's execution.
				// So, its state should be either D3D12_RESOURCE_STATE_COMMON or NON_PIXEL_SHADER_RESOURCE.
				// The SDK may alter the resource state during execution, but will return it in the same state it arrived.
				D3D12_SHADER_RESOURCE_VIEW_DESC		srvDesc = {};
				D3D12_UNORDERED_ACCESS_VIEW_DESC	uavDesc = {};
				ID3D12Resource* resource = nullptr;
			};
		};

		// ==============================================================
		// BVHTask
		// ==============================================================
		// These data structures are for geometry inputs to construct BVHs.
		// ==============================================================
		namespace BVHTask {
			struct VertexBufferInput {
				// SDK doesn't change the resource state in it and SDK always expects that s_VertexBuffer is readable from compute shaders and raytracing shaders during SDK commandlist's execution.
				// So, its state should be any one of D3D12_RESOURCE_STATE_COMMON, NON_PIXEL_SHADER_RESOURCE or GENERIC_READ.
				ID3D12Resource* resource = nullptr;
				DXGI_FORMAT				format = DXGI_FORMAT_UNKNOWN;
				UINT64					offsetInBytes = 0ull;
				UINT					strideInBytes = 0u;
				UINT					count = 0u;
			};
			struct IndexBufferInput {
				// SDK doesn't change the resource state in it and SDK always expects that s_IndexBuffer is readable from compute shaders and raytracing shaders during SDK commandlist's execution.
				// So, its state should be any one of D3D12_RESOURCE_STATE_COMMON, NON_PIXEL_SHADER_RESOURCE or GENERIC_READ.
				ID3D12Resource* resource = nullptr;
				DXGI_FORMAT				format = DXGI_FORMAT_UNKNOWN;
				UINT64					offsetInBytes = 0ull;
				UINT					count = 0u;
			};
		};

		struct ExecuteContext_InitSettings {
			ID3D12Device* D3D12Device = nullptr;

			bool			useInlineRaytracing = true;
			bool			useShaderTableRaytracing = true;

			uint32_t		supportedWorkingsets = 2u;
			uint32_t		descHeapSize = 8192u;
			uint32_t		uploadHeapSizeForVolatileConstantBuffers = 64u * 1024u;

			uint32_t* coldLoadShaderList = nullptr;
			uint32_t		coldLoadShaderListSize = 0u;
		};

#define KickstartRT_DECLSPEC_INL KickstartRT_DECLSPEC
#define KickstartRT_ExecutionContext_Native
#include "KickStartRT_inl.h"
#undef KickstartRT_ExecutionContext_Native
#undef KickstartRT_DECLSPEC_INL
	};
};

#endif