#pragma once

#include <windows.h>

namespace pitot::config {

// %APPDATA%\Pitot 和其中的 config.ini。目录不存在时创建。失败返回 false。
bool GetConfigPaths(wchar_t (&dir)[MAX_PATH], wchar_t (&file)[MAX_PATH]);

// %LOCALAPPDATA%Pitot 下的数据文件路径（目录不存在时创建）。失败返回 false。
bool GetLocalDataPath(const wchar_t* name, wchar_t (&file)[MAX_PATH]);

}  // namespace pitot::config
