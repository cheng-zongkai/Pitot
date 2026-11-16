// 传感器寄存器解码：Zen 与 Intel 的温度 / 能量 / 基准频率、NCT6687D 温度

#include <cmath>

#include "sensors/decode.h"
#include "test.h"

namespace {

using namespace pitot::sensors;

bool Near(double a, double b) { return std::fabs(a - b) < 1e-4; }

}  // namespace

TEST_CASE("zen: family and model from cpuid") {
    // Ryzen 5 5600：CPUID 1 EAX = 0x00A20F12 → family 19h model 21h
    const CpuFamilyModel fm = DecodeFamilyModel(0x00A20F12);
    CHECK(fm.family == 0x19);
    CHECK(fm.model == 0x21);
    // Ryzen 5 3600：0x00870F10 → 17h / 71h
    const CpuFamilyModel zen2 = DecodeFamilyModel(0x00870F10);
    CHECK(zen2.family == 0x17);
    CHECK(zen2.model == 0x71);
}

TEST_CASE("zen: ccd temperature base by model") {
    CHECK(CcdTemperatureBase({0x19, 0x21}) == kSmnCcdTempZen2);
    CHECK(CcdTemperatureBase({0x17, 0x71}) == kSmnCcdTempZen2);
    CHECK(CcdTemperatureBase({0x19, 0x61}) == kSmnCcdTempZen4);
    CHECK(!CcdTemperatureBase({0x19, 0x50}));  // Cezanne APU：只有 Tctl
    CHECK(!CcdTemperatureBase({0x17, 0x08}));
}

TEST_CASE("zen: tctl decode with and without 49 C offset") {
    // 54.875 ℃ = 439 × 0.125
    CHECK(Near(DecodeTctl(439u << 21), 54.875));
    // RANGE_SEL 置位：减 49
    CHECK(Near(DecodeTctl((439u << 21) | 0x80000), 5.875));
    // TJ_SEL = 11b 同样减 49；只有一位不算
    CHECK(Near(DecodeTctl((800u << 21) | 0x30000), 51.0));
    CHECK(Near(DecodeTctl((800u << 21) | 0x10000), 100.0));
}

TEST_CASE("zen: ccd temperature decode") {
    // 51.625 ℃ → (51.625 + 305) / 0.125 = 2853
    CHECK(DecodeCcdTemperature(2853).has_value());
    CHECK(Near(*DecodeCcdTemperature(2853), 51.625));
    CHECK(Near(*DecodeCcdTemperature(0xABC00000u | 2853), 51.625));  // 高位忽略
    CHECK(!DecodeCcdTemperature(0));                                  // 没有这个 CCD
    CHECK(!DecodeCcdTemperature(0xFFF));                              // 206 ℃，无效
}

TEST_CASE("zen: energy unit and counter wrap") {
    // ESU = 16 → 15.26 µJ
    CHECK(Near(EnergyUnitJoules(0x000A1003), 1.0 / 65536));
    CHECK(EnergyDelta(100, 350) == 250);
    CHECK(EnergyDelta(0xFFFFFF00u, 0x10) == 0x110);
}

TEST_CASE("zen: P0 frequency") {
    // Ryzen 5 5600 P0 = 3.5 GHz：FID 0x8C (140)，DfsId 8 → 140 / 8 × 200 = 3500
    const uint64_t p0 = (1ull << 63) | (8ull << 8) | 0x8C;
    CHECK(PstateMhz(0x19, p0) == 3500.0);
    CHECK(!PstateMhz(0x19, p0 & ~(1ull << 63)));  // 未启用
    CHECK(!PstateMhz(0x19, 1ull << 63));          // FID / DfsId 为 0
    CHECK(PstateMhz(0x1A, (1ull << 63) | 1000) == 5000.0);
}

TEST_CASE("nct6687: temperature decode") {
    CHECK(Near(*DecodeNctTemperature(46, 0x00), 46.0));
    CHECK(Near(*DecodeNctTemperature(37, 0x80), 37.5));
    CHECK(Near(*DecodeNctTemperature(0xFB, 0x80), -4.5));  // 有符号
    CHECK(!DecodeNctTemperature(0, 0x80));                  // 0 视为未接
    CHECK(!DecodeNctTemperature(127, 0));                   // ≥ 127 视为未接
    CHECK(DecodeNctFan(0x04, 0x60) == 1120);
}

TEST_CASE("cpu: vendor from cpuid 0") {
    CHECK(DecodeCpuVendor(0x68747541, 0x69746E65, 0x444D4163) == CpuVendorId::Amd);
    CHECK(DecodeCpuVendor(0x756E6547, 0x49656E69, 0x6C65746E) == CpuVendorId::Intel);
    CHECK(DecodeCpuVendor(0, 0, 0) == CpuVendorId::Unknown);
    // Core i5-12400：CPUID 1 EAX = 0x00090675 → family 6 model 97h
    const CpuFamilyModel adl = DecodeFamilyModel(0x00090675);
    CHECK(adl.family == 0x6);
    CHECK(adl.model == 0x97);
}

TEST_CASE("intel: package temperature") {
    // TjMax 100 ℃，TCC 偏移 [29:24] 不参与
    const auto tjmax = DecodeTjMax((5ull << 24) | (100ull << 16));
    CHECK(tjmax && *tjmax == 100.0f);
    CHECK(!DecodeTjMax(0));
    // 距 TjMax 45 ℃ → 55 ℃；[31] 为 0 是无效读数
    CHECK(Near(*DecodeIntelThermStatus(0x80000000ull | (45ull << 16), 100.0f), 55.0));
    CHECK(Near(*DecodeIntelThermStatus(0x88000000ull | (0x7Full << 16) | 0xFFFF, 100.0f), -27.0));
    CHECK(!DecodeIntelThermStatus(45ull << 16, 100.0f));
}

TEST_CASE("intel: base frequency and energy unit") {
    CHECK(IntelBaseMhz(36ull << 8) == 3600.0);
    CHECK(!IntelBaseMhz(0));
    // 常见的 RAPL_POWER_UNIT 0xA0E03：ESU 14 → 61 µJ
    CHECK(Near(IntelEnergyUnitJoules({0x6, 0x97}, 0xA0E03), 1.0 / 16384));
    // Silvermont 的单位是 2^ESU 微焦：ESU 5 → 32 µJ
    CHECK(Near(IntelEnergyUnitJoules({0x6, 0x37}, 0x505), 32e-6));
}
