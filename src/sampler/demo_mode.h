#pragma once

#include <atomic>

// 演示模式的进程内开关（Pitot.exe --demo，D-083）。放在单独的头文件里，
// 让 pitot_core 里的 CPU 型号 / 拓扑读取也能看到它，不用链接演示数据本身

namespace pitot::sampler {

inline std::atomic<bool> g_demo_mode{false};

inline void SetDemoMode(bool on) noexcept { g_demo_mode.store(on); }
inline bool DemoMode() noexcept { return g_demo_mode.load(); }

}  // namespace pitot::sampler
