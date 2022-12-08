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
#include <Utils.h>
#include <Log.h>

namespace KickstartRT_NativeLayer {

    namespace Utils {
        void LogGeometryInput(const BVHTask::GeometryInput *input)
        {
            Log::Info(L"Name: %s", input->name ? input->name : L"Null");

            for (auto&& cmp : input->components) {
                Log::Info(L"VertexBuffer: Offset: %d, Stride : %d, Count: %d",
                    cmp.vertexBuffer.offsetInBytes, cmp.vertexBuffer.strideInBytes, cmp.vertexBuffer.count);
                Log::Info(L"IndexBuffer: Offset: %d, Count: %d",
                    cmp.indexBuffer.offsetInBytes, cmp.indexBuffer.count);
                Log::Info(L"IndexRange: Enabled: %s, Min: %d, Max: %d",
                    cmp.indexRange.isEnabled ? L"True" : L"False",
                    cmp.indexRange.minIndex, cmp.indexRange.maxIndex);
            }
        };

        bool CheckInputTextureState(GraphicsAPI::CommandList* cmdList, const RenderTask::ShaderResourceTex* inputTex, GraphicsAPI::ResourceState::State expectedState)
        {
#if defined(GRAPHICS_API_D3D12)
            auto SRVToSubResRange = [](const D3D12_SHADER_RESOURCE_VIEW_DESC& srvDesc) -> GraphicsAPI::SubresourceRange {
                switch (srvDesc.ViewDimension) {
                case D3D12_SRV_DIMENSION_TEXTURE2D:
                {
                    const auto& desc(srvDesc.Texture2D);
                    return GraphicsAPI::SubresourceRange(0, 1, (uint8_t)desc.MostDetailedMip, (uint8_t)desc.MipLevels);
                }
                case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
                {
                    const auto& desc(srvDesc.Texture2DArray);
                    return GraphicsAPI::SubresourceRange((uint8_t)desc.FirstArraySlice, (uint8_t)desc.ArraySize, (uint8_t)desc.MostDetailedMip, (uint8_t)desc.MipLevels);
                }
                default:
                    return GraphicsAPI::SubresourceRange(0, 1, 0, 1);
                }
            };

            GraphicsAPI::Texture	t;
            {
                GraphicsAPI::Texture::ApiData initData;
                initData.m_resource = inputTex->resource;
                t.InitFromApiData(initData, GraphicsAPI::ResourceState::State::Common); // dummy state.
            }

            GraphicsAPI::SubresourceRange	subResRange = SRVToSubResRange(inputTex->srvDesc);
            GraphicsAPI::Resource* tArr[1] = { &t };
            return cmdList->AssertResourceStates(tArr, &subResRange, 1, &expectedState);
#else
            // VK doesn't have resource state.
            (void)cmdList; (void)inputTex; (void)expectedState;
            return true;
#endif
        };

        bool CheckInputTextureState(GraphicsAPI::CommandList* cmdList, const RenderTask::UnorderedAccessTex* inputTex, GraphicsAPI::ResourceState::State expectedState)
        {
#if defined(GRAPHICS_API_D3D12)
            auto UAVToSubResRange = [](const D3D12_UNORDERED_ACCESS_VIEW_DESC& uavDesc) -> GraphicsAPI::SubresourceRange {
                switch (uavDesc.ViewDimension) {
                case D3D12_UAV_DIMENSION_TEXTURE2D:
                {
                    // Depth stencil plance slice is not supported.
                    const auto& desc(uavDesc.Texture2D);
                    return GraphicsAPI::SubresourceRange(0, 1, (uint8_t)desc.MipSlice, 1);
                }
                case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
                {
                    const auto& desc(uavDesc.Texture2DArray);
                    return GraphicsAPI::SubresourceRange((uint8_t)desc.FirstArraySlice, (uint8_t)desc.ArraySize, (uint8_t)desc.MipSlice, 1);
                }
                default:
                    return GraphicsAPI::SubresourceRange(0, 1, 0, 1);
                }
            };

            GraphicsAPI::Texture	t;
            {
                GraphicsAPI::Texture::ApiData initData;
                initData.m_resource = inputTex->resource;
                t.InitFromApiData(initData, GraphicsAPI::ResourceState::State::Common); // dummy state.
            }

            GraphicsAPI::SubresourceRange	subResRange = UAVToSubResRange(inputTex->uavDesc);
            GraphicsAPI::Resource* tArr[1] = { &t };
            return cmdList->AssertResourceStates(tArr, &subResRange, 1, &expectedState);
#else
            // VK doesn't have resource state.
            (void)cmdList; (void)inputTex; (void)expectedState;
            return true;
#endif
        };

    };
};
