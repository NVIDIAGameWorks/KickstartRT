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
#include <ResourceLogger.h>

#include <filesystem>
#include <fstream>

namespace KickstartRT_NativeLayer
{
	static const wchar_t* KindToStr(size_t kind)
	{
		std::array<const wchar_t*, (size_t)ResourceLogger::ResourceKind::e_Num_Kinds> kindToStr =
		{
			L"VertexTemporary_SharedBlock",
			L"VertexTemporary_SharedEntry",
			L"VertexPersistent_SharedBlock",
			L"VertexPersistent_SharedEntry",
			L"DirectLightingCache_SharedBlock",
			L"DirectLightingCache_SharedEntry",
			L"TLAS",
			L"Other",
			L"Counter_SharedBlock",
			L"Counter_SharedEntry",
			L"Readback_SharedBlock",
			L"Readback_SharedEntry",
			L"BLASTemporary_SharedBlock",
			L"BLASTemporary_SharedEntry",
			L"BLASPermanent_SharedBlock",
			L"BLASPermanent_SharedEntry",
			L"BLASScratchTemp_SharedBlock",
			L"BLASScratchTemp_SharedEntry",
			L"BLASScratchPerm_SharedBlock",
			L"BLASScratchPerm_SharedEntry",
			L"DenoiserTemp_SharedEntry",
			L"DenoiserPerm_SharedEntry",
		};

		return kindToStr[kind];
	}

	void ResourceLogger::CheckLeaks()
	{
		auto& a = m_allocationInfo;

		bool foundLeaks = false;
		for (size_t i = 0; i < (size_t)ResourceLogger::ResourceKind::e_Num_Kinds; ++i) {
			if (a.m_numResources[i] > 0 || a.m_totalRequestedBytes[i] > 0)
				foundLeaks = true;
		}
		if (foundLeaks) {
			Log::Info(L"Found resource leaks.");
			for (size_t i = 0; i < (size_t)ResourceLogger::ResourceKind::e_Num_Kinds; ++i) {
				Log::Info(L"[%d][%s]: Num: %d TotalBytes: %d", i, KindToStr(i), a.m_numResources[i], a.m_totalRequestedBytes[i]);
			}
			Log::Fatal(L"Found resource leaks.");
		}
	};

	void ResourceLogger::LogResource(uint64_t frameIndex)
	{
		if (!m_isLogging)
			return;

		m_frameLogs.push_back({ frameIndex, m_allocationInfo });

		if (m_frameLogs.size() < m_logFlushFrames)
			return;

		FlushLog();
	}

	void ResourceLogger::FlushLog()
	{
		if (!m_isLogging)
			return;

		std::filesystem::path logPath(m_logFilePath);
		auto ext = logPath.extension();
		wchar_t flushTimesStr[256];
		swprintf(flushTimesStr, 256, L"%03d%s", (uint32_t)m_flushTimes, ext.c_str());
		logPath.replace_extension(flushTimesStr);

		std::wofstream of;

		of.open(logPath, std::ios::out);
		if (of.is_open()) {

			of << L"Total Requested MegaBytes" << std::endl;
			of << L"FrameIndex,";
			for (size_t i = 0; i < (size_t)ResourceLogger::ResourceKind::e_Num_Kinds; ++i) {
				of << KindToStr(i) << ",";
			}
			of << std::endl;

			for (auto&& l : m_frameLogs) {
				uint64_t fi = l.first;
				auto& a = l.second;

				of << fi << ",";
				for (size_t i = 0; i < (size_t)ResourceLogger::ResourceKind::e_Num_Kinds; ++i) {
					of << (double)a.m_totalRequestedBytes[i]/(1024.*1024) << ",";
				}
				of << std::endl;
			}

			of << L"Num Resource Allocations" << std::endl;
			of << L"FrameIndex,";
			for (size_t i = 0; i < (size_t)ResourceLogger::ResourceKind::e_Num_Kinds; ++i) {
				of << KindToStr(i) << ",";
			}
			of << std::endl;

			for (auto&& l : m_frameLogs) {
				uint64_t fi = l.first;
				auto& a = l.second;

				of << fi << ",";
				for (size_t i = 0; i < (size_t)ResourceLogger::ResourceKind::e_Num_Kinds; ++i) {
					of << a.m_numResources[i] << ",";
				}
				of << std::endl;
			}
		}
		of.close();

		m_frameLogs.clear();
		++m_flushTimes;
	}

	Status ResourceLogger::GetResourceAllocations(KickstartRT::ResourceAllocations* retAllocation)
	{
		if (retAllocation == nullptr)
			return Status::ERROR_INVALID_PARAM;

		*retAllocation = m_allocationInfo;

		return Status::OK;
	}

	Status ResourceLogger::BeginLoggingResourceAllocations(const wchar_t* filePath)
	{
		std::filesystem::path logPath(filePath);

		auto dir = logPath.parent_path();
		if (! std::filesystem::exists(dir))
			return Status::ERROR_INVALID_PARAM;

		auto stem = logPath.stem();
		if (stem.empty())
			return Status::ERROR_INVALID_PARAM;

		auto ext = logPath.extension();
		if (ext.empty())
			logPath.replace_extension(L"csv");

		m_logFilePath = logPath.wstring();
		m_isLogging = true;
		m_flushTimes = 0;
		m_frameLogs.clear();

		return Status::OK;
	}

	Status ResourceLogger::EndLoggingResourceAllocations()
	{
		if (!m_isLogging)
			return Status::ERROR_INVALID_CALL_FOR_THE_CURRENT_PROCESSING_STAGE;

		FlushLog();

		m_isLogging = false;
		m_flushTimes = 0;
		m_logFilePath.clear();

		return Status::OK;
	}
};
