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
#include "WinResFS.h"
#include "Platform.h"
#include "Log.h"

#include <string>
#include <regex>
#include <sstream>
#include <assert.h>

#if !defined WIN32
// Linux can't use the windows resource files, so it uses a different system.
#include <KickstartRT_binary_resource.h>
#endif

#ifdef WIN32
namespace KickstartRT
{
    extern HINSTANCE g_ModuleHandle; // DLLMain
}
#endif

namespace KickstartRT::VirtualFS
{
    namespace Blob = KickstartRT::ShaderBlob;
    namespace fs = std::filesystem;
#ifdef WIN32
    static BOOL CALLBACK EnumResourcesCallback(HMODULE /*hModule*/, LPCWSTR /*lpType*/, LPWSTR lpName, LONG_PTR lParam)
    {
        if (!IS_INTRESOURCE(lpName))
        {
            auto pNames = (std::vector<std::wstring>*)lParam;
            pNames->push_back(lpName);
        }

        return true;
    }
#endif
    WinResFileSystem::WinResFileSystem(const void* hModule, const wchar_t* type)
        : m_hModule(hModule)
        , m_Type(type)
    {
#ifdef WIN32
        if (m_hModule == nullptr)
            m_hModule = KickstartRT::g_ModuleHandle;

        wchar_t mname[1024] = {};
        GetModuleFileNameW((HMODULE)m_hModule, mname, 1023);

        EnumResourceNamesW((HMODULE)m_hModule, m_Type.c_str(), EnumResourcesCallback, (LONG_PTR)&m_ResourceNames);
#else
        for (uint32_t ii = 0; ii < g_resource_count; ii++)
        {
            std::string s = g_resource_list[ii].name;
            m_ResourceNames.push_back(std::wstring(s.begin(), s.end()));
        }
#endif
    }

    bool WinResFileSystem::folderExists(const fs::path& /*name*/)
    {
        return false;
    }

    static inline void ltrim(std::wstring& s, int c)
    {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](int ch) {
            return ch != c;
        }));
    }
    bool WinResFileSystem::fileExists(const fs::path& name)
    {
#ifdef WIN32
        std::wstring nameString = name.lexically_normal().generic_string<wchar_t>();
        ltrim(nameString, L'/');

        HRSRC hResource = FindResourceW((HMODULE)m_hModule, nameString.c_str(), m_Type.c_str());

        return (hResource != nullptr);
#else
        std::wstring nameString = name.lexically_normal().generic_string<wchar_t>();
        ltrim(nameString, L'/');

        std::string str = std::string(nameString.begin(), nameString.end());
        if (findResourceSymbol(str.c_str()))
            return true;
        return false;
#endif
    }

    std::shared_ptr<Blob::IBlob> WinResFileSystem::readFile(const fs::path& name)
    {
#ifdef WIN32
        std::wstring nameString = name.lexically_normal().generic_string<wchar_t>();
        ltrim(nameString, L'/');

        HRSRC hResource = FindResourceW((HMODULE)m_hModule, nameString.c_str(), m_Type.c_str());

        if (hResource == nullptr)
            return nullptr;

        DWORD size = SizeofResource((HMODULE)m_hModule, hResource);
        if (size == 0)
        {
            // empty resource (can that really happen?)
            return std::make_shared<Blob::NonOwningBlob>(nullptr, 0);
        }

        HGLOBAL hGlobal = LoadResource((HMODULE)m_hModule, hResource);

        if (hGlobal == nullptr)
            return nullptr;

        void* pData = LockResource(hGlobal);
        return std::make_shared<Blob::NonOwningBlob>(pData, size);
#else
        std::wstring nameString = name.lexically_normal().generic_string<wchar_t>();
        ltrim(nameString, L'/');

        std::string str = std::string(nameString.begin(), nameString.end());
        const ResourceSymbol *sym = findResourceSymbol(str.c_str());
        if (sym == nullptr)
            return nullptr;

        if (sym->size == 0)
        {
            // empty resource (can that really happen?)
            return std::make_shared<Blob::NonOwningBlob>(nullptr, 0);
        }

        return std::make_shared<Blob::NonOwningBlob>(sym->start, sym->size);
#endif
    }

    bool WinResFileSystem::writeFile(const fs::path& name, const void* data, size_t size)
    {
        (void)name;
        (void)data;
        (void)size;
        return false; // unsupported
    }

    bool WinResFileSystem::enumerate(const fs::path& pattern, bool directories, std::vector<std::wstring>& results)
    {
        if (directories)
        {
            // directory info is not stored
            return false;
        }

        std::wstring patternString = pattern.lexically_normal().generic_string<wchar_t>();

        // Convert the pattern to a regex
        std::wostringstream ss;

        for (wchar_t c : patternString)
        {
            switch (c)
            {
            case L'?': ss << L"[^\\/]"; break;
            case L'*': ss << L"[^\\/]+"; break;
            case L'.': ss << L"\\."; break;
            default: ss << c; break;
            }
        }

        std::wregex regex(ss.str(), std::wregex::icase);

        for (const std::wstring& path : m_ResourceNames)
        {
            if (std::regex_match(path, regex))
            {
                results.push_back(path);
            }
        }

        return true;
    }

}
