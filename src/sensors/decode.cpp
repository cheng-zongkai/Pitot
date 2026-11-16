#include "sensors/decode.h"

namespace pitot::sensors {

CpuFamilyModel DecodeFamilyModel(uint32_t eax) {
    const uint32_t base_family = (eax >> 8) & 0xF;
    const uint32_t base_model = (eax >> 4) & 0xF;
    CpuFamilyModel fm;
    fm.family = base_family == 0xF ? base_family + ((eax >> 20) & 0xFF) : base_family;
    fm.model = (base_family == 0xF || base_family == 0x6) ? (((eax >> 16) & 0xF) << 4) | base_model : base_model;
    return fm;
}

std::optional<uint32_t> CcdTemperatureBase(CpuFamilyModel fm) {
    if (fm.family == 0x17 || fm.family == 0x19) {
        switch (fm.model) {
        case 0x31:  // Threadripper 3000
        case 0x71:  // Zen 2 桌面
        case 0x21:  // Zen 3 桌面（Vermeer）
            return kSmnCcdTempZen2;
        case 0x61:  // Zen 4 桌面（Raphael）
            return kSmnCcdTempZen4;
        default:
            return std::nullopt;
        }
    }
    if (fm.family == 0x1A && fm.model == 0x44) {  // Zen 5 桌面（Granite Ridge）
        return kSmnCcdTempZen4;
    }
    return std::nullopt;
}

float DecodeTctl(uint32_t raw) {
    const bool offset = (raw & 0x80000) != 0 || (raw & 0x30000) == 0x30000;
    float t = static_cast<float>(raw >> 21) * 0.125f;
    if (offset) {
        t -= 49.0f;
    }
    return t;
}

std::optional<float> DecodeCcdTemperature(uint32_t raw) {
    const uint32_t value = raw & 0xFFF;
    if (value == 0) {
        return std::nullopt;
    }
    const float t = static_cast<float>(value) * 0.125f - 305.0f;
    if (t >= 125.0f) {
        return std::nullopt;
    }
    return t;
}

double EnergyUnitJoules(uint64_t pwr_unit_msr) {
    const int esu = static_cast<int>((pwr_unit_msr >> 8) & 0x1F);
    return 1.0 / static_cast<double>(1ull << esu);
}

std::optional<double> PstateMhz(uint32_t family, uint64_t msr) {
    if ((msr >> 63) == 0) {
        return std::nullopt;
    }
    if (family == 0x1A) {
        const uint32_t fid = static_cast<uint32_t>(msr & 0xFFF);
        return fid ? std::optional<double>(fid * 5.0) : std::nullopt;
    }
    const uint32_t fid = static_cast<uint32_t>(msr & 0xFF);
    const uint32_t dfs = static_cast<uint32_t>((msr >> 8) & 0x3F);
    if (fid == 0 || dfs == 0) {
        return std::nullopt;
    }
    return static_cast<double>(fid) / static_cast<double>(dfs) * 200.0;
}

CpuVendorId DecodeCpuVendor(uint32_t ebx, uint32_t edx, uint32_t ecx) {
    // "AuthenticAMD" / "GenuineIntel"，按 EBX、EDX、ECX 的顺序
    if (ebx == 0x68747541 && edx == 0x69746E65 && ecx == 0x444D4163) {
        return CpuVendorId::Amd;
    }
    if (ebx == 0x756E6547 && edx == 0x49656E69 && ecx == 0x6C65746E) {
        return CpuVendorId::Intel;
    }
    return CpuVendorId::Unknown;
}

std::optional<float> DecodeTjMax(uint64_t msr) {
    const uint32_t tjmax = static_cast<uint32_t>((msr >> 16) & 0xFF);
    return tjmax ? std::optional<float>(static_cast<float>(tjmax)) : std::nullopt;
}

std::optional<float> DecodeIntelThermStatus(uint64_t msr, float tjmax) {
    if ((msr & 0x80000000u) == 0) {
        return std::nullopt;
    }
    return tjmax - static_cast<float>((msr >> 16) & 0x7F);
}

std::optional<double> IntelBaseMhz(uint64_t msr) {
    const uint32_t ratio = static_cast<uint32_t>((msr >> 8) & 0xFF);
    return ratio ? std::optional<double>(ratio * kIntelBusMhz) : std::nullopt;
}

double IntelEnergyUnitJoules(CpuFamilyModel fm, uint64_t msr) {
    const int esu = static_cast<int>((msr >> 8) & 0x1F);
    if (fm.family == 0x6) {
        switch (fm.model) {
        case 0x37:  // Silvermont
        case 0x4A:
        case 0x4D:
        case 0x5A:
        case 0x5D:
        case 0x4C:  // Airmont
            return 1.0e-6 * static_cast<double>(1ull << esu);
        default:
            break;
        }
    }
    return 1.0 / static_cast<double>(1ull << esu);
}

std::optional<float> DecodeNctTemperature(uint8_t high, uint8_t low) {
    const float t = static_cast<float>(static_cast<int8_t>(high)) + ((low & 0x80) ? 0.5f : 0.0f);
    if (high == 0 || t >= 127.0f || t <= -127.0f) {
        return std::nullopt;
    }
    return t;
}

}  // namespace pitot::sensors
