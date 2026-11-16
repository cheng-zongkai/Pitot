#pragma once

#include <windows.h>

#include <optional>

#include "config/config.h"
#include "ui/strip_layout.h"

namespace pitot::ui {

// 一条任务栏的当前状态。坐标都是物理像素。
struct TaskbarInfo {
    HWND tray = nullptr;
    bool primary = true;  // Shell_TrayWnd；副显示器的是 Shell_SecondaryTrayWnd（D-077）
    RECT rect{};    // 任务栏窗口，屏幕坐标
    RECT client{};  // 任务栏客户区，子窗口坐标以它为准
    RECT start{};   // 开始按钮（旧版 "Start" 子窗口），屏幕坐标；找不到时为空
    RECT tasks{};   // 任务栏图标区（旧版 MSTaskSwWClass，跟着图标的实际范围走），找不到时为空
    RECT notify{};  // 通知区（TrayNotifyWnd），找不到时为空
    UINT dpi = 96;
};

// 主显示器的任务栏
std::optional<TaskbarInfo> FindTaskbar();
// 某一条任务栏（主或副），窗口没了时为空
std::optional<TaskbarInfo> FindTaskbar(HWND tray);
// 全部任务栏，主任务栏排第一个。返回个数
inline constexpr int kMaxTaskbars = 4;
int FindTaskbars(TaskbarInfo (&out)[kMaxTaskbars]);

// 位置、尺寸、DPI 是否都相同
bool SameGeometry(const TaskbarInfo& a, const TaskbarInfo& b);

inline float ToDip(LONG px, UINT dpi) {
    return static_cast<float>(px) * USER_DEFAULT_SCREEN_DPI / static_cast<float>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI);
}

// 任务栏左缘到开始按钮左缘的距离（DIP）；找不到开始按钮时为空
inline std::optional<float> TaskbarStartOffsetDip(const TaskbarInfo& taskbar) {
    if (IsRectEmpty(&taskbar.start)) {
        return std::nullopt;
    }
    return ToDip(taskbar.start.left - taskbar.rect.left, taskbar.dpi);
}

// 任务栏条能放在哪儿（D-074），都从任务栏左缘算起，DIP
inline StripSpace StripSpaceFor(const TaskbarInfo& taskbar, config::StripPosition position) {
    if (position == config::StripPosition::Left) {
        return LeftStripSpace(TaskbarStartOffsetDip(taskbar));
    }
    const float width = ToDip(taskbar.rect.right - taskbar.rect.left, taskbar.dpi);
    std::optional<float> tasks_right;
    if (!IsRectEmpty(&taskbar.tasks)) {
        tasks_right = ToDip(taskbar.tasks.right - taskbar.rect.left, taskbar.dpi);
    }
    std::optional<float> notify_left;
    if (!IsRectEmpty(&taskbar.notify)) {
        notify_left = ToDip(taskbar.notify.left - taskbar.rect.left, taskbar.dpi);
    }
    return RightStripSpace(tasks_right, notify_left, width);
}

}  // namespace pitot::ui
