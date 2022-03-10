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
#include "common/ShaderBlob.h"
#include "common/CRC.h"
#include "Log.h"
#include "ShaderFactory.h"

#include "GraphicsAPI/GraphicsAPI.h"
#include "ShaderTableRT.h"
#include "Utils.h"

#include <iostream>

namespace KickstartRT::ShaderFactory {
    namespace FS = KickstartRT::VirtualFS;
    namespace Blob = KickstartRT::ShaderBlob;
    namespace Log = KickstartRT_NativeLayer::Log;
    using namespace KickstartRT_NativeLayer::BVHTask;

    Factory::Factory(
        std::shared_ptr<FS::IFileSystem> fs,
        const std::filesystem::path& basePath,
        const uint32_t* const coldLoadShaderList, uint32_t coldLoadShaderListSize) :
        m_fs(fs)
        , m_basePath(basePath)
    {
        constexpr uint32_t headerSize = 3;
        if (coldLoadShaderListSize > headerSize) {
            Version listVersion;
            listVersion.Major = coldLoadShaderList[0];
            listVersion.Minor = coldLoadShaderList[1];
            listVersion.Patch = coldLoadShaderList[2];

            if (! (listVersion == Version())) {
                Log::Warning(L"Cold load shader list has been created with different version of a library. Strongly recommend to take a new shader loaded list with the current SDK.");
            }
            
            m_coldLoadShaderList.resize(coldLoadShaderListSize - headerSize);
            m_coldLoadShaderList.assign(coldLoadShaderList + headerSize, coldLoadShaderList + coldLoadShaderListSize);
        }
    }

    void Factory::ClearCache()
    {
        m_BytecodeCache.clear();
    }

    std::shared_ptr<Blob::IBlob> Factory::GetBytecode(const wchar_t* fileName, const wchar_t* entryName)
    {
        if (!entryName)
            entryName = L"main";

        std::wstring adjustedName = fileName;
        {
            size_t pos = adjustedName.find(L".hlsl");
            if (pos != std::string::npos)
                adjustedName.erase(pos, 5);

            if (entryName && wcscmp(entryName, L"main"))
                adjustedName += L"_" + std::wstring(entryName);
        }

        std::filesystem::path shaderFilePath = m_basePath / (adjustedName + L".bin");

        std::shared_ptr<Blob::IBlob>& data = m_BytecodeCache[shaderFilePath.generic_string<wchar_t>()];

        if (data)
            return data;

        data = m_fs->readFile(shaderFilePath);

        if (!data)
        {
            Log::Error(L"Couldn't read the binary file for shader %s from %s", fileName, shaderFilePath.generic_string<wchar_t>().c_str());
            return nullptr;
        }

        return data;
    }

    std::optional<std::pair<size_t, size_t>> Factory::FindShaderPermutationOffset(std::shared_ptr<Blob::IBlob>& blob, const ShaderDesc& /* d */, std::optional<uint32_t> shaderMacroCRC, bool errorIfNotFound)
    {
        size_t offset, size;

        if (Blob::FindPermutationInBlob(blob->data(), blob->size(), shaderMacroCRC, &offset, &size))
        {

            return std::pair<size_t, size_t>(offset, size);
        }
        else
        {
            if (errorIfNotFound)
            {
                Log::Message(Log::Severity::Error, Blob::FormatShaderNotFoundMessage(blob->data(), blob->size(), shaderMacroCRC).c_str());
            }
        }
        return std::nullopt;
    }

    std::shared_ptr<Blob::IBlob> Factory::FindShaderPermutation(std::shared_ptr<Blob::IBlob>& blob, const ShaderDesc& /* d */, const Blob::ShaderConstant* constants, uint32_t numConstants, bool errorIfNotFound)
    {
        const void* binary = nullptr;
        size_t binarySize = 0;

        if (Blob::FindPermutationInBlob(blob->data(), blob->size(), constants, numConstants, &binary, &binarySize))
        {
            return std::make_shared<Blob::SubBlob>(blob, reinterpret_cast<intptr_t>(binary) - reinterpret_cast<intptr_t>(blob->data()), binarySize);
        }
        else
        {
            if (errorIfNotFound)
            {
                Log::Message(Log::Severity::Error, Blob::FormatShaderNotFoundMessage(blob->data(), blob->size(), constants, numConstants).c_str());
            }

            return std::make_shared<Blob::SubBlob>();
        }
    }

