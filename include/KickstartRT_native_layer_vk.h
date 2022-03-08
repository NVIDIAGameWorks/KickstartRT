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

#ifdef WIN32
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <Windows.h>
#include <wrl/client.h>
#endif

#include "KickstartRT_common.h"

#if defined(KickstartRT_Graphics_API_Vulkan)

#define KickstartRT_USE_TIMELINE_SEMAPHORE 0
#ifdef WIN32
#pragma warning( push )
// non class enum warnings.
#pragma warning( disable : 26812 )
#include <vulkan/vulkan.h>
#pragma warning( pop )
#else
#include <vulkan/vulkan.h>
#endif

namespace KickstartRT {
	namespace VK {
		struct BuildGPUTaskInput {
			// If true, update BLAS and TLAS before doing any rendering task. 
			bool					geometryTaskFirst = true;
			// The max number of BLASes to be built from the build queue.
			uint32_t maxBlasBuildCount = 4u;

			VkCommandBuffer			commandBuffer = {}; // Users always need to provide a opened commandlist and SDK doesn't close the commandlist. Users cannot touch the command list unti it recieves a GPUTaskHandle from the SDK.
		};

		// ==============================================================
		// RenderTask
		// ==============================================================
		// These data structures are for lighting inputs and reflection output.
		// ==============================================================
		namespace RenderTask {
			struct ShaderResourceTex {
				// Application has a responsivility to place a proper vkCmdPipelineBarrier() to guarantee
				// VK_ACCESS_SHADER_READ_BIT and VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL from Compute shader stage.
				// SDK doesn't place any barrier for input buffers.
				VkImage					image = {};
				VkImageViewType			imageViewType = {};
				VkFormat				format = {};
				VkImageAspectFlags		aspectMask = {};
				uint32_t				baseMipLevel = 0;
				uint32_t				mipCount = 0;
				uint32_t				baseArrayLayer = 0;
				uint32_t				layerCount = 0;

				//VkImageLayout			imageLayout = {};
				//VkAccessFlagBits		accessFlagBits = {};
			};

			struct UnorderedAccessTex {
				// Application has a responsivility to place a proper vkCmdPipelineBarrier() to guarantee
				// VK_ACCESS_SHADER_WRITE_BIT and VK_IMAGE_LAYOUT_GENERAL from Compute shader stage.
				// SDK doesn't place any barrier for input buffers.
				VkImage					image = {};
				VkImageViewType			imageViewType = {};
				VkFormat				format = {};
				VkImageAspectFlags		aspectMask = {};
				uint32_t				baseMipLevel = 0;
				uint32_t				baseArrayLayer = 0;
				uint32_t				layerCount = 0;

				//VkImageLayout			imageLayout = {};
				//VkAccessFlagBits		accessFlagBits = {};
			};

			struct CombinedAccessTex {
				// Application has a responsivility to place a proper vkCmdPipelineBarrier() to guarantee
				// VK_ACCESS_SHADER_READ_BIT and VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL from Compute shader stage.
				// The SDK may alter the resource state during execution, but will return it in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
				VkImage					image = {};
				VkImageViewType			imageViewType = {};
				VkFormat				format = {};
				VkImageAspectFlags		aspectMask = {};
				uint32_t				baseMipLevel = 0;
				uint32_t				mipCount = 0;
				uint32_t				baseArrayLayer = 0;
				uint32_t				layerCount = 0;

				//VkImageLayout			imageLayout = {};
				//VkAccessFlagBits		accessFlagBits = {};
			};
		};

		// ==============================================================
		// BVHTask
		// ==============================================================
		// These data structures are for geometry inputs.
		// ==============================================================
		namespace BVHTask {
			struct VertexBufferInput {
				// format has to be VK_FORMAT_R32G32B32_SFLOAT, but the VkBuffer will be accessed as a VK_FORMAT_R32_SFLOAT typed buffer.
				// Application has a responsivility to place a proper vkCmdPipelineBarrier() to guarantee
				// VK_ACCESS_SHADER_READ_BIT from Compute shader stage.
				// SDK doesn't place any barrier for input buffers.
				VkBuffer			typedBuffer = {};
				VkFormat			format = VK_FORMAT_UNDEFINED;
				uint64_t			offsetInBytes = 0ull;
				uint32_t			strideInBytes = 0u;
				uint32_t			count = 0u;

				//VkAccessFlagBits	accessFlagBits = {};
			};
			struct IndexBufferInput {
				// format has to be either VK_FORMAT_R32_UINT or R16. The VkBuffer will be accessed as a VK_FORMAT_R32_UINT typed buffer.
				// Application has a responsivility to place a proper vkCmdPipelineBarrier() to guarantee
				// VK_ACCESS_SHADER_READ_BIT from Compute shader stage.
				// SDK doesn't place any barrier for input buffers.
				VkBuffer			typedBuffer = {};
				VkFormat			format = VK_FORMAT_UNDEFINED;
				uint64_t			offsetInBytes = 0ull;
				uint32_t			count = 0u;

				//VkAccessFlagBits	accessFlagBits = {};
			};
		};

		struct ExecuteContext_InitSettings {
			VkDevice			device = {};
			VkPhysicalDevice	physicalDevice = {};
			VkInstance			instance = {};

			bool			useInlineRaytracing = true;
			bool			useShaderTableRaytracing = true;

			uint32_t		supportedWorkingsets = 2u;
			uint32_t		descHeapSize = 8192u;
			uint32_t		uploadHeapSizeForVolatileConstantBuffers = 64u * 1024u;

			uint32_t* coldLoadShaderList = nullptr;
			uint32_t		coldLoadShaderListSize = 0u;
		};
#ifdef WIN32
#define KickstartRT_DECLSPEC_INL KickstartRT_DECLSPEC
#else
#define KickstartRT_DECLSPEC_INL
#endif
#define KickstartRT_ExecutionContext_Native
#include "KickstartRT_inl.h"
#undef KickstartRT_ExecutionContext_Native
#undef KickstartRT_DECLSPEC_INL
	};
};

#endif
