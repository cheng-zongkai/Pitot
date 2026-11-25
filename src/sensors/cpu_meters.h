#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

#include "sampler/snapshot.h"
#include "sensors/decode.h"

// AMD 和 Intel 共用的两种计算：能量计数 → 封装功耗，APERF / MPERF → 运行时平均频率。
// 两边只是 MSR 编号和单位不同（见 docs/subsystems/cpu.md）。

namespace pitot::sensors {

// PawnIoModule::Open 的错误码 → 面板上的状态说明
inline sampler::SensorState StateFromOpenError(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return sampler::SensorState::NoDriver;
    case ERROR_NOT_SUPPORTED:
        return sampler::SensorState::Unsupported;
    default:
        return sampler::SensorState::Failed;
    }
}

// 32 位能量计数的差值 ÷ 经过时间。第一轮只建立基准
class EnergyMeter {
public:
    std::optional<float> Sample(uint32_t counter, double joules_per_count) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        std::optional<float> watts;
        if (primed_) {
            LARGE_INTEGER freq{};
            QueryPerformanceFrequency(&freq);
            const double seconds =
                static_cast<double>(now.QuadPart - last_time_.QuadPart) / static_cast<double>(freq.QuadPart);
            if (seconds > 0.1) {
                watts = static_cast<float>(EnergyDelta(last_, counter) * joules_per_count / seconds);
            }
        }
        last_ = counter;
        last_time_ = now;
        primed_ = true;
        return watts;
    }

private:
    uint32_t last_ = 0;
    LARGE_INTEGER last_time_{};
    bool primed_ = false;
};

// 逐个逻辑处理器读 APERF / MPERF。MSR 只能在目标核上读，所以会临时改采样线程的亲和性，读完恢复。
// 两个计数器都只在 C0 递增：ΣΔAPERF / ΣΔMPERF × 基准频率 = 各核运行时的平均频率，按运行时长加权
class FrequencyMeter {
public:
    // read(msr, value) 读当前核上的一个 MSR。第一轮只建立基准，返回空
    template <class ReadMsr>
    std::optional<float> Sample(int cpu_count, uint32_t mperf_msr, uint32_t aperf_msr, double base_mhz,
                                ReadMsr&& read) {
        cpu_count = std::min(cpu_count, sampler::kMaxCpus);
        if (cpu_count <= 0 || base_mhz <= 0) {
            return std::nullopt;
        }
        const HANDLE thread = GetCurrentThread();
        DWORD_PTR previous = 0;
        uint64_t sum_aperf = 0;
        uint64_t sum_mperf = 0;
        bool ok = true;
        for (int i = 0; i < cpu_count; ++i) {
            const DWORD_PTR old = SetThreadAffinityMask(thread, DWORD_PTR{1} << i);
            if (i == 0) {
                previous = old;
            }
            if (old == 0) {
                ok = false;
                continue;
            }
            uint64_t mperf = 0;
            uint64_t aperf = 0;
            if (!read(mperf_msr, mperf) || !read(aperf_msr, aperf)) {
                ok = false;
                continue;
            }
            const auto idx = static_cast<size_t>(i);
            // 任一计数器回绕就丢掉这个核本轮的差值
            if (primed_ && mperf >= last_mperf_[idx] && aperf >= last_aperf_[idx]) {
                sum_mperf += mperf - last_mperf_[idx];
                sum_aperf += aperf - last_aperf_[idx];
            }
            last_mperf_[idx] = mperf;
            last_aperf_[idx] = aperf;
        }
        if (previous) {
            SetThreadAffinityMask(thread, previous);
        }
        std::optional<float> mhz;
        if (primed_ && ok && sum_mperf > 0) {
            mhz = static_cast<float>(static_cast<double>(sum_aperf) / static_cast<double>(sum_mperf) * base_mhz);
        }
        primed_ = ok;
        return mhz;
    }

    void Reset() { primed_ = false; }

private:
    std::array<uint64_t, sampler::kMaxCpus> last_aperf_{};
    std::array<uint64_t, sampler::kMaxCpus> last_mperf_{};
    bool primed_ = false;
};

}  // namespace pitot::sensors
