#include "sensors/zen.h"

#include "app/resource.h"
#include "common/log.h"
#include "common/names.h"

namespace pitot::sensors {
namespace {

// 规则：共享锁最多等 10 ms，拿不到就跳过本轮（AGENTS.md）
constexpr DWORD kLockTimeoutMs = 10;

}  // namespace

void ZenSensors::Init(CpuFamilyModel fm) {
    fm_ = fm;
    if (fm_.family < 0x17 || fm_.family > 0x1A) {
        log::Write(L"zen: not a supported AMD cpu (family %X model %X)", fm_.family, fm_.model);
        state_ = sampler::SensorState::Unsupported;
        return;
    }

    const DWORD error = module_.Open(IDR_PAWNIO_AMDFAMILY17);
    if (error != ERROR_SUCCESS) {
        log::Write(L"zen: PawnIO AMDFamily17 unavailable, error %lu", error);
        state_ = StateFromOpenError(error);
        return;
    }
    if (!pci_mutex_.Open(names::kPciMutex)) {
        log::Write(L"zen: cannot open %s, error %lu", names::kPciMutex, GetLastError());
    }
    ccd_base_ = CcdTemperatureBase(fm_);

    uint64_t value = 0;
    if (ReadMsr(kMsrPwrUnit, value)) {
        energy_unit_ = EnergyUnitJoules(value);
    }
    if (ReadMsr(kMsrPstate0, value)) {
        base_mhz_ = PstateMhz(fm_.family, value);
    }
    log::Write(L"zen: family %X model %X, ccd base %s, energy unit %.3g J, P0 %.0f MHz", fm_.family, fm_.model,
               ccd_base_ ? L"yes" : L"no", energy_unit_, base_mhz_ ? *base_mhz_ : 0.0);
    state_ = sampler::SensorState::Ok;
}

bool ZenSensors::ReadSmn(uint32_t address, uint32_t& value) {
    uint64_t out = 0;
    if (!module_.Call("ioctl_read_smn", address, out)) {
        return false;
    }
    value = static_cast<uint32_t>(out);
    return true;
}

void ZenSensors::SampleThermalPower(sampler::SensorReadings& r) {
    if (state_ != sampler::SensorState::Ok) {
        return;
    }

    // 封装功耗：封装级 MSR，在哪个核上读都一样。读失败时保留上一轮
    uint64_t energy = 0;
    if (energy_unit_ > 0 && ReadMsr(kMsrPkgEnergy, energy)) {
        if (const auto watts = energy_.Sample(static_cast<uint32_t>(energy), energy_unit_)) {
            r.package_watts = watts;
        }
    }

    SharedMutexLock lock(pci_mutex_, kLockTimeoutMs);
    if (!lock) {
        return;  // FanControl 等正在用 PCI，本轮温度保留上一次
    }
    uint32_t raw = 0;
    if (ReadSmn(kSmnTctl, raw)) {
        r.cpu_temp = DecodeTctl(raw);
    }
    if (ccd_base_) {
        for (int i = 0; i < kMaxCcds; ++i) {
            r.ccd[static_cast<size_t>(i)].reset();
            if (ReadSmn(*ccd_base_ + static_cast<uint32_t>(i) * 4, raw)) {
                r.ccd[static_cast<size_t>(i)] = DecodeCcdTemperature(raw);
            }
        }
    }
}

void ZenSensors::SampleFrequency(sampler::SensorReadings& r, int cpu_count) {
    r.effective_mhz.reset();
    if (state_ != sampler::SensorState::Ok || !base_mhz_) {
        return;
    }
    r.effective_mhz = frequency_.Sample(cpu_count, kMsrMperf, kMsrAperf, *base_mhz_,
                                        [this](uint32_t msr, uint64_t& value) { return ReadMsr(msr, value); });
}

}  // namespace pitot::sensors
