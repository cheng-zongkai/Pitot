#include "common/log.h"

#include <windows.h>

#include <psapi.h>

#include <cstdarg>
#include <cstdio>
#include <iterator>

#include "common/names.h"
#include "common/unique_handle.h"
#include "config/paths.h"

namespace pitot::log {
namespace {

unique_file_handle g_file;

}  // namespace

void Init() {
    wchar_t path[MAX_PATH];
    if (!config::GetLocalDataPath(names::kLogFile, path)) {
        return;
    }
    g_file.reset(CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
}

void Write(const wchar_t* format, ...) {
    if (!g_file) {
        return;
    }

    wchar_t text[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    const int prefix = swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] ", t.wYear, t.wMonth,
                                  t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                                  GetCurrentThreadId());
    if (prefix < 0) {
        return;
    }

    // 留一个位置给换行
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text + prefix, std::size(text) - prefix - 1, _TRUNCATE, format, args);
    va_end(args);

    size_t len = wcslen(text);
    text[len++] = L'\n';

    char utf8[3 * std::size(text)];
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(len), utf8, static_cast<int>(sizeof(utf8)),
                                          nullptr, nullptr);
    if (bytes > 0) {
        DWORD written = 0;
        WriteFile(g_file.get(), utf8, static_cast<DWORD>(bytes), &written, nullptr);
    }
}

void Memory(const wchar_t* label) {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        Write(L"memory: %s private=%.2f MB working_set=%.2f MB", label, counters.PrivateUsage / 1048576.0,
              counters.WorkingSetSize / 1048576.0);
    }
}

}  // namespace pitot::log
