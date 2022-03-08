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

template<typename PType, typename HType>
inline PType* ToPtr_s(HType handle)
{
	static_assert(sizeof(PType*) == sizeof(uint64_t));
	static_assert(sizeof(HType) == sizeof(uint64_t));
	static_assert(sizeof(PType::m_id) == sizeof(uint64_t));

	static_assert(((uint64_t)0xFFFF'FFFF'FFFF'FFFFull) >> 1 == (uint64_t)0x7FFF'FFFF'FFFF'FFFF);
	static_assert(((int64_t)0xFFFF'FFFF'FFFF'FFFFull) >> 1 == (int64_t)0xFFFF'FFFF'FFFF'FFFF);

	constexpr int HandleIDBits = 14; // upper 14 bit is used as a handle id space.
	constexpr int64_t AddressMask = static_cast<int64_t>(0xFFFF'FFFF'FFFF'FFFFull >> HandleIDBits);
	constexpr uint64_t IDMask = static_cast<uint64_t>(~AddressMask);

	// Sign extend first to make the pointer canonical
	PType* p = reinterpret_cast<PType*>(((static_cast<int64_t>(handle) & AddressMask) << HandleIDBits) >> HandleIDBits);
	if (p->m_id != (static_cast<uint64_t>(handle) & IDMask))
		return nullptr;

	return p;
};

template<typename PType, typename HType>
inline HType ToHandle_s(PType* p)
{
	static_assert(sizeof(PType*) == sizeof(uint64_t));
	static_assert(sizeof(HType) == sizeof(uint64_t));
	static_assert(sizeof(PType::m_id) == sizeof(uint64_t));

	constexpr int HandleIDBits = 14; // upper 14 bit is used as a handle id space.
	constexpr uint64_t AddressMask = 0xFFFF'FFFF'FFFF'FFFFull >> HandleIDBits;
	constexpr uint64_t AddressMask_N1 = ~(0xFFFF'FFFF'FFFF'FFFFull >> (HandleIDBits + 1));
	(void)(AddressMask_N1);

	// Upper (14+1) bits need to be all set or zero.
	assert(
		(reinterpret_cast<uint64_t>(p) & AddressMask_N1) == 0 ||
		(reinterpret_cast<uint64_t>(p) & AddressMask_N1) == AddressMask_N1);

	return static_cast<HType>((reinterpret_cast<uint64_t>(p) & AddressMask) | p->m_id);
};

