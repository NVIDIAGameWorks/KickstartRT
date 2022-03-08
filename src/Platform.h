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
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wrl/client.h>
#else
#include <cstring>
#endif

#include <assert.h>

#if defined(GRAPHICS_API_D3D12)
#include <d3d12.h>

#if defined(USE_PIX)
#pragma warning( push )
#pragma warning( disable : 6101 )
#pragma warning( disable : 26812 )

#include <WinPixEventRuntime/pix3.h>
#pragma warning( pop )
#endif

#elif defined(GRAPHICS_API_VK)
#ifdef WIN32
#pragma warning( push )
// non class enum warnings.
#pragma warning( disable : 26812 )
#include <vulkan/vulkan.h>
#pragma warning( pop )
#else
#include <vulkan/vulkan.h>
#endif
#else
#error "Either GRAPHICS_API_D3D12 or GRAPHICS_API_VK must be defined."
#endif

#if defined(GRAPHICS_API_D3D12)
#define KickstartRT_Graphics_API_D3D12
#elif defined(GRAPHICS_API_VK)
#define KickstartRT_Graphics_API_Vulkan
#endif

#include "KickstartRT.h"

#if defined(GRAPHICS_API_D3D12)
#define KickstartRT_NativeLayer KickstartRT::D3D12
#elif defined(GRAPHICS_API_VK)
#define KickstartRT_NativeLayer KickstartRT::VK
#endif

#include "common/SDKDefines.h"

