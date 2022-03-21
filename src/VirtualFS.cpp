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
#include "Platform.h"
#include "Log.h"
#include "VirtualFS.h"

#include <fstream>
#include <assert.h>
#include <algorithm>
#include <limits>
#ifndef WIN32
#include <glob.h>
#endif

namespace KickstartRT::VirtualFS
{
    namespace Log = KickstartRT_NativeLayer::Log;
    namespace Blob = KickstartRT::ShaderBlob;

    bool NativeFileSystem::folderExists(const std::filesystem::path& name)
    {
        return std::filesystem::exists(name) && std::filesystem::is_directory(name);
    }

    bool NativeFileSystem::fileExists(const std::filesystem::path& name)
    {
        return std::filesystem::exists(name) && std::filesystem::is_regular_file(name);
    }

    std::shared_ptr<Blob::IBlob> NativeFileSystem::readFile(const std::filesystem::path& name)
    {
        // TODO: better error reporting

        std::ifstream file(name, std::ios::binary);

        if (!file.is_open())
        {
            // file does not exist or is locked
            return nullptr;
        }

        file.seekg(0, std::ios::end);
        uint64_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
        {
            // file larger than size_t
            assert(false);
            return nullptr;
        }

        char* data = static_cast<char*>(malloc(size));

        if (data == nullptr)
        {
            // out of memory
            assert(false);
            return nullptr;
        }

        file.read(data, size);

        if (!file.good())
        {
            // reading error
            assert(false);
            return nullptr;
        }

        return std::make_shared<Blob::Blob>(data, size);
    }

    bool NativeFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
    {
        // TODO: better error reporting

        std::ofstream file(name, std::ios::binary);

        if (!file.is_open())
        {
            // file does not exist or is locked
            return false;
        }

        if (size > 0)
        {
            file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        }

        if (!file.good())
        {
            // writing error
            return false;
        }

        return true;
    }

    bool NativeFileSystem::enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::wstring>& results)
    {
#ifdef WIN32
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW(pattern.generic_string<wchar_t>().c_str(), &findData);

        if (hFind == INVALID_HANDLE_VALUE)
            return false;

        do
        {
            bool isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            bool isDot = wcscmp(findData.cFileName, L".") == 0;
            bool isDotDot = wcscmp(findData.cFileName, L"..") == 0;

            if ((isDirectory == directories) && !isDot && !isDotDot)
            {
                results.push_back(findData.cFileName);
            }
        } while (FindNextFileW(hFind, &findData) != 0);

        FindClose(hFind);

        return true;
#else
        assert(0);
        glob_t glob_result;
        glob(pattern.generic_string<char>().c_str(), GLOB_TILDE, NULL, &glob_result);

        for (size_t ii = 0; ii < glob_result.gl_pathc; ii++)
        {
            std::string s = glob_result.gl_pathv[ii];
            std::wstring ws = std::wstring(s.begin(), s.end());

            bool isDirectory = std::filesystem::is_directory(ws);
            bool isDot = (s == ".");
            bool isDotDot = (s == "..");

            if ((isDirectory == directories) && !isDot && !isDotDot)
                results.push_back(ws);
        }
        return true;
#endif
    }

    RelativeFileSystem::RelativeFileSystem(std::shared_ptr<IFileSystem> parent, const std::filesystem::path& basePath)
        : m_Parent(parent)
        , m_BasePath(basePath.lexically_normal())
    {
    }

    bool RelativeFileSystem::folderExists(const std::filesystem::path& name)
    {
        return m_Parent->folderExists(m_BasePath / name.relative_path());
    }

    bool RelativeFileSystem::fileExists(const std::filesystem::path& name)
    {
        return m_Parent->fileExists(m_BasePath / name.relative_path());
    }

    std::shared_ptr<Blob::IBlob> RelativeFileSystem::readFile(const std::filesystem::path& name)
    {
        return m_Parent->readFile(m_BasePath / name.relative_path());
    }

    bool RelativeFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
    {
        return m_Parent->writeFile(m_BasePath / name.relative_path(), data, size);
    }

    bool RelativeFileSystem::enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::wstring>& results)
    {
        return m_Parent->enumerate(m_BasePath / pattern.relative_path(), directories, results);
    }

    void RootFileSystem::mount(const std::filesystem::path& path, std::shared_ptr<IFileSystem> fs)
    {
        if (findMountPoint(path, nullptr, nullptr))
        {
            Log::Error(L"Cannot mount a filesystem at %s: there is another FS that includes this path", path.c_str());
            return;
        }

        m_MountPoints.push_back(std::make_pair(path.lexically_normal().generic_string<wchar_t>(), fs));
    }

    void RootFileSystem::mount(const std::filesystem::path& path, const std::filesystem::path& nativePath)
    {
        mount(path, std::make_shared<RelativeFileSystem>(std::make_shared<NativeFileSystem>(), nativePath));
    }

    bool RootFileSystem::unmount(const std::filesystem::path& path)
    {
        std::wstring spath = path.lexically_normal().generic_string<wchar_t>();

        for (size_t index = 0; index < m_MountPoints.size(); index++)
        {
            if (m_MountPoints[index].first == spath)
            {
                m_MountPoints.erase(m_MountPoints.begin() + index);
                return true;
            }
        }

        return false;
    }

    bool RootFileSystem::findMountPoint(const std::filesystem::path& path, std::filesystem::path* pRelativePath, IFileSystem** ppFS)
    {
        std::wstring spath = path.lexically_normal().generic_string<wchar_t>();

        for (auto it : m_MountPoints)
        {
            if (spath.find(it.first, 0) == 0 && ((spath.length() == it.first.length()) || (spath[it.first.length()] == '/')))
            {
                if (pRelativePath)
                {
                    std::wstring relative = spath.substr(it.first.size());
                    *pRelativePath = relative;
                }

                if (ppFS)
                {
                    *ppFS = it.second.get();
                }

                return true;
            }
        }

        return false;
    }

    bool RootFileSystem::folderExists(const std::filesystem::path& name)
    {
        std::filesystem::path relativePath;
        IFileSystem* fs = nullptr;

        if (findMountPoint(name, &relativePath, &fs))
        {
            return fs->folderExists(relativePath);
        }

        return false;
    }

    bool RootFileSystem::fileExists(const std::filesystem::path& name)
    {
        std::filesystem::path relativePath;
        IFileSystem* fs = nullptr;

        if (findMountPoint(name, &relativePath, &fs))
        {
            return fs->fileExists(relativePath);
        }

        return false;
    }

    std::shared_ptr<Blob::IBlob> RootFileSystem::readFile(const std::filesystem::path& name)
    {
        std::filesystem::path relativePath;
        IFileSystem* fs = nullptr;

        if (findMountPoint(name, &relativePath, &fs))
        {
            return fs->readFile(relativePath);
        }

        return nullptr;
    }

    bool RootFileSystem::writeFile(const std::filesystem::path& name, const void* data, size_t size)
    {
        std::filesystem::path relativePath;
        IFileSystem* fs = nullptr;

        if (findMountPoint(name, &relativePath, &fs))
        {
            return fs->writeFile(relativePath, data, size);
        }

        return false;
    }

    bool RootFileSystem::enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::wstring>& results)
    {
        std::filesystem::path relativePath;
        IFileSystem* fs = nullptr;

        if (findMountPoint(pattern, &relativePath, &fs))
        {
            return fs->enumerate(relativePath, directories, results);
        }

        return false;
    };
};
