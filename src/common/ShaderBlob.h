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
#include "common/ShaderBlobEntry.h"

#include <vector>
#include <string>
#include <memory>
#include <optional>

namespace KickstartRT::ShaderBlob {
	struct ShaderConstant
	{
		const char* name;
		const char* value;
	};

	uint32_t GetShaderConstantCRC(const ShaderConstant* constants, uint32_t numConstants);

	bool FindPermutationInBlob(const void* blob, size_t blobSize, std::optional<uint32_t> shaderMacroCRC, size_t* offset, size_t* pSize);
	bool FindPermutationInBlob(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, const void** pBinary, size_t* pSize);
	void EnumeratePermutationsInBlob(const void* blob, size_t blobSize, std::vector<std::string>& permutations);
	std::wstring FormatShaderNotFoundMessage(const void* blob, size_t blobSize, std::optional<uint32_t> shaderMacroCRC);
	std::wstring FormatShaderNotFoundMessage(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants);

	class IBlob
	{
	public:
		virtual ~IBlob() { }
		virtual const void* data() const = 0;
		virtual size_t size() const = 0;
	};

	class Blob : public IBlob
	{
	private:
		void* m_data;
		size_t m_size;

	public:
		Blob(void* data, size_t size);
		virtual ~Blob() override;
		virtual const void* data() const override;
		virtual size_t size() const override;
	};

	class SubBlob : public IBlob
	{
	private:
		const size_t m_offset;
		const size_t m_size;
		std::shared_ptr<IBlob> m_parent;

	public:
		SubBlob();
		SubBlob(std::shared_ptr<IBlob> parent, size_t offset, size_t size);
		virtual ~SubBlob() override;
		virtual const void* data() const override;
		virtual size_t size() const override;
	};

	class NonOwningBlob : public IBlob
	{
	private:
		void* m_data;
		size_t m_size;

	public:
		NonOwningBlob(void* data, size_t size) : m_data(data), m_size(size) { }
		virtual const void* data() const override { return m_data; }
		virtual size_t size() const override { return m_size; }
	};
};
