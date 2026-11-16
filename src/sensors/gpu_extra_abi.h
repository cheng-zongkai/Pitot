#pragma once

#include <cstdint>

// 浮窗打开时才读的显卡扩展指标，AMD（PitotAdlx.dll）和 NVIDIA（PitotNvidia.dll）共用这一份布局（D-033、D-081）。
// Pitot.exe 只认这个结构，两边的 DLL 都不把厂商 SDK 的类型带进来。
// 改字段或顺序时，两个 DLL 的 ABI 版本都要加一，旧 DLL 会被拒绝而不是读错内存。

enum GpuExtraField : uint32_t {
    kGpuExtraHotspot = 1u << 0,
    kGpuExtraMemoryTemperature = 1u << 1,
    kGpuExtraBoardPower = 1u << 2,
    kGpuExtraFan = 1u << 3,
    kGpuExtraTemperature = 1u << 4,
};

struct GpuExtraMetrics {
    uint32_t size;   // 调用方填 sizeof(GpuExtraMetrics)
    uint32_t valid;  // 哪些字段本轮有值（GpuExtraField）
    double hotspot_celsius;
    double memory_celsius;
    double board_power_watts;
    double temperature_celsius;
    int32_t fan_rpm;
    int32_t reserved;
};