    std::optional<std::pair<size_t, size_t>> Factory::FindShaderOffset(const wchar_t* fileName, const wchar_t* entryName, std::optional<uint32_t> shaderMacroCRC, ShaderType::Enum shaderType)
    {
        ShaderDesc desc = ShaderDesc(shaderType);
        desc.debugName = fileName;
        return FindShaderOffset(fileName, entryName, shaderMacroCRC, desc);
    }

    std::optional<std::pair<size_t, size_t>> Factory::FindShaderOffset(const wchar_t* fileName, const wchar_t* entryName, std::optional<uint32_t> shaderMacroCRC, const ShaderDesc& desc)
    {
        std::shared_ptr<Blob::IBlob> byteCode = GetBytecode(fileName, entryName);

        if (!byteCode)
            return std::nullopt;

        ShaderFactory::ShaderDesc descCopy = desc;
        descCopy.entryName = entryName;

        return FindShaderPermutationOffset(byteCode, descCopy, shaderMacroCRC);
    }

    std::shared_ptr<Blob::IBlob> Factory::FindShader(const wchar_t* fileName, const wchar_t* entryName, const std::vector<ShaderMacro>* pDefines, ShaderType::Enum shaderType)
    {
        ShaderDesc desc = ShaderDesc(shaderType);
        desc.debugName = fileName;
        return FindShader(fileName, entryName, pDefines, desc);
    }

    std::shared_ptr<Blob::IBlob> Factory::FindShader(const wchar_t* fileName, const wchar_t* entryName, const std::vector<ShaderMacro>* pDefines, const ShaderDesc& desc)
    {
        std::shared_ptr<Blob::IBlob> byteCode = GetBytecode(fileName, entryName);

        if (!byteCode)
            return std::make_shared<Blob::SubBlob>();

        std::vector<Blob::ShaderConstant> constants;
        if (pDefines)
        {
            for (const ShaderMacro& define : *pDefines)
                constants.push_back(Blob::ShaderConstant{ define.name.c_str(), define.definition.c_str() });
        }

        ShaderFactory::ShaderDesc descCopy = desc;
        descCopy.entryName = entryName;

        return FindShaderPermutation(byteCode, descCopy, constants.data(), uint32_t(constants.size()));
    }

    std::optional<uint32_t> Factory::GetShaderMacroCRC(const std::vector<ShaderMacro>* pDefines)
    {
        std::vector<Blob::ShaderConstant> constants;

        if (pDefines->size() == 0)
            return std::nullopt;

        for (const ShaderMacro& define : *pDefines)
            constants.push_back(Blob::ShaderConstant{ define.name.c_str(), define.definition.c_str() });

        return ShaderBlob::GetShaderConstantCRC(constants.data(), (uint32_t)constants.size());
    };

    Status Factory::GetLoadedShaderList(uint32_t* loadedListBuffer, size_t bufferSize, size_t* retListSize)
    {
        *retListSize = 0;
        memset(loadedListBuffer, 0, sizeof(uint32_t) * bufferSize);

        uint32_t writtenElementCnt = 0;

        // First 3 entries are used to store library version.
        {
            Version v;
            loadedListBuffer[writtenElementCnt++] = v.Major;
            loadedListBuffer[writtenElementCnt++] = v.Minor;
            loadedListBuffer[writtenElementCnt++] = v.Patch;
        }

        for (auto&& itr : m_shaderDict) {
            if (!itr.second->Loaded())
                continue;
            loadedListBuffer[writtenElementCnt++] = itr.first;
            if (writtenElementCnt == bufferSize)
                break;
        }
        *retListSize = writtenElementCnt;
       
        return Status::OK;
    }

