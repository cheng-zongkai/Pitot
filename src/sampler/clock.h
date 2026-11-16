#pragma once

#include <windows.h>

#include <cstdint>

namespace pitot::sampler {

// QPC 时钟（D-063）。ETW 会话的事件时间戳也是 QPC，进程速率和卡片速率用它算时长才能对上；
// GetTickCount64 只有 15.6 ms 的分辨率，一秒的区间就差 1.6%
inline int64_t Qpc() noexcept {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

inline int64_t QpcFrequency() noexcept {
    // 开机后不变，只取一次
    static const int64_t frequency = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    return frequency;
}

inline double QpcSeconds(int64_t ticks) noexcept {
    return static_cast<double>(ticks) / static_cast<double>(QpcFrequency());
}

// 毫秒，给按毫秒记时的累计计数器速率（NetworkRate）用
inline uint64_t QpcMs() noexcept {
    return static_cast<uint64_t>(Qpc() / (QpcFrequency() / 1000));
}

}  // namespace pitot::sampler
