#pragma once

#include <windows.h>

#include <string>

namespace pitot::ui {

// 当前 Windows 是否为深色模式（读 SystemUsesLightTheme，和“设置 > 个性化 > 颜色”一致）
bool SystemIsDarkTheme();

// 把当前用户的 Windows 浅色 / 深色模式和壁纸一起换掉，然后广播 WM_SETTINGCHANGE，
// 让任务栏、开始菜单、设置窗口等当场跟着变。壁纸为空表示不动壁纸。
// 会调用 SystemParametersInfo，必须放在后台线程上（见 ApplySystemThemeAsync）。
bool ApplySystemTheme(bool dark, const wchar_t* wallpaper);

// 在一次性后台线程里执行 ApplySystemTheme，完成后给 notify 发 msg::kThemeApplied
//（wParam 为 1 表示成功）。浮窗关闭了也不影响，PostMessage 失败即可。
void ApplySystemThemeAsync(HWND notify, bool dark, std::wstring wallpaper);

}  // namespace pitot::ui
