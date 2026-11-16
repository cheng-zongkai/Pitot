#pragma once

#include <windows.h>

// 进程内自定义窗口消息，集中定义避免编号冲突。都发给隐藏主窗口。

namespace pitot::msg {

// 任务栏条被系统意外销毁（通常是 explorer 退出）
inline constexpr UINT kStripLost = WM_APP + 1;
// 采样线程发布了新快照
inline constexpr UINT kSnapshotReady = WM_APP + 2;
// 任务栏 DPI 或尺寸可能变了，需要重新同步任务栏条
inline constexpr UINT kTaskbarChanged = WM_APP + 3;
// 任务栏条被单击：展开或收起浮窗。wParam 是 ui::StripPart，顺序与 FlyoutWindow::Panel 一致
inline constexpr UINT kStripClicked = WM_APP + 4;
// 浮窗里点了“设置”
inline constexpr UINT kOpenSettings = WM_APP + 5;
// 浮窗显示或收起，wParam 为 1 表示显示。主窗口据此开关详细指标采集
inline constexpr UINT kFlyoutVisibility = WM_APP + 7;
// 浮窗当前显示的面板，wParam 是 FlyoutWindow::Panel。GPU 进程列表只在 GPU 面板打开时采
inline constexpr UINT kFlyoutPanel = WM_APP + 8;
// 浮窗网络面板里选了网卡。用 SendMessage 同步发，lParam 是 const GUID*，为空表示“自动”
inline constexpr UINT kSelectAdapter = WM_APP + 9;
// 结束进程的后台任务完成（发给浮窗自己）。wParam 是已结束的数量，lParam 是尝试的数量；
// 0 / 0 表示没能结束（已退出、身份变化、权限不足或关键进程）
inline constexpr UINT kEndProcessResult = WM_APP + 10;
// 主题切换的后台任务完成（发给浮窗自己）。wParam 为 1 表示成功
inline constexpr UINT kThemeApplied = WM_APP + 11;
// 高温通知临时通知区图标的回调（D-079）。lParam 低位是 NIN_* 事件（NOTIFYICON_VERSION_4）
inline constexpr UINT kHotNotify = WM_APP + 12;

}  // namespace pitot::msg
