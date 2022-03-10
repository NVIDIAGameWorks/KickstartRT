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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

#if !defined WIN32
#include <pthread.h>
#endif

namespace KickstartRT
{
	namespace OS
	{
		class SyncObject
		{
		public:
			SyncObject() : m_Signalled(false), m_Valid(false){}
			~SyncObject() {}

			enum {
				Infinite = 0xFFFFFFFF,
			};

			bool Init()
			{
				m_Valid = true;
				return true;
			}

			bool Cleanup()
			{
				m_Valid = false;
				m_CV.notify_all();
				return true;
			}

			bool WaitForSignal(uint32_t timeout = Infinite)
			{
				std::unique_lock<std::mutex> lock(m_Mutex);
				auto t = std::chrono::system_clock::now() + std::chrono::milliseconds{ timeout };
				m_CV.wait_until(lock, t, [&]() {return IsSignalled() || !IsValid(); });
				return IsSignalled();
			}

			bool Signal()
			{
				std::unique_lock<std::mutex> lock(m_Mutex);
				m_Signalled = true;
				lock.unlock();
				m_CV.notify_all();
				return true;
			}

			bool Reset()
			{
				std::unique_lock<std::mutex> lock(m_Mutex);
				m_Signalled = false;
				lock.unlock();
				return true;
			}

			bool IsValid()
			{
				return m_Valid;
			}

			bool IsSignalled()
			{
				return m_Signalled;
			}

		private:
			std::mutex m_Mutex;
			std::condition_variable m_CV;
			bool m_Signalled;
			std::atomic<bool> m_Valid;
		};

		inline uint64_t SetThreadAffinityMask(std::thread* pThread, uint64_t mask)
		{
#ifdef WIN32
			return ::SetThreadAffinityMask(pThread->native_handle(), mask);
#else
			cpu_set_t cpuset;
			CPU_ZERO(&cpuset);
			for (int j = 0; j < 64; j++)
				if (j & mask)
					CPU_SET(j, &cpuset);
			return (pthread_setaffinity_np(pThread->native_handle(), sizeof(cpu_set_t), &cpuset) == 0);
#endif
		}

		inline uint32_t SetThreadName(std::thread* pThread, std::wstring& name)
		{
#ifdef WIN32
			return ::SetThreadDescription(pThread->native_handle(), name.c_str());
#else
			std::string str(name.begin(), name.end());
			return pthread_setname_np(pThread->native_handle(), str.c_str());
#endif
		}
	}

}