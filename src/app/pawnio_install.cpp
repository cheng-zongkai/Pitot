#include "app/pawnio_install.h"

#include <softpub.h>
#include <urlmon.h>
#include <wintrust.h>

#include "common/log.h"
#include "common/names.h"
#include "common/unique_handle.h"
#include "config/config.h"
#include "config/paths.h"
#include "ui/strings.h"

namespace pitot::app {
namespace {

struct service_traits {
    using pointer = SC_HANDLE;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { CloseServiceHandle(h); }
};
using unique_service = unique_handle_t<service_traits>;

bool DriverAvailable() {
    unique_file_handle device(CreateFileW(names::kPawnIoDevice, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr));
    return static_cast<bool>(device);
}

bool Install() {
    wchar_t directory[MAX_PATH]{};
    wchar_t file[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, directory);
    if (!length || length >= MAX_PATH || !GetTempFileNameW(directory, L"pio", 0, file)) {
        return false;
    }
    const HRESULT downloaded = URLDownloadToFileW(nullptr,
        L"https://github.com/namazso/PawnIO.Setup/releases/latest/download/PawnIO_setup.exe",
        file, 0, nullptr);
    bool installed = false;
    if (SUCCEEDED(downloaded)) {
        // 验签至安装结束期间禁止改写或替换下载文件。
        unique_file_handle locked(CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                              OPEN_EXISTING, 0, nullptr));
        if (locked) {
            WINTRUST_FILE_INFO info{};
            info.cbStruct = sizeof(info);
            info.pcwszFilePath = file;
            info.hFile = locked.get();
            WINTRUST_DATA trust{};
            trust.cbStruct = sizeof(trust);
            trust.dwUIChoice = WTD_UI_NONE;
            trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
            trust.dwUnionChoice = WTD_CHOICE_FILE;
            trust.pFile = &info;
            trust.dwStateAction = WTD_STATEACTION_VERIFY;
            GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
            const LONG verified = WinVerifyTrust(nullptr, &action, &trust);
            trust.dwStateAction = WTD_STATEACTION_CLOSE;
            WinVerifyTrust(nullptr, &action, &trust);
            if (verified == ERROR_SUCCESS) {
                STARTUPINFOW startup{sizeof(startup)};
                PROCESS_INFORMATION pi{};
                wchar_t command[MAX_PATH + 4]{};
                _snwprintf_s(command, _TRUNCATE, L"\"%s\"", file);
                if (CreateProcessW(file, command, nullptr, nullptr, FALSE, 0, nullptr, directory, &startup, &pi)) {
                    unique_handle process(pi.hProcess);
                    unique_handle thread(pi.hThread);
                    AllowSetForegroundWindow(pi.dwProcessId);
                    if (WaitForSingleObject(process.get(), INFINITE) == WAIT_OBJECT_0) {
                        DWORD code = 0;
                        installed = GetExitCodeProcess(process.get(), &code) && code == 0 && DriverAvailable();
                        log::Write(L"pawnio: installer exit=%lu available=%d", code, installed);
                    }
                }
            } else {
                log::Write(L"pawnio: installer signature error=0x%08lX", static_cast<DWORD>(verified));
            }
        }
    } else {
        log::Write(L"pawnio: download failed 0x%08lX", static_cast<DWORD>(downloaded));
    }
    DeleteFileW(file);
    return installed;
}

}  // namespace

bool LaunchPawnIoInstall(bool only_if_missing) {
    wchar_t exe[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (!length || length >= MAX_PATH) {
        return false;
    }
    wchar_t command[MAX_PATH + 64]{};
    _snwprintf_s(command, _TRUNCATE, L"\"%s\" %s", exe,
                  only_if_missing ? names::kOfferPawnIoArg : names::kInstallPawnIoArg);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &pi)) {
        log::Write(L"pawnio: cannot start installer helper %lu", GetLastError());
        return false;
    }
    unique_handle process(pi.hProcess);
    unique_handle thread(pi.hThread);
    AllowSetForegroundWindow(pi.dwProcessId);
    return true;
}

int RunPawnIoInstall(bool only_if_missing) {
    unique_handle mutex(CreateMutexW(nullptr, FALSE, names::kPawnIoInstallMutex));
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }
    if (only_if_missing) {
        unique_service manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
        if (!manager) {
            return 0;
        }
        unique_service service(OpenServiceW(manager.get(), names::kPawnIoService, SERVICE_QUERY_STATUS));
        if (service || GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) {
            return 0;
        }
    }
    wchar_t directory[MAX_PATH]{}, file[MAX_PATH]{};
    const config::Config config = config::GetConfigPaths(directory, file) ? config::LoadConfig(file) : config::Config{};
    ui::SetLanguage(ui::ResolveLanguage(config.general.language));
    const auto& s = ui::Str();
    if (DriverAvailable()) {
        MessageBoxW(nullptr, s.kPawnIoInstalled, s.kSetPawnIo, MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    if (MessageBoxW(nullptr, s.kPawnIoPrompt, s.kSetPawnIo,
                    MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION | MB_SETFOREGROUND) != IDYES) {
        return 0;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool installed = SUCCEEDED(com) && Install();
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    MessageBoxW(nullptr, installed ? s.kPawnIoInstalled : s.kPawnIoFailed, s.kSetPawnIo,
                MB_OK | (installed ? MB_ICONINFORMATION : MB_ICONWARNING));
    return installed ? 0 : 1;
}

}  // namespace pitot::app
