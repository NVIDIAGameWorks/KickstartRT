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
#if !defined(KICKSTARTRT_SDK_DEFINES_H)
#define KICKSTARTRT_SDK_DEFINES_H

// This file is also referred from HLSL shaders.
#define KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_PERSISTENT_DEVICE_RESOURCES 1
#define KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_TEMPORAL_DEVICE_RESOURCES 1
#define KICKSTARTRT_ENABLE_SHARED_BUFFERS_FOR_READBACK_AND_COUNTER_RESOURCES 1

// This enables indirection table to refer Direct Lighting Cache in shaders, and it will reduce the number of descriptor table entry while ray tracing.
// This required to enable shared buffers.
// that this is also referred in shader codes, so you need to recompile them after changing the sate.
#define KICKSTARTRT_ENABLE_DIRECT_LIGHTING_CACHE_INDIRECTION_TABLE (0 & KICKSTARTRT_SDK_ENABLE_SHARED_BUFFERS_FOR_PERSISTENT_DEVICE_RESOURCES & KICKSTARTRT_SDK_ENABLE_SHARED_BUFFERS_FOR_TEMPORAL_DEVICE_RESOURCES)

#if !defined(KickstartRT_SDK_WITH_NRD)
#error "KickstartRT_SDK_WITH_NRD must be defined as either 0 or 1."
#endif

#define KICKSTARTRT_USE_BYTEADDRESSBUFFER_FOR_DLC 0

#endif
