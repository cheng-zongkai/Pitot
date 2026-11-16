#include "app/hot_notice.h"

#include <shellapi.h>

#include <cwchar>

#include "app/resource.h"
#include "common/log.h"

namespace pitot::app {
namespace {

constexpr UINT kIconId = 1;

}  // namespace

void HotNotice::Show(HWND owner, UINT callback_message, const wchar_t* title, const wchar_t* body) {
    Remove();
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = kIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO | NIF_SHOWTIP;
    data.uCallbackMessage = callback_message;
    // 通知区域里临时出现的图标用 Pitot 自己的，提示是谁发的；警告意味交给 NIIF_WARNING（D-082）
    const HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_PITOT),
                                                     IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                     GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    data.hIcon = icon ? icon : LoadIconW(nullptr, IDI_WARNING);
    wcsncpy_s(data.szTip, title, _TRUNCATE);
    wcsncpy_s(data.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(data.szInfo, body, _TRUNCATE);
    // 勿扰时段里不打扰（专注助手）
    data.dwInfoFlags = NIIF_WARNING | NIIF_RESPECT_QUIET_TIME;
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        log::Write(L"hot: Shell_NotifyIcon add failed %lu", GetLastError());
        return;
    }
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    owner_ = owner;
    shown_ = true;
    log::Write(L"hot: notified [%s]", body);
}

void HotNotice::Remove() {
    if (!shown_) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner_;
    data.uID = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    shown_ = false;
}

}  // namespace pitot::app
