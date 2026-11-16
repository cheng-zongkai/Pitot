#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>

#include "sampler/snapshot.h"
#include "sensors/cpu_meters.h"
#include "sensors/decode.h"
#include "sensors/pawnio.h"

namespace pitot::sensors {

// Intel 的封装温度、封装功耗、有效频率，经 PawnIO IntelMSR 模块读取（D-084）。
// 只读 MSR，不写任何寄存器；不经过 SMN，所以不用持 PCI 锁。
// 每核温度不读：要逐核切亲和性，而封装温度已经是封装内最热处的读数
class IntelSensors {
public:
    // 加载模块。调用方已确认是 Intel；模块拒绝加载时 state 为 Unsupported
    void Init(CpuFamilyModel fm, uint32_t cpuid6_eax);
    [[nodiscard]] sampler::SensorState state() const noexcept { return state_; }

    // 封装温度与封装功耗。读失败的项保留上一轮
    void SampleThermalPower(sampler::SensorReadings& r);
    void SampleFrequency(sampler::SensorReadings& r, int cpu_count);
    void ResetFrequency() { frequency_.Reset(); }

private:
    bool ReadMsr(uint32_t msr, uint64_t& value) { return module_.Call("ioctl_read_msr", msr, value); }

    PawnIoModule module_;
    sampler::SensorState state_ = sampler::SensorState::Starting;
    std::optional<float> tjmax_;  // 没有封装温度传感器或读不到 TjMax 时为空，温度不显示
    double energy_unit_ = 0;
    std::optional<double> base_mhz_;

    EnergyMeter energy_;
    FrequencyMeter frequency_;
};

}  // namespace pitot::sensors
