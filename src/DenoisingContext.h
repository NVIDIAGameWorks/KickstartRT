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
#include <Handle.h>
#include <OS.h>

#include <memory>

namespace KickstartRT_NativeLayer
{
	class PersistentWorkingSet;
	struct RenderPass_Denoising;

	struct DenoisingContext
	{
		const uint64_t m_id;
		DenoisingContextInput m_input;
		std::unique_ptr<RenderPass_Denoising> m_RP;

		DenoisingContext(uint64_t id, const DenoisingContextInput *input);
		~DenoisingContext();
		void DeferredRelease(PersistentWorkingSet* pws);

		static DenoisingContext* ToPtr(DenoisingContextHandle handle)
		{
			return ToPtr_s<DenoisingContext, DenoisingContextHandle>(handle);
		}

		DenoisingContextHandle ToHandle()
		{
			return ToHandle_s<DenoisingContext, DenoisingContextHandle>(this);
		};
	};
};

