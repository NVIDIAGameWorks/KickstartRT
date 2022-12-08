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
#include <vector>
#include <string>

#define RETURN_IF_STATUS_FAILED(x) \
  do { \
    KickstartRT::Status sts##__COUNTER__ = (x); \
    if (sts##__COUNTER__ != KickstartRT::Status::OK) return sts##__COUNTER__;\
  } while (0)

namespace KickstartRT_NativeLayer
{
    template <typename ... Args>
    inline std::wstring DebugName(const std::wstring& fmt, Args ... args)
    {
        int len = std::swprintf(nullptr, 0, fmt.c_str(), args ...);
        // todo - not quite right for linux. Returns a negative
        if (len <= 0)
            len = 256;

        std::vector<wchar_t> buf(len + 1);
        std::swprintf(&buf[0], len + 1, fmt.c_str(), args ...);

        const std::wstring sWrk(L"KS:");
        return sWrk + buf.data();
    };

    template <>
    inline std::wstring DebugName(const std::wstring& str)
    {
        return std::wstring(L"KS:") + str;
    }

    template <typename ... Args>
    inline std::string DebugName(const std::string& fmt, Args ... args)
    {
        size_t len = std::snprintf(nullptr, 0, fmt.c_str(), args ...);
        std::vector<char> buf(len + 1);
        std::snprintf(&buf[0], len + 1, fmt.c_str(), args ...);

        const std::string sWrk("KS:");
        return sWrk + buf.data();
    };

    template <>
    inline std::string DebugName(const std::string& str)
    {
        return std::string("KS:") + str;
    }

    inline const char* GetString(DenoisingContextInput::DenoisingMethod method) {
        switch (method) {
        case DenoisingContextInput::DenoisingMethod::NRD_Reblur:
            return "NRD_Reblur";
        case DenoisingContextInput::DenoisingMethod::NRD_Relax:
            return "NRD_Relax";
        case DenoisingContextInput::DenoisingMethod::NRD_Sigma:
            return "NRD_Sigma";
        default:
            return "Unknown";
        }
    }

    inline const char* GetString(DenoisingContextInput::SignalType signalType) {
        switch (signalType) {
        case DenoisingContextInput::SignalType::Specular:
            return "Specular";
        case DenoisingContextInput::SignalType::Diffuse:
            return "Diffuse";
        case DenoisingContextInput::SignalType::SpecularAndDiffuse:
            return "SpecularAndDiffuse";
        case DenoisingContextInput::SignalType::DiffuseOcclusion:
            return "DiffuseOcclusion";
        case DenoisingContextInput::SignalType::Shadow:
            return "Shadow";
        case DenoisingContextInput::SignalType::MultiShadow:
            return "MultiShadow";
        default:
            return "Unknown";
        }
    }

    namespace Utils {
        void LogGeometryInput(const BVHTask::GeometryInput* input);

        bool CheckInputTextureState(GraphicsAPI::CommandList* cmdList, const RenderTask::ShaderResourceTex* inputTex, GraphicsAPI::ResourceState::State expectedState);
        bool CheckInputTextureState(GraphicsAPI::CommandList* cmdList, const RenderTask::UnorderedAccessTex* inputTex, GraphicsAPI::ResourceState::State expectedState);
    };

    namespace GraphicsAPI {

        struct TexValidator {
            TexValidator(const char* debugName, const RenderTask::UnorderedAccessTex& uaTex) :m_debugName(debugName) {
#if defined(GRAPHICS_API_D3D12)
                m_format = Resource::GetResourceFormat(uaTex.uavDesc.Format);
                m_isNull = uaTex.resource == nullptr;
#elif defined(GRAPHICS_API_VK)
                m_format = Resource::GetResourceFormat(uaTex.format);
                m_isNull = uaTex.image == 0;
#endif
                m_formatType = Resource::GetFormatType(m_format);
                m_channelCount = Resource::GetChannelCount(m_format);
            };

            TexValidator(const char* debugName, const RenderTask::ShaderResourceTex& srvTex) :m_debugName(debugName) {
#if defined(GRAPHICS_API_D3D12)
                m_format = Resource::GetResourceFormat(srvTex.srvDesc.Format);
                m_isNull = srvTex.resource == nullptr;
#elif defined(GRAPHICS_API_VK)
                m_format = Resource::GetResourceFormat(srvTex.format);
                m_isNull = srvTex.image == 0;
#endif
                m_formatType = Resource::GetFormatType(m_format);
                m_channelCount = Resource::GetChannelCount(m_format);
            };

            TexValidator(const char* debugName, const RenderTask::CombinedAccessTex& tex) :m_debugName(debugName) {
#if defined(GRAPHICS_API_D3D12)
                m_format = Resource::GetResourceFormat(tex.srvDesc.Format);
                m_isNull = tex.resource == nullptr;
#elif defined(GRAPHICS_API_VK)
                m_format = Resource::GetResourceFormat(tex.format);
                m_isNull = tex.image == 0;
#endif
                m_formatType = Resource::GetFormatType(m_format);
                m_channelCount = Resource::GetChannelCount(m_format);
            };

            bool IsNull() const { return m_isNull; }

            Status AssertIsNotNull() const {
                if (m_isNull) {
                    Log::Fatal(L"Unexpected (%s) to be set", m_debugName);
                    return Status::ERROR_INVALID_PARAM;
                }
                return Status::OK;
            }

            Status AssertChannelCount(std::initializer_list<uint32_t> counts) const {
                if (!IsAnyOf(m_channelCount, counts)) {
                    Log::Fatal(L"Unexpected channel count. Has (%d) channels.", m_channelCount);
                    return Status::ERROR_INVALID_PARAM;
                }
                return Status::OK;
            }

            template<class... TArgs>
            Status AssertFormatType(std::initializer_list<Resource::FormatType> formats) const {
               if (!IsAnyOf(m_formatType, formats)) {
                   Log::Fatal(L"Unexpected format type. Is (%d)", m_formatType);
                       return Status::ERROR_INVALID_PARAM;
               }
               return Status::OK;
            }
        private:
            template<class T>
            bool IsAnyOf(T object, std::initializer_list<T> list) const {
                for (auto l : list) {
                    if (object == l) return true;
                }
                return false;
            }

            const char* m_debugName;
            bool m_isNull;
            Resource::Format m_format;
            Resource::FormatType m_formatType;
            uint32_t m_channelCount;
        };
    };
};


