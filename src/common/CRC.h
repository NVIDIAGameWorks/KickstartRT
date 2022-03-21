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

#include "Arch.h"
#include <stdint.h>
#ifndef ARCH_ARM
#include <nmmintrin.h>
#endif
#include <vector>

#ifdef _MSC_VER
#define CRC_H_FORCEINLINE __forceinline
#else
#define CRC_H_FORCEINLINE __attribute__((__always_inline__))
#endif

namespace KickstartRT::CRC {
	extern const bool CpuSupportsSSE42;
	extern const uint32_t CrcTable[];

	class CrcHash
	{
	private:
		uint32_t crc;
	public:
		CrcHash()
			: crc(~0u)
		{
		}

		uint32_t Get()
		{
			return ~crc;
		}
#ifndef ARCH_ARM
		template<size_t size> CRC_H_FORCEINLINE void AddBytesSSE42(const void* p)
		{
			static_assert(size % 4 == 0, "Size of hashable types must be multiple of 4");

			const uint32_t* data = (const uint32_t*)p;

			const size_t numIterations = size / sizeof(uint32_t);
			for (size_t i = 0; i < numIterations; i++)
			{
				crc = _mm_crc32_u32(crc, data[i]);
			}
		}
#endif
		CRC_H_FORCEINLINE void AddBytes(const char* p, size_t size)
		{
			for (size_t idx = 0; idx < size; idx++)
				crc = CrcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
		}

		template<typename T> void Add(const T& value)
		{
#ifndef ARCH_ARM
			if (CpuSupportsSSE42)
				AddBytesSSE42<sizeof(value)>((void*)&value);
			else
#endif
				AddBytes((char*)&value, sizeof(value));
		}

		void Add(const void* p, size_t size)
		{
#ifndef ARCH_ARM
			if (CpuSupportsSSE42)
			{
				uint32_t* data = (uint32_t*)p;
				const size_t numIterations = size / sizeof(uint32_t);
				for (size_t i = 0; i < numIterations; i++)
				{
					crc = _mm_crc32_u32(crc, data[i]);
				}

				if (size % sizeof(uint32_t))
				{
					AddBytes((char*)&data[numIterations], size % sizeof(uint32_t));
				}
			}
			else 
#endif
			{
				AddBytes((char*)p, size);
			}
		}

		template <typename T> void AddVector(const std::vector<T>& vec)
		{
			Add(vec.data(), vec.size() * sizeof(T));
		}
	};
};

#undef CRC_H_FORCEINLINE
