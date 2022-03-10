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
#include "ShaderBlob.h"
#include "CRC.h"
#include "Platform.h"

#include <sstream>
#include <memory>
#include <locale>
#include <codecvt>
#include <cstring>
#include <optional>

namespace KickstartRT::ShaderBlob
{
	uint32_t GetShaderConstantCRC(const ShaderConstant* constants, uint32_t numConstants)
	{
		CRC::CrcHash hasher;
		const char equal = '=';
		const char semicolon = ';';
		for (uint32_t n = 0; n < numConstants; n++)
		{
			const ShaderConstant& constant = constants[n];
			hasher.AddBytes(constant.name, strlen(constant.name));
			hasher.AddBytes(&equal, 1);
			hasher.AddBytes(constant.value, strlen(constant.value));
			hasher.AddBytes(&semicolon, 1);
		}
		return hasher.Get();
	}
		
	bool FindPermutationInBlob(const void* blob, size_t blobSize, std::optional<uint32_t> shaderMacroCRC, size_t* pOffset, size_t* pSize)
	{
		if (!blob || blobSize < kBlobHeaderSize)
			return false;

		if (!pOffset || !pSize)
			return false;

		const void* blob_start = blob;
		*pOffset = (size_t)-1;
		*pSize = 0;

		if (memcmp(((const BlobHeader*)blob)->signature, kBlobSignature, kBlobSignatureSize) != 0)
		{
			if (! shaderMacroCRC.has_value())
			{
				*pOffset = 0;
				*pSize = blobSize;
				return true; // this blob is not a permutation blob, and no permutation is requested
			}
			else
			{
				return false; // this blob is not a permutation blob, but the caller requested a permutation
			}
		}

		blob = static_cast<const char*>(blob) + kBlobHeaderSize;
		blobSize -= kBlobHeaderSize;

		uint32_t defineHash;
		if (shaderMacroCRC.has_value())
			defineHash = shaderMacroCRC.value();
		else
			defineHash = 0; // not sure if it's fine to use zero for no-permutation..

		while (blobSize > sizeof(ShaderBlobEntry))
		{
			const ShaderBlobEntry* header = static_cast<const ShaderBlobEntry*>(blob);

			if (header->dataSize == 0)
				return false; // last header in the blob is empty

			if (blobSize < sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize)
				return false; // insufficient bytes in the blob, cannot continue

			if (header->defineHash == defineHash)
			{
				const char* binary = static_cast<const char*>(blob) + sizeof(ShaderBlobEntry) + header->hashKeySize;
				CRC::CrcHash dataHasher;
				dataHasher.AddBytes(binary, header->dataSize);
				if (dataHasher.Get() != header->dataCrc)
					return false; // crc mismatch, corrupted data in the blob

				*pOffset = binary - (const char *)blob_start;
				*pSize = header->dataSize;
				return true;
			}

			size_t offset = sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize;
			blob = static_cast<const char*>(blob) + offset;
			blobSize -= offset;
		}

		return false; // went through the blob, permutation not found
	}

	bool FindPermutationInBlob(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants, const void** pBinary, size_t* pSize)
	{
		if (!blob || blobSize < kBlobHeaderSize)
			return false;

		if (!pBinary || !pSize)
			return false;

		if (memcmp(((const BlobHeader*)blob)->signature, kBlobSignature, kBlobSignatureSize) != 0)
		{
			if (numConstants == 0)
			{
				*pBinary = blob;
				*pSize = blobSize;
				return true; // this blob is not a permutation blob, and no permutation is requested
			}
			else
			{
				return false; // this blob is not a permutation blob, but the caller requested a permutation
			}
		}

		blob = static_cast<const char*>(blob) + kBlobHeaderSize;
		blobSize -= kBlobHeaderSize;

		uint32_t defineHash = GetShaderConstantCRC(constants, numConstants);

		while (blobSize > sizeof(ShaderBlobEntry))
		{
			const ShaderBlobEntry* header = static_cast<const ShaderBlobEntry*>(blob);

			if (header->dataSize == 0)
				return false; // last header in the blob is empty

			if (blobSize < sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize)
				return false; // insufficient bytes in the blob, cannot continue

			if (header->defineHash == defineHash)
			{
				const char* binary = static_cast<const char*>(blob) + sizeof(ShaderBlobEntry) + header->hashKeySize;
				CRC::CrcHash dataHasher;
				dataHasher.AddBytes(binary, header->dataSize);
				if (dataHasher.Get() != header->dataCrc)
					return false; // crc mismatch, corrupted data in the blob

				*pBinary = binary;
				*pSize = header->dataSize;
				return true;
			}

			size_t offset = sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize;
			blob = static_cast<const char*>(blob) + offset;
			blobSize -= offset;
		}

		return false; // went through the blob, permutation not found
	}

