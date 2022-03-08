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
#include "common/ShaderBlob.h"

#include <stddef.h>
#include <memory>
#include <string>
#include <filesystem>


namespace KickstartRT::VirtualFS
{
    class IFileSystem
    {
    public:
        virtual bool folderExists(const std::filesystem::path& name) = 0;
        virtual bool fileExists(const std::filesystem::path& name) = 0;
        virtual std::shared_ptr<ShaderBlob::IBlob> readFile(const std::filesystem::path& name) = 0;
        virtual bool writeFile(const std::filesystem::path& name, const void* data, size_t size) = 0;
        virtual bool enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::wstring>& results) = 0;
    };

    class NativeFileSystem : public IFileSystem
    {
    public:
        virtual bool folderExists(const std::filesystem::path& name) override;
        virtual bool fileExists(const std::filesystem::path& name) override;
        virtual std::shared_ptr<ShaderBlob::IBlob> readFile(const std::filesystem::path& name) override;
        virtual bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        virtual bool enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::wstring>& results) override;
    };

    class RelativeFileSystem : public IFileSystem
    {
    private:
        std::shared_ptr<IFileSystem> m_Parent;
        std::filesystem::path m_BasePath;
    public:
        RelativeFileSystem(std::shared_ptr<IFileSystem> parent, const std::filesystem::path& basePath);

        std::filesystem::path const& GetBasePath() const { return m_BasePath; }

        virtual bool folderExists(const std::filesystem::path& name) override;
        virtual bool fileExists(const std::filesystem::path& name) override;
        virtual std::shared_ptr<ShaderBlob::IBlob> readFile(const std::filesystem::path& name) override;
        virtual bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        virtual bool enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::wstring>& results) override;
    };

    class RootFileSystem : public IFileSystem
    {
    private:
        std::vector<std::pair<std::wstring, std::shared_ptr<IFileSystem>>> m_MountPoints;

        bool findMountPoint(const std::filesystem::path& path, std::filesystem::path* pRelativePath, IFileSystem** ppFS);
    public:
        void mount(const std::filesystem::path& path, std::shared_ptr<IFileSystem> fs);
        void mount(const std::filesystem::path& path, const std::filesystem::path& nativePath);
        bool unmount(const std::filesystem::path& path);

        virtual bool folderExists(const std::filesystem::path& name) override;
        virtual bool fileExists(const std::filesystem::path& name) override;
        virtual std::shared_ptr<ShaderBlob::IBlob> readFile(const std::filesystem::path& name) override;
        virtual bool writeFile(const std::filesystem::path& name, const void* data, size_t size) override;
        virtual bool enumerate(const std::filesystem::path& pattern, bool directories, std::vector<std::wstring>& wresults) override;
    };

};
