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
#include <Log.h>
#include <ExecuteContext.h>
#include <stdarg.h>

namespace KickstartRT_NativeLayer::Log
{
    static constexpr size_t g_MessageBufferSize = 4096;
    static Callback_t      s_callback = nullptr;
    static Severity        s_minSeverity = Severity::Info;
    static bool            s_defaultMessageProcStatus = true;

    Status SetMinSeverity(Severity severity)
    {
        std::scoped_lock mtx(g_APIInterfaceMutex);

        s_minSeverity = severity;

        return Status::OK;
    };

    Status SetCallback(Callback_t func)
    {
        std::scoped_lock mtx(g_APIInterfaceMutex);

        s_callback = func;

        return Status::OK;
    };

    Status SetDefaultMessageProc(bool status)
    {
        std::scoped_lock mtx(g_APIInterfaceMutex);

        s_defaultMessageProcStatus = status;

        return Status::OK;
    };

    static void DefaultMessageProc(Severity severity, const wchar_t* message)
    {
        if (severity < s_minSeverity || (!s_defaultMessageProcStatus))
            return;

#if WIN32
        const wchar_t* severityText = L"";
        switch (severity)
        {
        case Severity::Info: severityText = L"INFO";  break;
        case Severity::Warning: severityText = L"WARNING"; break;
        case Severity::Error: severityText = L"ERROR"; break;
        case Severity::Fatal: severityText = L"FATAL"; break;
        default:
            break;
        }

        wchar_t buf[g_MessageBufferSize];
        swprintf(buf, std::size(buf), L"%s: %s\n", severityText, message);

#ifdef WIN32
        OutputDebugStringW(buf);
#endif

#if 0
        if (severity == Severity::Error)
        {
            MessageBoxW(0, buf, L"Error", MB_ICONERROR);
        }
#endif

        fwprintf(stderr, L"%s\n", buf);
#else
        // Linux fails to output wchars properly, so convert to normal char.
        std::string severityText;
        switch (severity)
        {
        case Severity::Info: severityText = "INFO";  break;
        case Severity::Warning: severityText = "WARNING"; break;
        case Severity::Error: severityText = "ERROR"; break;
        case Severity::Fatal: severityText = "FATAL"; break;
        default:
            break;
        }

        std::wstring ws = message;
        std::string s = std::string(ws.begin(), ws.end());
        fprintf(stderr, "%s: %s\n", severityText.c_str(), s.c_str());

#endif
        if (severity == Severity::Fatal)
            assert(false);
    }

    static void Message(Severity severity, const wchar_t* fmt, va_list args)
    {
        wchar_t buffer[g_MessageBufferSize];

        int len = vswprintf(buffer, std::size(buffer), fmt, args);

        if (len > 0) {
            if (s_callback && severity >= s_minSeverity)
                s_callback(severity, buffer, len);

            DefaultMessageProc(severity, buffer);
        }
    }

    void Message(Severity severity, const wchar_t* fmt...)
    {
        va_list args;
        va_start(args, fmt);
        Message(severity, fmt, args);
        va_end(args);
    }

    void Info(const wchar_t* fmt...)
    {
        va_list args;
        va_start(args, fmt);
        Message(Severity::Info, fmt, args);
        va_end(args);
    }

    void Warning(const wchar_t* fmt...)
    {
        va_list args;
        va_start(args, fmt);
        Message(Severity::Warning, fmt, args);
        va_end(args);
    }

    void Error(const wchar_t* fmt...)
    {
        va_list args;
        va_start(args, fmt);
        Message(Severity::Error, fmt, args);
        va_end(args);
    }

    void Fatal(const wchar_t* fmt...)
    {
        va_list args;
        va_start(args, fmt);
        Message(Severity::Fatal, fmt, args);
        va_end(args);
    }

    std::wstring ToWideString(const std::string& src)
    {
#if 1
        return std::wstring(src.begin(), src.end());
#else
        wchar_t wBuf[1024];
        int res = MultiByteToWideChar(CP_UTF8, 0, src.c_str(), -1, wBuf, 1024);

        if (res > 0)
            return std::wstring(wBuf);

        Error(L"Failed to convert string to wstring");
        return std::wstring();
#endif
    };
}
