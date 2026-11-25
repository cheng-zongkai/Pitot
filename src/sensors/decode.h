#pragma once

#include <cstdint>
#include <optional>

// 传感器寄存器的地址与解码。纯函数，有单元测试。
// 常量和公式的出处见 docs/subsystems/cpu.md 和 docs/subsystems/board-sensors.md。
// LHM 指 LibreHardwareMonitor master（2026-11-04）。

namespace pitot::sensors {

// ---- AMD Zen（AMDFamily17 模块） ----
inline constexpr uint32_t kMsrMperf = 0xC00000E7;      // 只读别名，P0 频率计数，仅 C0 时递增
inline constexpr uint32_t kMsrAperf = 0xC00000E8;      // 只读别名，实际频率计数，仅 C0 时递增
inline constexpr uint32_t kMsrPstate0 = 0xC0010064;    // P0 定义，算基准频率
inline constexpr uint32_t kMsrPwrUnit = 0xC0010299;    // 能量单位 ESU [12:8]
inline constexpr uint32_t kMsrPkgEnergy = 0xC001029B;  // 封装能量计数 [31:0]
inline constexpr uint32_t kSmnTctl = 0x00059800;       // THM_TCON_CUR_TMP
inline constexpr uint32_t kSmnCcdTempZen2 = 0x00059954;  // Zen 2 / Zen 3 桌面与 TR3000 的 CCD1，后面每个 CCD +4
inline constexpr uint32_t kSmnCcdTempZen4 = 0x00059B08;  // Zen 4 / Zen 5 桌面
inline constexpr int kMaxCcds = 8;

struct CpuFamilyModel {
    uint32_t family = 0;
    uint32_t model = 0;
};
// CPUID 1 的 EAX → 显示用 family / model（含扩展位）
CpuFamilyModel DecodeFamilyModel(uint32_t cpuid1_eax);

// 这个型号的 CCD 温度寄存器基址；不支持每 CCD 温度的型号返回空（LHM Amd17Cpu.UpdateSensors）
std::optional<uint32_t> CcdTemperatureBase(CpuFamilyModel fm);

// Tctl：CUR_TEMP [31:21] × 0.125 ℃；RANGE_SEL [19] 或 TJ_SEL [17:16] = 11b 时减 49 ℃
float DecodeTctl(uint32_t raw);

// CCD 温度：[11:0] × 0.125 − 305 ℃；[11:0] 为 0 表示没有这个 CCD，≥ 125 ℃ 视为无效
std::optional<float> DecodeCcdTemperature(uint32_t raw);

// 能量单位，焦耳 / 计数：0.5 ^ ESU
double EnergyUnitJoules(uint64_t pwr_unit_msr);

// 32 位能量计数的差值，处理回绕
inline uint32_t EnergyDelta(uint32_t previous, uint32_t current) { return current - previous; }

// P0 的频率，MHz。PstateEn [63] 为 0 时返回空。
// 17h / 19h：CpuFid [7:0] / CpuDfsId [13:8] × 200；1Ah：CpuFid [11:0] × 5（LHM Amd17Cpu.Core.UpdateSensors）
std::optional<double> PstateMhz(uint32_t family, uint64_t pstate_msr);

// ---- CPU 厂商 ----
enum class CpuVendorId : uint8_t { Unknown, Amd, Intel };
// CPUID 0 的 EBX / EDX / ECX 拼成的厂商字符串
CpuVendorId DecodeCpuVendor(uint32_t ebx, uint32_t edx, uint32_t ecx);

// ---- Intel（IntelMSR 模块，D-084） ----
// 来源：LHM IntelCpu.cs（Update、GetTjMaxFromMsr、构造函数里的 RAPL 与 TSC 倍频），对照 Intel SDM 第 4 卷
inline constexpr uint32_t kMsrIntelMperf = 0x000000E7;            // IA32_MPERF，按 TSC 频率计数，仅 C0
inline constexpr uint32_t kMsrIntelAperf = 0x000000E8;            // IA32_APERF，按实际频率计数，仅 C0
inline constexpr uint32_t kMsrPlatformInfo = 0x000000CE;          // [15:8] 最大非睿频倍频
inline constexpr uint32_t kMsrTemperatureTarget = 0x000001A2;     // [23:16] TjMax
inline constexpr uint32_t kMsrPackageThermStatus = 0x000001B1;    // [31] 有效，[22:16] 距 TjMax 的度数
inline constexpr uint32_t kMsrRaplPowerUnit = 0x00000606;         // ESU [12:8]
inline constexpr uint32_t kMsrIntelPkgEnergy = 0x00000611;        // 封装能量计数 [31:0]
inline constexpr uint32_t kCpuid6PackageThermal = 1u << 6;        // CPUID 6 EAX：有封装级数字温度传感器
inline constexpr double kIntelBusMhz = 100.0;                     // Sandy Bridge 起的基准外频

// TjMax ℃：IA32_TEMPERATURE_TARGET [23:16]；为 0 时返回空（不假设 100 ℃）
std::optional<float> DecodeTjMax(uint64_t temperature_target_msr);

// 封装温度 = TjMax − [22:16]；[31] 为 0 表示读数无效
std::optional<float> DecodeIntelThermStatus(uint64_t therm_status_msr, float tjmax);

// 基准频率 MHz = PLATFORM_INFO [15:8] × 100；MPERF 按这个频率计数。倍频为 0 时返回空
std::optional<double> IntelBaseMhz(uint64_t platform_info_msr);

// 能量单位，焦耳 / 计数。一般是 0.5 ^ ESU；Silvermont / Airmont 凌动是 2 ^ ESU 微焦（LHM IntelCpu 构造函数）
double IntelEnergyUnitJoules(CpuFamilyModel fm, uint64_t rapl_power_unit_msr);

// ---- Nuvoton NCT6687D（LpcIO 模块） ----
inline constexpr uint8_t kSioEnterKey = 0x87;     // 写两次进入配置模式
inline constexpr uint8_t kSioExitKey = 0xAA;      // 退出配置模式
inline constexpr uint8_t kSioDeviceSelect = 0x07;  // 逻辑设备选择寄存器
inline constexpr uint8_t kSioChipId = 0x20;
inline constexpr uint8_t kSioChipRevision = 0x21;
inline constexpr uint8_t kSioBaseAddress = 0x60;  // 16 位
inline constexpr uint8_t kNuvotonHwmLdn = 0x0B;   // 硬件监控逻辑设备
inline constexpr uint8_t kNct6687Id = 0xD5;
inline constexpr uint8_t kNct6687Revision = 0x92;

// ---- 其他 Super I/O（LpcIO 模块，D-085）。来源 LHM Lpc/LpcIO.cs、LpcPort.cs ----
inline constexpr uint8_t kIteEnterKeys[4] = {0x87, 0x01, 0x55, 0x55};  // 0x4E 上最后一个字节是 0xAA
inline constexpr uint8_t kIteEnterLastKey4E = 0xAA;
inline constexpr uint8_t kIteConfigControl = 0x02;  // 写 0x02 退出配置模式（0x4E 上的第二颗不退出）
inline constexpr uint8_t kIteChipVersion = 0x22;    // [3:0]
inline constexpr uint8_t kIteHwmLdn = 0x04;         // 环境控制器
inline constexpr uint8_t kFintekHwmLdn = 0x04;
inline constexpr uint8_t kF71858HwmLdn = 0x02;
inline constexpr uint8_t kFintekVendorIdReg = 0x23;  // 16 位，应为 0x1934
inline constexpr uint16_t kFintekVendorId = 0x1934;
inline constexpr uint8_t kNuvotonIoLockReg = 0x28;  // 硬件监控逻辑设备里，[4] 为 1 表示 I/O 映射被锁
inline constexpr uint8_t kNuvotonIoLockBit = 0x10;
// 硬件监控寄存器的访问：基址 + 5 是索引，+ 6 是数据（Nuvoton bank 系、ITE、Fintek 相同）
inline constexpr uint16_t kHwmIndexOffset = 0x05;
inline constexpr uint16_t kHwmDataOffset = 0x06;

// Nuvoton bank 系（NCT6779D–NCT6799D）。寄存器写成 bank << 8 | 索引。来源 LHM Lpc/Nct677X.cs
inline constexpr uint8_t kNctBankSelect = 0x4E;
inline constexpr uint16_t kNctVendorIdHigh = 0x804F;
inline constexpr uint16_t kNctVendorIdLow = 0x004F;
inline constexpr uint16_t kNuvotonVendorId = 0x5CA3;
inline constexpr uint16_t kNctFanCountRegs[7] = {0x4B0, 0x4B2, 0x4B4, 0x4B6, 0x4B8, 0x4BA, 0x4CC};
inline constexpr int kNctFanMaxCount = 0x1FFF;  // 13 位计数到顶 = 停转
inline constexpr int kNctFanMinCount = 0x15;

// ITE 环境控制器。来源 LHM Lpc/IT87XX.cs
inline constexpr uint8_t kIteConfigReg = 0x00;  // [4] 应为 1（IT8655E / IT8665E 除外）
inline constexpr uint8_t kIteVendorIdReg = 0x58;  // 0x90 或 0x7F
inline constexpr uint8_t kIteBankReg = 0x06;      // [6:5] 是 bank
inline constexpr uint8_t kIteTempBase = 0x29;
inline constexpr uint8_t kIteFan16Enable = 0x0C;     // 风扇 4 / 5 / 6 的 16 位计数开关
inline constexpr uint8_t kIteFan16EnableAlt = 0x0B;  // IT8665E 第 6 个风扇的开关
inline constexpr uint8_t kIteFanDivisor = 0x0B;      // 8 位计数时风扇 1 / 2 的分频
inline constexpr uint8_t kIteFanLow[6] = {0x0D, 0x0E, 0x0F, 0x80, 0x82, 0x4C};
inline constexpr uint8_t kIteFanHigh[6] = {0x18, 0x19, 0x1A, 0x81, 0x83, 0x4D};
inline constexpr uint8_t kIteFanLowAlt6 = 0x93;   // IT8665E / IT8625E 的第 6 个风扇
inline constexpr uint8_t kIteFanHighAlt6 = 0x94;

// Fintek。来源 LHM Lpc/F718XX.cs
inline constexpr uint8_t kFintekTempBase = 0x70;
inline constexpr uint8_t kF71858TempConfig = 0x69;  // [1:0] 温度表模式
inline constexpr uint8_t kFintekFanRegs[4] = {0xA0, 0xB0, 0xC0, 0xD0};  // 16 位，高字节在前
// EC 空间访问：基址 + 4 页号、+5 索引、+6 数据；页号寄存器读到 0xFF 表示空闲，读完写回 0xFF 释放
inline constexpr uint16_t kEcPageOffset = 0x04;
inline constexpr uint16_t kEcIndexOffset = 0x05;
inline constexpr uint16_t kEcDataOffset = 0x06;
inline constexpr uint8_t kEcPageFree = 0xFF;
inline constexpr uint16_t kNctTempBase = 0x100;  // 11 个通道，每个 2 字节
inline constexpr int kNctTempCount = 11;
inline constexpr uint16_t kNctFanBase = 0x140;  // 16 个通道，每个 2 字节，高字节在前
inline constexpr int kNctFanCount = 16;
inline constexpr uint16_t kNctMonitorControl = 0x180;  // [7] 为 1 表示监控已启用（只读，不写）

// 温度：高字节有符号整数 + 低字节 [7] × 0.5 ℃。0 或 ≥ 127 视为未接（docs/configuration.md）
std::optional<float> DecodeNctTemperature(uint8_t high, uint8_t low);

// 风扇：16 位转速，直接是 RPM
inline int DecodeNctFan(uint8_t high, uint8_t low) { return (high << 8) | low; }

}  // namespace pitot::sensors
