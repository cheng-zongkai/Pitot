#include "ui/taskbar.h"

#include <cwchar>
#include <iterator>

namespace pitot::ui {
namespace {

void ChildRect(HWND parent, const wchar_t* cls, RECT& out) {
    out = {};
    if (HWND child = FindWindowExW(parent, nullptr, cls, nullptr)) {
        GetWindowRect(child, &out);
    }
}

}  // namespace

std::optional<TaskbarInfo> FindTaskbar(HWND tray) {
    TaskbarInfo info;
    info.tray = tray;
    if (!tray || !IsWindow(tray) || !GetWindowRect(tray, &info.rect) || !GetClientRect(tray, &info.client)) {
        return std::nullopt;
    }
    wchar_t cls[32]{};
    GetClassNameW(tray, cls, static_cast<int>(std::size(cls)));
    info.primary = wcscmp(cls, L"Shell_TrayWnd") == 0;
    info.dpi = GetDpiForWindow(tray);
    if (info.dpi == 0) {
        info.dpi = USER_DEFAULT_SCREEN_DPI;
    }
    // Win11 的任务栏由 XAML 绘制，但旧版子窗口仍然跟随各部分的位置，可以低成本拿到：
    // "Start" 是开始按钮，ReBarWindow32 → MSTaskSwWClass 是图标区，TrayNotifyWnd 是通知区
    ChildRect(tray, L"Start", info.start);
    if (HWND rebar = FindWindowExW(tray, nullptr, L"ReBarWindow32", nullptr)) {
        ChildRect(rebar, L"MSTaskSwWClass", info.tasks);
        if (IsRectEmpty(&info.tasks)) {
            GetWindowRect(rebar, &info.tasks);
        }
    } else {
        // 副任务栏没有 ReBar，图标区直接是 WorkerW 下的 MSTaskListWClass
        if (HWND worker = FindWindowExW(tray, nullptr, L"WorkerW", nullptr)) {
            ChildRect(worker, L"MSTaskListWClass", info.tasks);
        }
    }
    ChildRect(tray, L"TrayNotifyWnd", info.notify);
    return info;
}

std::optional<TaskbarInfo> FindTaskbar() { return FindTaskbar(FindWindowW(L"Shell_TrayWnd", nullptr)); }

int FindTaskbars(TaskbarInfo (&out)[kMaxTaskbars]) {
    int n = 0;
    if (const auto primary = FindTaskbar()) {
        out[n++] = *primary;
    }
    HWND secondary = nullptr;
    while (n < kMaxTaskbars && (secondary = FindWindowExW(nullptr, secondary, L"Shell_SecondaryTrayWnd", nullptr))) {
        if (const auto info = FindTaskbar(secondary)) {
            out[n++] = *info;
        }
    }
    return n;
}

bool SameGeometry(const TaskbarInfo& a, const TaskbarInfo& b) {
    return a.tray == b.tray && a.dpi == b.dpi && EqualRect(&a.rect, &b.rect) && EqualRect(&a.client, &b.client) &&
           EqualRect(&a.start, &b.start) && EqualRect(&a.tasks, &b.tasks) && EqualRect(&a.notify, &b.notify);
}

}  // namespace pitot::ui
