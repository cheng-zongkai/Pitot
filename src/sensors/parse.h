#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

// 显卡和存储用到的字符串解析与换算。纯函数，有单元测试。

namespace pitot::sensors {

// PDH “PhysicalDisk”的实例名“1 D: E:”→ 盘号 1；“_Total”或格式不对返回空
std::optional<uint32_t> ParseDiskInstance(const wchar_t* name);

// IOCTL_STORAGE_QUERY_PROPERTY 返回的 ASCII 型号 → 宽字符，去掉首尾空格、把连续空格压成一个
void CleanModelName(const char* in, wchar_t* out, size_t size);

// PDH “GPU Engine”“GPU Process Memory”的实例名：
// “pid_1234_luid_0x00000000_0x0000E145_phys_0_eng_0_engtype_3D”→ pid 和适配器 LUID
struct GpuInstance {
    uint32_t pid = 0;
    uint32_t luid_high = 0;
    uint32_t luid_low = 0;
};
std::optional<GpuInstance> ParseGpuInstance(const wchar_t* name);

// 节点累计运行时间（100 ns）两次读数之差 ÷ 经过时间 → 利用率 0–100。
// 计数倒退或时间不对返回空
std::optional<double> NodeUtilization(int64_t previous, int64_t current, int64_t elapsed_100ns);

}  // namespace pitot::sensors
