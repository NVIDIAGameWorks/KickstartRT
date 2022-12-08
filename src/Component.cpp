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
#include <Component.h>

namespace KickstartRT_NativeLayer::Component
{
	template <typename T>
	Vector<T>::Vector()
	{
		m_data = new T[4];
		m_capacity = 4;
		m_size = 0;
	}

	template <typename T>
	Vector<T>::~Vector()
	{
		if (m_data != nullptr)
			delete[] m_data;
	}

	// move op
	template <typename T>
	Vector<T>& Vector<T>::operator=(Vector<T>&& other)
	{
		if (this == &other)
			return *this;

		delete[] m_data;
		m_data = other.m_data;
		m_size = other.m_size;
		m_capacity = other.m_capacity;

		other.m_data = nullptr;
		other.m_size = 0;
		other.m_capacity = 0;

		return *this;
	}

	template <typename T>
	void Vector<T>::reserve(size_t newCapacity)
	{
		if (newCapacity > m_capacity) {
			T* newData = new T[newCapacity];

			// use = op.
			for (size_t i = 0; i < m_size; ++i)
				newData[i] = m_data[i];

			delete[] m_data;
			m_data = newData;
			m_capacity = newCapacity;
		}
	}

	template <typename T>
	void Vector<T>::resize(size_t newSize)
	{
		if (newSize > m_size) {
			if (newSize > m_capacity)
				reserve(newSize);
			m_size = newSize;
		}
		else if (newSize < m_size) {
			// clear elements with = op.
			for (size_t i = newSize; i < m_size; ++i)
				m_data[i] = T();
			m_size = newSize;
		}
	}

	// copy op
	template <typename T>
	Vector<T>& Vector<T>::operator=(const Vector<T>& other)
	{
		if (this == &other)
			return *this;

		resize(other.m_size);
		for (size_t i = 0; i < other.m_size; ++i)
			m_data[i] = other.m_data[i];

		return *this;
	}

	template <typename T>
	void Vector<T>::push_back(T newElm)
	{
		if (m_size == m_capacity)
			reserve(m_capacity * 2);

		m_data[m_size++] = newElm;
	}
}

namespace KickstartRT_NativeLayer::Component
{
	template class Vector<BVHTask::GeometryInput::GeometryComponent>;
}
