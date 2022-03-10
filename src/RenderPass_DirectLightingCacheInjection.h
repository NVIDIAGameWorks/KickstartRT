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

#include <memory>
#include <deque>

namespace KickstartRT_NativeLayer
{
	class PersistentWorkingSet;
	class TaskWorkingSet;
	struct RenderPass_ResourceRegistry;
};

namespace KickstartRT_NativeLayer
{
	struct RenderPass_DirectLightingCacheInjection
	{
		static constexpr uint32_t     m_threadDim_XY[2] = { 8, 16 };

		enum DescTableLayout : uint32_t {
			e_CB_CBV = 0,
			e_DepthTex_SRV,
			e_LightingTex_SRV,
			e_DescTableSize,
		};

		struct CB {
			uint32_t    m_Viewport_TopLeftX;
			uint32_t    m_Viewport_TopLeftY;
			uint32_t    m_Viewport_Width;
			uint32_t    m_Viewport_Height;

			float       m_Viewport_MinDepth;
			float       m_Viewport_MaxDepth;
			uint32_t	m_CTA_Swizzle_GroupDimension_X;
			uint32_t	m_CTA_Swizzle_GroupDimension_Y;

			float       m_rayOrigin[3];
			uint32_t    m_depthType;

			float		m_averageWindow;
			uint32_t    m_fastAccum;
			float		m_padding[2];

			Math::Float_4x4	m_clipToViewMatrix;
			Math::Float_4x4	m_viewToWorldMatrix;
		};

		struct CB_clear {
			uint32_t	m_instanceIndex;
			uint32_t    m_numberOfTiles;
			uint32_t    m_resourceIndex;
			uint32_t    m_pad_u1;
			float       m_clearColor[3];
			float		m_pad_f0;
		};

		bool		m_enableInlineRaytracing = false;
		bool		m_enableShaderTableRaytracing = false;

		GraphicsAPI::DescriptorTableLayout	m_descTableLayout1;
		GraphicsAPI::DescriptorTableLayout	m_descTableLayout2;

		GraphicsAPI::RootSignature			m_rootSignature;

		ShaderFactory::ShaderDictEntry*		m_shaderTable = nullptr;
		ShaderFactory::ShaderDictEntry*		m_pso = nullptr;
		ShaderFactory::ShaderDictEntry*		m_pso_clear = nullptr;

    public:
        Status Init(PersistentWorkingSet *pws, bool enableInlineRaytracing, bool enableShaderTableRaytracing);

		Status Dispatch(TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList, RenderPass_ResourceRegistry* resources, const RenderTask::DirectLightingInjectionTask* input);
		Status BuildCommandList(TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList,
			RenderPass_ResourceRegistry* resources,
			GraphicsAPI::DescriptorTable *lightingCache_descTable, const RenderTask::DirectLightingInjectionTask* directLightingInjection);

		Status DispatchClear(TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList, const CB_clear& clCB);
		Status BuildCommandListClear(TaskWorkingSet* fws, GraphicsAPI::CommandList* cmdList,
			GraphicsAPI::DescriptorTable* lightingCache_descTable, const std::deque<CB_clear>& clearList);
	};
};
