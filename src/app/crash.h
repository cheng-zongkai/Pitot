#pragma once

// 崩溃记录（M8，D-078）：未处理的异常、std::terminate、纯虚调用和 CRT 非法参数都会在
// %LOCALAPPDATA%\Pitot 下留一份小型转储 crash-<时间>.dmp，并在 pitot.log 里写一行异常代码和出错的模块。
// 只留最近 names::kCrashDumpsKept 份。转储里只有线程栈和模块列表，不含整个进程的内存。

namespace pitot::app {

// 尽早调用（在 log::Init 之后）。主进程和设置窗口进程都装
void InstallCrashHandler();

}  // namespace pitot::app
