#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <optional>

#include "sampler/snapshot.h"
#include "sensors/cpu_meters.h"
#include "sensors/decode.h"
#include "sensors/pawnio.h"

namespace pitot::sensors {

// AMD Zen 的温度、封装功耗、有效频率，经 PawnIO AMDFamily17 模块读取（D-028）。
// 只读 MSR 和 SMN，不写任何寄存器。SMN 读取期间持 Global\Access_PCI。
class ZenSensors {
public:
    // 加载模块。调用方已确认是 AMD；不是 17h–1Ah 时 state 为 Unsupported
    void Init(CpuFamilyModel fm);
    [[nodiscard]] sampler::SensorState state() const noexcept { return state_; }

    // Tctl、CCD 温度、封装功耗。拿不到 PCI 锁时温度保留上一轮
    void SampleThermalPower(sampler::SensorReadings& r);
    // 有效频率：逐个逻辑处理器读 APERF / MPERF（FrequencyMeter）。第一轮只建立基准
    void SampleFrequency(sampler::SensorReadings& r, int cpu_count);
    void ResetFrequency() { frequency_.Reset(); }

private:
    bool ReadMsr(uint32_t msr, uint64_t& value) { return module_.Call("ioctl_read_msr", msr, value); }
    bool ReadSmn(uint32_t address, uint32_t& value);

    PawnIoModule module_;
    SharedMutex pci_mutex_;
    sampler::SensorState state_ = sampler::SensorState::Starting;
    CpuFamilyModel fm_{};
    std::optional<uint32_t> ccd_base_;
    double energy_unit_ = 0;
    std::optional<double> base_mhz_;

    EnergyMeter energy_;
    FrequencyMeter frequency_;
};

}  // namespace pitot::sensors