	void EnumeratePermutationsInBlob(const void* blob, size_t blobSize, std::vector<std::string>& permutations)
	{
		if (!blob || blobSize < kBlobHeaderSize)
			return;

		if (memcmp(((const BlobHeader*)blob)->signature, kBlobSignature, kBlobSignatureSize) != 0)
			return;

		blob = static_cast<const char*>(blob) + kBlobHeaderSize;
		blobSize -= kBlobHeaderSize;

		while (blobSize > sizeof(ShaderBlobEntry))
		{
			const ShaderBlobEntry* header = static_cast<const ShaderBlobEntry*>(blob);

			if (header->dataSize == 0)
				return;

			if (blobSize < sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize)
				return;

			if (header->hashKeySize > 0)
			{
				std::string hashKey;
				hashKey.resize(header->hashKeySize);

				memcpy(&hashKey[0], static_cast<const char*>(blob) + sizeof(ShaderBlobEntry), header->hashKeySize);

				permutations.push_back(hashKey);
			}
			else
			{
				permutations.push_back("<default>");
			}

			size_t offset = sizeof(ShaderBlobEntry) + header->dataSize + header->hashKeySize;
			blob = static_cast<const char*>(blob) + offset;
			blobSize -= offset;
		}
	}

	std::wstring FormatShaderNotFoundMessage(const void* blob, size_t blobSize, std::optional<uint32_t> shaderMacroCRC)
	{
		if (shaderMacroCRC.has_value()) {
			std::stringstream ss;
			ShaderConstant	shC;

			shC.name = "Shader Macro CRC";
			ss << "0x" << std::hex << shaderMacroCRC.value();
			std::string valueStr = ss.str();
			shC.value = valueStr.c_str();

			return FormatShaderNotFoundMessage(blob, blobSize, &shC, 1);
		}
		else {
			return FormatShaderNotFoundMessage(blob, blobSize, nullptr, 0);
		}
	};

	std::wstring FormatShaderNotFoundMessage(const void* blob, size_t blobSize, const ShaderConstant* constants, uint32_t numConstants)
	{
		std::stringstream ss;
		ss << "Couldn't find the required shader permutation in the blob, or the blob is corrupted." << std::endl;
		ss << "Required permutation key: " << std::endl;

		if (numConstants)
		{
			for (uint32_t n = 0; n < numConstants; n++)
			{
				const ShaderConstant& constant = constants[n];

				ss << constant.name << "=" << constant.value << ";";
			}
		}
		else
		{
			ss << "<default>";
		}

		ss << std::endl;

		std::vector<std::string> permutations;
		EnumeratePermutationsInBlob(blob, blobSize, permutations);

		if (permutations.size() > 0)
		{
			ss << "Permutations available in the blob:" << std::endl;
			for (const std::string& key : permutations)
				ss << key.c_str() << std::endl;
		}
		else
		{
			ss << "No permutations found in the blob.";
		}
#if 0
		auto ToWideString = [](const std::string& src) {
			wchar_t wBuf[1024];
			int res = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, wBuf, 1024);

			if (res > 0)
				return std::wstring(wBuf);

			return std::wstring();
		};

		return ToWideString(ss.str());
#else
		std::string str = ss.str(); 
        return std::wstring(str.begin(), str.end());
#endif
	}

	Blob::Blob(void* data, size_t size)
		: m_data(data)
		, m_size(size)
	{
	}

	const void* Blob::data() const
	{
		return m_data;
	}

	size_t Blob::size() const
	{
		return m_size;
	}

	Blob::~Blob()
	{
		if (m_data)
		{
			free(m_data);
			m_data = nullptr;
		}

		m_size = 0;
	}

	SubBlob::SubBlob() :
		m_offset(0),
		m_size(0),
		m_parent(nullptr)
	{
	};

	SubBlob::SubBlob(std::shared_ptr<IBlob> parent, size_t offset, size_t size) :
		m_offset(offset),
		m_size(size),
		m_parent(parent)
	{
	};

	const void* SubBlob::data() const
	{
		return reinterpret_cast<void*>(reinterpret_cast<intptr_t>(m_parent->data()) + m_offset);
	}

	size_t SubBlob::size() const
	{
		return m_size;
	}

	SubBlob::~SubBlob()
	{
	}
}
