#include "app/crash.h"

#include <windows.h>

#include <dbghelp.h>

#include <cstdlib>
#include <cwchar>
#include <exception>
#include <iterator>

#include "common/log.h"
#include "common/names.h"
#include "common/unique_handle.h"

namespace pitot::app {
namespace {

// std::terminate、纯虚调用、CRT 非法参数时自己抛一个异常，统一走下面的过滤器
constexpr DWORD kFatalCode = 0xE0505400;  // 'PT' + 0

struct ModuleTraits {
    using pointer = HMODULE;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { FreeLibrary(h); }
};

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

// 只留最新的几份：文件名里是时间，按名字排序就是按时间排序
void PruneDumps(const wchar_t* dir) {
    wchar_t pattern[MAX_PATH];
    _snwprintf_s(pattern, _TRUNCATE, L"%s\\%s*.dmp", dir, names::kCrashDumpPrefix);
    wchar_t found[32][64]{};
    int count = 0;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        if (count < static_cast<int>(std::size(found)) && wcslen(data.cFileName) < std::size(found[0])) {
            wcscpy_s(found[count++], data.cFileName);
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    // 插入排序：最多 32 个名字
    for (int i = 1; i < count; ++i) {
        for (int j = i; j > 0 && wcscmp(found[j - 1], found[j]) > 0; --j) {
            wchar_t tmp[64];
            wcscpy_s(tmp, found[j]);
            wcscpy_s(found[j], found[j - 1]);
            wcscpy_s(found[j - 1], tmp);
        }
    }
    for (int i = 0; i + names::kCrashDumpsKept < count; ++i) {
        wchar_t path[MAX_PATH];
        _snwprintf_s(path, _TRUNCATE, L"%s\\%s", dir, found[i]);
        DeleteFileW(path);
    }
}

void WriteDump(EXCEPTION_POINTERS* info) {
    wchar_t dir[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 64) {
        return;
    }
    wcscat_s(dir, L"\\");
    wcscat_s(dir, names::kAppDataFolder);
    CreateDirectoryW(dir, nullptr);
    SYSTEMTIME t{};
    GetLocalTime(&t);
    wchar_t path[MAX_PATH];
    _snwprintf_s(path, _TRUNCATE, L"%s\\%s%04u%02u%02u-%02u%02u%02u-%lu.dmp", dir, names::kCrashDumpPrefix, t.wYear,
                 t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());

    // dbghelp 只在崩溃时加载，平时不占内存
    unique_handle_t<ModuleTraits> dbghelp(LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
    const auto write = dbghelp ? reinterpret_cast<MiniDumpWriteDumpFn>(
                                     reinterpret_cast<void*>(GetProcAddress(dbghelp.get(), "MiniDumpWriteDump")))
                               : nullptr;
    if (!write) {
        log::Write(L"crash: dbghelp unavailable, no dump");
        return;
    }
    unique_file_handle file(
        CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        log::Write(L"crash: cannot create %s error=%lu", path, GetLastError());
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file.get(), type, info ? &exception : nullptr,
                          nullptr, nullptr);
    file.reset();
    log::Write(L"crash: dump %s ok=%d", path, ok);
    PruneDumps(dir);
}

LONG WINAPI Filter(EXCEPTION_POINTERS* info) {
    // 只处理第一次：转储过程中再出错就直接结束
    static volatile LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    const EXCEPTION_RECORD* record = info ? info->ExceptionRecord : nullptr;
    const DWORD code = record ? record->ExceptionCode : 0;
    void* address = record ? record->ExceptionAddress : nullptr;
    // 出错地址落在哪个模块：模块名 + 偏移，拿着同一版本的 PDB 就能定位
    wchar_t module[MAX_PATH] = L"?";
    HMODULE base = nullptr;
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      static_cast<LPCWSTR>(address), &base)) {
        GetModuleFileNameW(base, module, MAX_PATH);
    }
    const wchar_t* name = wcsrchr(module, L'\\');
    log::Write(L"crash: exception 0x%08lX at %p (%s+0x%llX) thread %lu", code, address, name ? name + 1 : module,
               base ? static_cast<unsigned long long>(static_cast<const char*>(address) -
                                                      reinterpret_cast<const char*>(base))
                    : 0ULL,
               GetCurrentThreadId());
    WriteDump(info);
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void Fatal(const wchar_t* why) {
    log::Write(L"crash: %s", why);
    RaiseException(kFatalCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    std::abort();
}

void OnTerminate() { Fatal(L"std::terminate"); }

void OnPureCall() { Fatal(L"pure virtual call"); }

void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    Fatal(L"invalid CRT parameter");
}

}  // namespace

void InstallCrashHandler() {
    SetUnhandledExceptionFilter(Filter);
    std::set_terminate(OnTerminate);
    _set_purecall_handler(OnPureCall);
    _set_invalid_parameter_handler(OnInvalidParameter);
    // 不弹 Windows 错误报告的对话框：Pitot 挂在任务栏里，对话框会盖住用户的东西
    SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX);
}

}  // namespace pitot::app
