#include "sensors/intel.h"

#include "app/resource.h"
#include "common/log.h"

namespace pitot::sensors {

void IntelSensors::Init(CpuFamilyModel fm, uint32_t cpuid6_eax) {
    const DWORD error = module_.Open(IDR_PAWNIO_INTELMSR);
    if (error != ERROR_SUCCESS) {
        log::Write(L"intel: PawnIO IntelMSR unavailable, error %lu", error);
        state_ = StateFromOpenError(error);
        return;
    }

    uint64_t value = 0;
    // 封装温度要两样都有：CPUID 声明了封装级传感器，TjMax 能读出来。缺一样就不显示，不假设 100 ℃
    if ((cpuid6_eax & kCpuid6PackageThermal) && ReadMsr(kMsrTemperatureTarget, value)) {
        tjmax_ = DecodeTjMax(value);
    }
    // RAPL 从 Sandy Bridge 起才有；更老的型号读这个 MSR 会失败，功耗就不显示
    if (ReadMsr(kMsrRaplPowerUnit, value)) {
        energy_unit_ = IntelEnergyUnitJoules(fm, value);
    }
    if (ReadMsr(kMsrPlatformInfo, value)) {
        base_mhz_ = IntelBaseMhz(value);
    }
    log::Write(L"intel: family %X model %X, package sensor %s, TjMax %.0f, energy unit %.3g J, base %.0f MHz",
               fm.family, fm.model, (cpuid6_eax & kCpuid6PackageThermal) ? L"yes" : L"no", tjmax_ ? *tjmax_ : 0.0f,
               energy_unit_, base_mhz_ ? *base_mhz_ : 0.0);
    state_ = sampler::SensorState::Ok;
}

void IntelSensors::SampleThermalPower(sampler::SensorReadings& r) {
    if (state_ != sampler::SensorState::Ok) {
        return;
    }
    uint64_t value = 0;
    if (energy_unit_ > 0 && ReadMsr(kMsrIntelPkgEnergy, value)) {
        if (const auto watts = energy_.Sample(static_cast<uint32_t>(value), energy_unit_)) {
            r.package_watts = watts;
        }
    }
    // 封装级 MSR，在哪个核上读都一样
    if (tjmax_ && ReadMsr(kMsrPackageThermStatus, value)) {
        r.cpu_temp = DecodeIntelThermStatus(value, *tjmax_);
    }
}

void IntelSensors::SampleFrequency(sampler::SensorReadings& r, int cpu_count) {
    r.effective_mhz.reset();
    if (state_ != sampler::SensorState::Ok || !base_mhz_) {
        return;
    }
    r.effective_mhz = frequency_.Sample(cpu_count, kMsrIntelMperf, kMsrIntelAperf, *base_mhz_,
                                        [this](uint32_t msr, uint64_t& v) { return ReadMsr(msr, v); });
}

}  // namespace pitot::sensors