    Status Factory::LoadColdLoadShaders(KickstartRT_NativeLayer::PersistentWorkingSet* pws)
    {
        for (auto hash : m_coldLoadShaderList) {
            auto itr = m_shaderDict.find(hash);
            if (itr == m_shaderDict.end())
                continue;
            if (itr->second->Loaded())
                continue;

            if (itr->second->m_type == ShaderFactory::ShaderType::Enum::SHADER_COMPUTE ||
                itr->second->m_type == ShaderFactory::ShaderType::Enum::SHADER_RAY_GENERATION) {
                auto sts = itr->second->CreateShaderObject(pws);
                if (sts != Status::OK) {
                    Log::Fatal(L"Failed to create shader object: %s", itr->second->m_fileName.c_str());
                    return Status::ERROR_INTERNAL;
                }
            }
            else {
                Log::Fatal(L"Unsupported shader type detected.");
                return Status::ERROR_INTERNAL;
            }
        }

        if (m_coldLoadShaderList.size() > 0) {
            // If cold load list has been specified, clear bytecode cache here to reduce mem usage.
            ClearCache();
            m_coldLoadShaderList.clear();
        }

        return Status::OK;
    }

    ShaderDictEntry::ShaderDictEntry() = default;
    ShaderDictEntry::~ShaderDictEntry() = default;

    bool ShaderDictEntry::Loaded() const
    {
        if (m_id_CRC.has_value()) {
            if (m_cs_pso || m_shaderTableRT)
                return true;
        }

        return false;
    };

    Status ShaderDictEntry::CreateShaderObject(KickstartRT_NativeLayer::PersistentWorkingSet* pws)
    {
        namespace Native = KickstartRT_NativeLayer;
        auto& factory(pws->m_shaderFactory);

        if (m_rootSig == nullptr) {
            Log::Message(Log::Severity::Fatal, L"Null root signature detected when creating a shader object");
            return Status::ERROR_INTERNAL;
        }

        if (m_type == ShaderType::Enum::SHADER_COMPUTE)
        {
            std::shared_ptr<Blob::IBlob> byteCode = factory->GetBytecode(m_fileName.c_str(), m_entryName.c_str());
            if (byteCode.get()->size() == 0) {
                Log::Fatal(L"Failed to find a binary for shader:%s", m_fileName.c_str());
                return Status::ERROR_INTERNAL;
            }

            Native::GraphicsAPI::ComputeShader cs;
            cs.Init(((const char*)byteCode.get()->data()) + m_offset, m_size);

            m_cs_pso = std::make_unique<Native::GraphicsAPI::ComputePipelineState>();
            if (! m_cs_pso->Init(&pws->m_device, m_rootSig, &cs)) {
                m_cs_pso.reset();
                Log::Fatal(L"Failed to create PSO: %s", m_fileName.c_str());
                return Status::ERROR_INTERNAL;
            }
            if (m_shaderName.length() > 0)
                m_cs_pso->SetName(Native::DebugName(m_shaderName.c_str()));

            return Status::OK;
        }
        if (m_type == ShaderType::Enum::SHADER_RAY_GENERATION) {
            std::shared_ptr<Blob::IBlob> byteCode = factory->GetBytecode(m_fileName.c_str(), m_entryName.c_str());
            if (byteCode.get()->size() == 0) {
                Log::Fatal(L"Failed to find a binary for shader:%s", m_fileName.c_str());
                return Status::ERROR_INTERNAL;
            }
            std::shared_ptr<Blob::IBlob> libBlob = std::make_shared<Blob::SubBlob>(byteCode, m_offset, m_size);

            m_shaderTableRT = Native::ShaderTableRT::Init(pws, m_rootSig, libBlob);
            if (! m_shaderTableRT) {
                Log::Fatal(L"Failed to create rtPSO");
                return Status::ERROR_INTERNAL;
            }
            if (m_shaderName.length() > 0)
                m_shaderTableRT->m_rtPSO->SetName(m_shaderName);

            return Status::OK;
        }

        return Status::ERROR_INTERNAL;
    };

