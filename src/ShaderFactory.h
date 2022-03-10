/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#pragma once
#include "VirtualFS.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <optional>

namespace KickstartRT_NativeLayer
{
    namespace GraphicsAPI {
        struct RootSignature;
        struct ComputePipelineState;
        struct CommandList;
    };
    class PersistentWorkingSet;
    class ShaderTableRT;
};

namespace KickstartRT::ShaderFactory
{
    struct ShaderType
    {
        enum class Enum
        {
            SHADER_COMPUTE,

            SHADER_RAY_GENERATION,
            SHADER_MISS,
            SHADER_CLOSEST_HIT,
            SHADER_ANY_HIT,
            SHADER_INTERSECTION,
            SHADER_CALLABLE,
        };
    };

    struct ShaderDesc
    {
        ShaderType::Enum shaderType;
        std::wstring debugName;
        std::wstring entryName;

        ShaderDesc(ShaderType::Enum type)
            : shaderType(type)
            , debugName(L"")
            , entryName(L"main")
        { }
    };

    struct ShaderMacro
    {
        std::string name;
        std::string definition;

        ShaderMacro(const std::string& _name, const std::string& _definition)
            : name(_name)
            , definition(_definition)
        { }
    };

    class Factory;

    // ShaderDictEntry assumes the same shader permutation always uses the same root signature format.
    // Once a shader has been registered to the shader dictionary, it won't be destructed until the execution context is destructed.
    class ShaderDictEntry {
    public:
        ShaderType::Enum      m_type;
        std::wstring    m_fileName;
        std::wstring    m_entryName;
        std::wstring    m_shaderName;
        size_t          m_offset = 0;
        size_t          m_size = 0;
        std::optional<uint32_t>        m_shaderMacro_CRC;
        std::optional<uint32_t>        m_id_CRC;

        // ShaderDictEntry doesn't manage rootSignature's object life time.
        KickstartRT_NativeLayer::GraphicsAPI::RootSignature* m_rootSig = nullptr;

    protected:
        std::unique_ptr<KickstartRT_NativeLayer::GraphicsAPI::ComputePipelineState> m_cs_pso;
        std::unique_ptr<KickstartRT_NativeLayer::ShaderTableRT> m_shaderTableRT;

    public:
        Status CreateShaderObject(KickstartRT_NativeLayer::PersistentWorkingSet* pws);
        KickstartRT_NativeLayer::GraphicsAPI::ComputePipelineState *GetCSPSO(KickstartRT_NativeLayer::PersistentWorkingSet* pws);
        KickstartRT_NativeLayer::ShaderTableRT *GetShaderTableRT(KickstartRT_NativeLayer::PersistentWorkingSet* pws, KickstartRT_NativeLayer::GraphicsAPI::CommandList *cmdList);
        uint32_t CalcCRC();
        bool Loaded() const;

        ShaderDictEntry();
        ~ShaderDictEntry();
    };

    class Factory
    {
    private:
        std::vector<uint32_t>                                               m_coldLoadShaderList;
        std::unordered_map<uint32_t, std::unique_ptr<ShaderDictEntry>>      m_shaderDict;

        std::unordered_map<std::wstring, std::shared_ptr<ShaderBlob::IBlob>> m_BytecodeCache;
        std::shared_ptr<VirtualFS::IFileSystem> m_fs;
        std::filesystem::path m_basePath;

        std::optional<std::pair<size_t, size_t>> FindShaderPermutationOffset(std::shared_ptr<ShaderBlob::IBlob>& blob, const ShaderDesc& d, std::optional<uint32_t> shaderMacroCRC, bool errorIfNotFound = true);
        std::shared_ptr<ShaderBlob::IBlob> FindShaderPermutation(std::shared_ptr<ShaderBlob::IBlob>& blob, const ShaderDesc& d, const ShaderBlob::ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound = true);

    public:
        Factory(
            std::shared_ptr<VirtualFS::IFileSystem> fs,
            const std::filesystem::path& basePath,
            const uint32_t * const coldLoadShaderList, uint32_t coldLoadShaderListSize);
        void ClearCache();

        static std::optional<uint32_t> GetShaderMacroCRC(const std::vector<ShaderMacro>* pDefines);
        std::pair<Status, ShaderDictEntry *> RegisterShader(std::unique_ptr<ShaderDictEntry> ent);
        Status GetLoadedShaderList(uint32_t *loadedListBuffer, size_t bufferSize, size_t *retListSize);
        Status LoadColdLoadShaders(KickstartRT_NativeLayer::PersistentWorkingSet* pws);

        std::optional<std::pair<size_t, size_t>> FindShaderOffset(const wchar_t* fileName, const wchar_t* entryName, std::optional<uint32_t> shaderMacroCRC, ShaderFactory::ShaderType::Enum shaderType);
        std::optional<std::pair<size_t, size_t>> FindShaderOffset(const wchar_t* fileName, const wchar_t* entryName, std::optional<uint32_t> shaderMacroCRC, const ShaderFactory::ShaderDesc& desc);

        std::shared_ptr<ShaderBlob::IBlob> FindShader(const wchar_t* fileName, const wchar_t* entryName, const std::vector<ShaderFactory::ShaderMacro>* pDefines, ShaderFactory::ShaderType::Enum shaderType);
        std::shared_ptr<ShaderBlob::IBlob> FindShader(const wchar_t* fileName, const wchar_t* entryName, const std::vector<ShaderFactory::ShaderMacro>* pDefines, const ShaderFactory::ShaderDesc& desc);

        std::shared_ptr<ShaderBlob::IBlob> GetBytecode(const wchar_t* fileName, const wchar_t* entryName);
    };

};
