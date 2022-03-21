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

// Linux complains heavily about the Status enum. Must be already defined
#ifdef Status
#undef Status
#endif

// This is defined in X.h
#ifdef None
#undef None
#endif

#ifdef WIN32
#define STDCALL __stdcall
#else
#define STDCALL
#endif

#include "KickstartRT_common.h"

#if !defined(KickstartRT_Graphics_API_D3D12) && !defined(KickstartRT_Graphics_API_Vulkan) && !defined(KickstartRT_Graphics_API_D3D11)
#error "You have to define either of KickstartRT_Graphics_API_D3D12, 11 or Vulkan."
#endif

#if !defined(KickstartRT_DECLSPEC)
#if defined(WIN32)
    #define KickstartRT_DECLSPEC __declspec(dllimport)
#else
    #define KickstartRT_DECLSPEC
#endif
#endif

#if defined (KickstartRT_Graphics_API_D3D12)
#include "KickstartRT_native_layer_d3d12.h"
#endif

#if defined(KickstartRT_Graphics_API_Vulkan)
#include "KickstartRT_native_layer_vk.h"
#endif

#if ! defined(KickstartRT_Interop_D3D11_DECLSPEC) && defined(WIN32)
#define KickstartRT_Interop_D3D11_DECLSPEC __declspec(dllimport)
#endif

#if defined (KickstartRT_Graphics_API_D3D11)
#include "KickstartRT_interop_layer_d3d11.h"
#endif

