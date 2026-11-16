#pragma once

#include <cstdint>
#include <cwchar>

#include "config/config.h"
#include "sampler/snapshot.h"
#include "sensors/decode.h"

// 传感器的稳定键（D-070）：设置窗口和 [sensor.<键>] 用它认出同一个传感器。
// 键只由硬件位置决定（寄存器地址、CCD 序号、磁盘编号），不随界面语言和别名变。

namespace pitot::sampler {

inline constexpr int kSensorKeyChars = config::kSensorKeyChars;
using SensorKey = wchar_t[kSensorKeyChars];

inline constexpr wchar_t kTctlKey[] = L"amd.tctl";
inline constexpr wchar_t kIntelPackageKey[] = L"intel.package";
inline constexpr wchar_t kGpuCoreKey[] = L"gpu.core";
inline constexpr wchar_t kGpuHotspotKey[] = L"gpu.hotspot";
inline constexpr wchar_t kGpuMemoryKey[] = L"gpu.memory";
inline constexpr wchar_t kGpuFanKey[] = L"gpu.fan";

// CCD 从 1 起编号，和面板上的“CCD1”一致
inline void CcdKey(int index, SensorKey& out) { _snwprintf_s(out, _TRUNCATE, L"amd.ccd%d", index + 1); }

// 主板通道的键（D-085）：芯片名 + 通道。Nuvoton EC 系用寄存器地址（nct6687d.0x104，和 D-070 的旧键一致），
// Nuvoton bank 系的温度用来源名（nct6798d.systin），其他从 1 起编号（it8689e.temp1、it8689e.fan2）
inline void BoardTempKey(const BoardChannel& ch, SensorKey& out) {
    const sensors::BoardChipInfo& info = sensors::ChipInfo(ch.chip);
    switch (info.family) {
    case sensors::BoardFamily::NuvotonEc:
        _snwprintf_s(out, _TRUNCATE, L"%s.0x%03x", info.key, sensors::kNctTempBase + 2 * ch.id);
        break;
    case sensors::BoardFamily::Nuvoton:
        _snwprintf_s(out, _TRUNCATE, L"%s.%s", info.key, sensors::NctSourceKey(ch.id));
        break;
    default:
        _snwprintf_s(out, _TRUNCATE, L"%s.temp%d", info.key, ch.id + 1);
        break;
    }
}

inline void FanKey(const BoardChannel& ch, SensorKey& out) {
    const sensors::BoardChipInfo& info = sensors::ChipInfo(ch.chip);
    if (info.family == sensors::BoardFamily::NuvotonEc) {
        _snwprintf_s(out, _TRUNCATE, L"%s.0x%03x", info.key, sensors::kNctFanBase + 2 * ch.id);
    } else {
        _snwprintf_s(out, _TRUNCATE, L"%s.fan%d", info.key, ch.id + 1);
    }
}

inline void NvmeKey(uint32_t disk_number, SensorKey& out) { _snwprintf_s(out, _TRUNCATE, L"nvme.%u", disk_number); }

// CPU 温度的键随厂商变（D-084）：AMD 是 Tctl，Intel 是封装温度，两者不是同一个量
inline const wchar_t* CpuTempKey(CpuVendor vendor) {
    return vendor == CpuVendor::Intel ? kIntelPackageKey : kTctlKey;
}

// 用户隐藏了哪些温度（D-070）。按位记，下标同 SensorReadings / StorageReadings。
// 只用于“最高温度”这类汇总：被隐藏的通道不再参与，任务栏条上明确选的单个来源不受影响
struct SensorMask {
    bool cpu_temp = true;
    uint32_t ccd = 0xFFFFFFFFu;
    uint32_t board = 0xFFFFFFFFu;
    bool gpu_core = true;
    bool gpu_hotspot = true;
    bool gpu_memory = true;
    uint32_t disks = 0xFFFFFFFFu;  // 按磁盘编号（PhysicalDriveN，N < 32）
    uint16_t board_layout = 0;     // 算 board 时的主板通道表；和快照里的不一样就要重算
};

// 主板通道的键取决于识别到的芯片，所以要传入读数；芯片识别完成后（board_layout 变了）调用方重算一次
inline SensorMask MakeSensorMask(const config::SensorsConfig& sensors, const SensorReadings& readings) {
    SensorMask mask;
    mask.board_layout = readings.board_layout;
    if (sensors.setting_count == 0) {
        return mask;
    }
    SensorKey key;
    // 一台机器只会有其中一个键，另一个永远是默认的“显示”
    mask.cpu_temp = config::SensorVisible(sensors, kTctlKey) && config::SensorVisible(sensors, kIntelPackageKey);
    for (int i = 0; i < kCcds; ++i) {
        CcdKey(i, key);
        if (!config::SensorVisible(sensors, key)) {
            mask.ccd &= ~(1u << i);
        }
    }
    for (int i = 0; i < kBoardTemps; ++i) {
        const BoardChannel& ch = readings.board_temp_ch[static_cast<size_t>(i)];
        if (ch.chip == sensors::BoardChip::None) {
            continue;
        }
        BoardTempKey(ch, key);
        if (!config::SensorVisible(sensors, key)) {
            mask.board &= ~(1u << i);
        }
    }
    mask.gpu_core = config::SensorVisible(sensors, kGpuCoreKey);
    mask.gpu_hotspot = config::SensorVisible(sensors, kGpuHotspotKey);
    mask.gpu_memory = config::SensorVisible(sensors, kGpuMemoryKey);
    for (uint32_t n = 0; n < 32; ++n) {
        NvmeKey(n, key);
        if (!config::SensorVisible(sensors, key)) {
            mask.disks &= ~(1u << n);
        }
    }
    return mask;
}

inline bool DiskVisible(const SensorMask& mask, uint32_t number) {
    return number >= 32 || (mask.disks & (1u << number)) != 0;
}

// 没被隐藏的主板通道里的最高温度，任务栏条的主板温度项用
inline std::optional<double> MaxBoardTemperature(const SensorReadings& r, const SensorMask& mask) {
    std::optional<double> best;
    for (size_t i = 0; i < r.board_temps.size(); ++i) {
        if ((r.board_temp_seen & mask.board & (1u << i)) && r.board_temps[i] && (!best || *r.board_temps[i] > *best)) {
            best = *r.board_temps[i];
        }
    }
    return best;
}

// 没被隐藏的磁盘里的最高温度（只有 NVMe 有），任务栏条的 SSD 温度项用
inline std::optional<double> MaxStorageTemperature(const StorageReadings& s, const SensorMask& mask) {
    std::optional<double> best;
    for (int i = 0; i < s.count; ++i) {
        const DiskReadings& d = s.disks[static_cast<size_t>(i)];
        if (d.temperature && DiskVisible(mask, d.number) && (!best || *d.temperature > *best)) {
            best = *d.temperature;
        }
    }
    return best;
}

// 传感器磁贴、高温通知用的最高温度：CPU、主板、显卡核心、存储里没被隐藏的。
// 不含热点 / 显存温度：它们只在浮窗打开时有（ADLX），算进来的话趋势线一开浮窗就跳一下
inline std::optional<double> MaxTemperature(const SensorReadings& r, const GpuReadings& g, const StorageReadings& s,
                                            const SensorMask& mask) {
    std::optional<double> best;
    auto take = [&](const std::optional<float>& t) {
        if (t && (!best || *t > *best)) {
            best = *t;
        }
    };
    if (mask.cpu_temp) {
        take(r.cpu_temp);
    }
    for (size_t i = 0; i < r.ccd.size(); ++i) {
        if (mask.ccd & (1u << i)) {
            take(r.ccd[i]);
        }
    }
    if (const auto board = MaxBoardTemperature(r, mask); board && (!best || *board > *best)) {
        best = board;
    }
    if (mask.gpu_core) {
        take(g.temperature);
    }
    if (const auto disk = MaxStorageTemperature(s, mask); disk && (!best || *disk > *best)) {
        best = disk;
    }
    return best;
}

}  // namespace pitot::sampler
