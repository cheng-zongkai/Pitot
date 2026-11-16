#include "ui/system_theme.h"

#include <thread>

#include "common/log.h"
#include "common/messages.h"

namespace pitot::ui {
namespace {

constexpr wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

bool WritePersonalizeDword(const wchar_t* name, DWORD value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status =
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

// 配置文件里的路径允许写 %USERPROFILE% 这类变量；展开失败就按原样用
std::wstring ExpandEnvironment(const wchar_t* text) {
    const DWORD needed = ExpandEnvironmentStringsW(text, nullptr, 0);
    if (needed == 0) {
        return text;
    }
    std::wstring expanded(needed, L'\0');
    const DWORD written = ExpandEnvironmentStringsW(text, expanded.data(), needed);
    if (written == 0 || written > needed) {
        return text;
    }
    expanded.resize(written - 1);
    return expanded;
}

}  // namespace

bool SystemIsDarkTheme() {
    DWORD light = 1;
    DWORD size = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light,
                     &size) != ERROR_SUCCESS) {
        return false;
    }
    return light == 0;
}

bool ApplySystemTheme(bool dark, const wchar_t* wallpaper) {
    const DWORD light = dark ? 0 : 1;
    if (!WritePersonalizeDword(L"AppsUseLightTheme", light) ||
        !WritePersonalizeDword(L"SystemUsesLightTheme", light)) {
        log::Write(L"theme: writing Personalize failed %lu", GetLastError());
        return false;
    }
    // 先广播再换壁纸。SystemParametersInfo(SPI_SETDESKWALLPAPER) 也会让 explorer 重画任务栏，
    // 实测它按“改变前”的模式画；先换壁纸再广播，任务栏会慢一拍（2026-11-08）。
    // 让 explorer 当场刷新深浅色；我们的主窗口也靠这条消息重载配色。
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"),
                        SMTO_ABORTIFHUNG, 1000, &result);
    if (wallpaper && wallpaper[0] != L'\0') {
        const std::wstring path = ExpandEnvironment(wallpaper);
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            log::Write(L"theme: wallpaper missing [%s]", path.c_str());
        } else {
            // 只换图片本身，不动 WallpaperStyle 等设置，和“设置 > 个性化 > 背景”选图片时一致
            if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(path.c_str()),
                                       SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)) {
                log::Write(L"theme: SPI_SETDESKWALLPAPER failed %lu", GetLastError());
            }
        }
    }
    return true;
}

void ApplySystemThemeAsync(HWND notify, bool dark, std::wstring wallpaper) {
    try {
        std::thread([notify, dark, wallpaper = std::move(wallpaper)] {
            bool ok = false;
            try {
                ok = ApplySystemTheme(dark, wallpaper.c_str());
            } catch (...) {
                ok = false;
            }
            if (notify) {
                PostMessageW(notify, msg::kThemeApplied, ok ? 1 : 0, 0);
            }
        }).detach();
    } catch (...) {
        if (notify) {
            PostMessageW(notify, msg::kThemeApplied, 0, 0);
        }
    }
}

}  // namespace pitot::ui
