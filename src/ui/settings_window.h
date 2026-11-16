#pragma once

#include <windows.h>

namespace pitot::ui {

// 设置窗口（M7，D-042）。Pitot.exe --settings 另起一个进程运行：主进程禁用了输入法（D-024），
// 这里要输入文字。每改一项立即写进 config.ini，主进程靠目录变更通知当场生效，不用“保存”按钮。
// 返回进程退出码。
int RunSettings(HINSTANCE instance);

}  // namespace pitot::ui
