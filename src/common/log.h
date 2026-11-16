#pragma once

// 诊断日志：%LOCALAPPDATA%\Pitot\pitot.log，UTF-8，追加写。
// 不分配堆内存，单行超过约 1000 字符会被截断。任何线程都可调用。

#include <sal.h>

namespace pitot::log {

void Init();
void Write(_Printf_format_string_ const wchar_t* format, ...);
// 记一行当前进程私有内存（提交量），排查常驻内存预算用
void Memory(const wchar_t* label);

}  // namespace pitot::log