    KickstartRT_NativeLayer::GraphicsAPI::ComputePipelineState* ShaderDictEntry::GetCSPSO(KickstartRT_NativeLayer::PersistentWorkingSet* pws)
    {
        if (!m_cs_pso) {
            auto sts = CreateShaderObject(pws);
            if (sts != Status::OK) {
                Log::Fatal(L"Failed to create shader object.");
                return nullptr;
            }
        }
        return m_cs_pso.get();
    }

    KickstartRT_NativeLayer::ShaderTableRT* ShaderDictEntry::GetShaderTableRT(KickstartRT_NativeLayer::PersistentWorkingSet* pws, KickstartRT_NativeLayer::GraphicsAPI::CommandList *cmdList)
    {
        if (!m_shaderTableRT) {
            auto sts = CreateShaderObject(pws);
            if (sts != Status::OK) {
                Log::Fatal(L"Failed to create shader object.");
                return nullptr;
            }
        }
        if (m_shaderTableRT->m_needToCoyBuffer) {
            std::vector<KickstartRT_NativeLayer::ShaderTableRT*>	stArr;
            stArr.push_back(m_shaderTableRT.get());

            if (KickstartRT_NativeLayer::ShaderTableRT::BatchCopy(cmdList, stArr) != Status::OK) {
                Log::Fatal(L"Failed BatchCopy.");
                return nullptr;
            }
        }

        return m_shaderTableRT.get();
    }

    uint32_t ShaderDictEntry::CalcCRC()
    {
        CRC::CrcHash hasher;

        hasher.AddBytes((const char*)&m_type, sizeof(m_type));
        hasher.AddBytes((const char*)m_fileName.c_str(), sizeof(m_fileName.c_str()[0]) * m_fileName.length());
        hasher.AddBytes((const char*)m_entryName.c_str(), sizeof(m_entryName.c_str()[0]) * m_entryName.length());
        hasher.AddBytes((const char*)m_shaderName.c_str(), sizeof(m_shaderName.c_str()[0]) * m_shaderName.length());
        if (m_shaderMacro_CRC.has_value()) {
            uint32_t val = m_shaderMacro_CRC.value();
            hasher.AddBytes((const char*)&val, sizeof(val));
        }

        m_id_CRC = hasher.Get();

        return m_id_CRC.value();
    };

    std::pair<Status, ShaderDictEntry *> Factory::RegisterShader(std::unique_ptr<ShaderDictEntry> ent)
    {
        if (!ent->m_id_CRC.has_value()) {
            Log::Fatal(L"Failed to register shader since shader id hash wasn't set:%s", ent->m_fileName.c_str());
            return { Status::ERROR_INTERNAL, nullptr };
        }
        if (ent->m_fileName.length() == 0 || ent->m_entryName.length() == 0) {
            Log::Fatal(L"Null filename and/or entryname detected when registering a shader.");
            return { Status::ERROR_INTERNAL, nullptr };
        }

		auto findItr = m_shaderDict.find(ent->m_id_CRC.value());
		if (findItr != m_shaderDict.end()) {
			// already has registered, or CRC hash conflict.
            if (std::wcscmp(findItr->second->m_fileName.c_str(), ent->m_fileName.c_str()) != 0 ||
                std::wcscmp(findItr->second->m_entryName.c_str(), ent->m_entryName.c_str()) != 0) {
                Log::Fatal(L"Shader hash conflict happend.");
                return { Status::ERROR_INTERNAL, nullptr };
            }
        }
        else {
            auto [itr, sts] = m_shaderDict.insert({ ent->m_id_CRC.value(), std::move(ent) });
            if (!sts) {
                Log::Fatal(L"Failed to insert a shader: %s", ent->m_fileName.c_str());
                return { Status::ERROR_INTERNAL, nullptr };
            }
            findItr = itr;
        }

        return { Status::OK, findItr->second.get() };
    };
};
