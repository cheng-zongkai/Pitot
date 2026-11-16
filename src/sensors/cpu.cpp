#include "sensors/cpu.h"

#include <intrin.h>

#include "common/log.h"

namespace pitot::sensors {

void CpuSensors::Init() {
    int regs[4]{};
    __cpuid(regs, 0);
    const auto max_leaf = static_cast<uint32_t>(regs[0]);
    const CpuVendorId id =
        DecodeCpuVendor(static_cast<uint32_t>(regs[1]), static_cast<uint32_t>(regs[3]), static_cast<uint32_t>(regs[2]));
    __cpuid(regs, 1);
    const CpuFamilyModel fm = DecodeFamilyModel(static_cast<uint32_t>(regs[0]));
    switch (id) {
    case CpuVendorId::Amd:
        vendor_ = sampler::CpuVendor::Amd;
        zen_.Init(fm);
        break;
    case CpuVendorId::Intel: {
        vendor_ = sampler::CpuVendor::Intel;
        uint32_t leaf6 = 0;
        if (max_leaf >= 6) {
            __cpuid(regs, 6);
            leaf6 = static_cast<uint32_t>(regs[0]);
        }
        intel_.Init(fm, leaf6);
        break;
    }
    case CpuVendorId::Unknown:
        log::Write(L"cpu: unsupported vendor (family %X model %X)", fm.family, fm.model);
        break;
    }
}

sampler::SensorState CpuSensors::state() const noexcept {
    switch (vendor_) {
    case sampler::CpuVendor::Amd:
        return zen_.state();
    case sampler::CpuVendor::Intel:
        return intel_.state();
    default:
        return sampler::SensorState::Unsupported;
    }
}

void CpuSensors::SampleThermalPower(sampler::SensorReadings& r) {
    if (vendor_ == sampler::CpuVendor::Amd) {
        zen_.SampleThermalPower(r);
    } else if (vendor_ == sampler::CpuVendor::Intel) {
        intel_.SampleThermalPower(r);
    }
}

void CpuSensors::SampleFrequency(sampler::SensorReadings& r, int cpu_count) {
    if (vendor_ == sampler::CpuVendor::Amd) {
        zen_.SampleFrequency(r, cpu_count);
    } else if (vendor_ == sampler::CpuVendor::Intel) {
        intel_.SampleFrequency(r, cpu_count);
    } else {
        r.effective_mhz.reset();
    }
}

void CpuSensors::ResetFrequency() {
    zen_.ResetFrequency();
    intel_.ResetFrequency();
}

}  // namespace pitot::sensors
