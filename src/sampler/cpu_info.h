#pragma once

#include <array>
#include <cstddef>

#include "sampler/snapshot.h"

namespace pitot::sampler {

// CPUID 品牌字符串，如 "AMD Ryzen 5 5600 6-Core Processor"
void ReadCpuBrand(wchar_t* out, size_t size);

// 浮窗标题栏用的短名称："AMD Ryzen 5 5600 6-Core Processor" → "Ryzen 5 5600"。
// 去掉厂商前缀、“N-Core Processor”之类的后缀和多余空格。纯函数。
void ShortCpuName(const wchar_t* brand, wchar_t* out, size_t size);

template <size_t N>
void ShortCpuName(const wchar_t* brand, wchar_t (&out)[N]) {
    ShortCpuName(brand, out, N);
}

// PDH “Processor Information”的实例名“组,编号”→ 逻辑处理器下标（组 × 64 + 编号），
// 和 Snapshot::cpu_core_percent 的下标一致。"_Total"、"0,_Total" 等返回 -1。纯函数。
int ParseProcessorInstance(const wchar_t* name);

// 物理核与逻辑处理器的对应关系，每核占用热力格按它排列
struct CpuCore {
    std::array<int, 4> threads{};  // 逻辑处理器下标
    int thread_count = 0;
};
struct CpuTopology {
    int core_count = 0;
    int thread_count = 0;
    std::array<CpuCore, kMaxCpus> cores{};
};
bool ReadCpuTopology(CpuTopology& out);

}  // namespace pitot::sampler
