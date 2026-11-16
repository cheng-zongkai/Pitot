#pragma once

#include "sampler/snapshot.h"
#include "sensors/intel.h"
#include "sensors/zen.h"

namespace pitot::sensors {

// 按 CPUID 厂商分派到 ZenSensors 或 IntelSensors（D-084）。其他厂商 state 为 Unsupported
class CpuSensors {
public:
    void Init();
    [[nodiscard]] sampler::SensorState state() const noexcept;
    [[nodiscard]] sampler::CpuVendor vendor() const noexcept { return vendor_; }

    void SampleThermalPower(sampler::SensorReadings& r);
    void SampleFrequency(sampler::SensorReadings& r, int cpu_count);
    void ResetFrequency();

private:
    sampler::CpuVendor vendor_ = sampler::CpuVendor::Unknown;
    ZenSensors zen_;
    IntelSensors intel_;
};

}  // namespace pitot::sensors
