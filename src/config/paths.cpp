#include "config/paths.h"

#include <cwchar>

#include "common/names.h"

namespace pitot::config {

bool GetConfigPaths(wchar_t (&dir)[MAX_PATH], wchar_t (&file)[MAX_PATH]) {
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return false;
    }
    if (_snwprintf_s(dir + n, MAX_PATH - n, _TRUNCATE, L"\\%s", names::kAppDataFolder) < 0 ||
        _snwprintf_s(file, _TRUNCATE, L"%s\\%s", dir, names::kConfigFile) < 0) {
        return false;
    }
    return CreateDirectoryW(dir, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool GetLocalDataPath(const wchar_t* name, wchar_t (&file)[MAX_PATH]) {
    wchar_t dir[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH ||
        _snwprintf_s(dir + n, MAX_PATH - n, _TRUNCATE, L"\\%s", names::kAppDataFolder) < 0 ||
        _snwprintf_s(file, _TRUNCATE, L"%s\\%s", dir, name) < 0) {
        return false;
    }
    return CreateDirectoryW(dir, nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

}  // namespace pitot::config
