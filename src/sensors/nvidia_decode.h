#pragma once

#include <cstdint>
#include <optional>

// NVML 读数的换算。纯函数，DLL 和单元测试共用。
// 布局和编号来自 NVML API 13.4.92 的公开说明，这里不引用它的头文件（D-081）。

namespace pitot::sensors {

inline constexpr uint32_t kVendorNvidia = 0x10DE;

inline constexpr int kNvmlSuccess = 0;

// nvmlDeviceGetTemperature 的传感器号：0 是核心，1 是芯片上最热的点（热点）
inline constexpr unsigned int kNvmlTempGpu = 0;
inline constexpr unsigned int kNvmlTempHotspot = 1;

// nvmlDeviceGetFieldValues 的显存温度字段。头文件只写了“显存温度”，单位按温度接口的摄氏度处理
inline constexpr unsigned int kNvmlMemoryTempField = 82;

inline constexpr int kNvmlValueUnsignedInt = 1;
inline constexpr int kNvmlValueUnsignedLong = 2;
inline constexpr int kNvmlValueSignedInt = 5;

// NVML_STRUCT_VERSION：低 24 位是结构大小，版本号在高 8 位。风扇转速结构是第一版
inline constexpr unsigned int NvidiaStructVersion(unsigned int struct_size, unsigned int version) {
    return struct_size | (version << 24);
}

// pciDeviceId 是设备号和厂商号拼在一起，高低 16 位哪种在前，不同版本的说法不一致，两种都认。
// want_device_id 为 0 时只要能看出是 NVIDIA 即可（和 ADLX 的“0 = 第一块卡”一样）
inline bool NvidiaDeviceSelected(uint32_t want_device_id, uint32_t packed) {
    const uint32_t lo = packed & 0xFFFFu;
    const uint32_t hi = packed >> 16;
    const bool nvidia = packed != 0 && (lo == kVendorNvidia || hi == kVendorNvidia);
    if (want_device_id == 0) {
        return nvidia;
    }
    return nvidia && (lo == want_device_id || hi == want_device_id);
}

// 成功才有值。超过 250 视为无效：NVML 用 -1 表示没有读数，当成无符号数会变成一个巨大的温度
inline std::optional<float> NvidiaCelsius(int rc, unsigned int degrees) {
    if (rc != kNvmlSuccess || degrees > 250u) {
        return std::nullopt;
    }
    return static_cast<float>(degrees);
}

// nvmlDeviceGetTemperatureV 的温度是有符号整数，负数当作没有读数
inline std::optional<float> NvidiaSignedCelsius(int rc, int degrees) {
    if (degrees < 0) {
        return std::nullopt;
    }
    return NvidiaCelsius(rc, static_cast<unsigned int>(degrees));
}

// 功耗接口的单位是毫瓦，0 是合法读数，全 1 是无效哨兵
inline std::optional<float> NvidiaWatts(int rc, unsigned int milliwatts) {
    if (rc != kNvmlSuccess || milliwatts == 0xFFFFFFFFu) {
        return std::nullopt;
    }
    return static_cast<float>(milliwatts) / 1000.f;
}

// 字段查询要同时看调用本身和这条字段自己的返回码。只认整数摄氏度，不猜别的编码
inline std::optional<float> NvidiaFieldCelsius(int call_rc, int value_rc, int value_type, unsigned int bits) {
    if (call_rc != kNvmlSuccess || value_rc != kNvmlSuccess) {
        return std::nullopt;
    }
    if (value_type == kNvmlValueSignedInt) {
        return NvidiaSignedCelsius(kNvmlSuccess, static_cast<int>(bits));
    }
    if (value_type == kNvmlValueUnsignedInt || value_type == kNvmlValueUnsignedLong) {
        return NvidiaCelsius(kNvmlSuccess, bits);
    }
    return std::nullopt;
}

// 0 转是停转。每分钟两万转以上当成无效哨兵，不显示
inline std::optional<int> NvidiaFanRpm(int rc, unsigned int rpm) {
    if (rc != kNvmlSuccess || rpm > 20000u) {
        return std::nullopt;
    }
    return static_cast<int>(rpm);
}

// 公开 NVAPI 的版本号：低 16 位是结构大小，版本在其上（和 NVML 的摆法不同）
inline constexpr unsigned int NvidiaNvapiVersion(unsigned int struct_size, unsigned int version) {
    return struct_size | (version << 16);
}

// NvAPI_GPU_GetPCIIdentifiers 的设备号有时是纯 16 位，有时和厂商号拼在一起。
// 调用方已经限定是 NVIDIA 的物理卡，所以这里只认设备号，不再要求看见 0x10DE。
// want 为 0 表示第一块卡
inline bool NvidiaNvapiSelected(uint32_t want_device_id, uint32_t device_id, uint32_t ext_device_id) {
    if (want_device_id == 0) {
        return true;
    }
    const auto hit = [want_device_id](uint32_t reported) {
        if (reported == 0) {
            return false;
        }
        const uint32_t lo = reported & 0xFFFFu;
        const uint32_t hi = reported >> 16;
        return lo == want_device_id || hi == want_device_id;
    };
    return hit(device_id) || hit(ext_device_id);
}

// GetThermalSettings 每个传感器 5 个 int32：控制器、默认下限、默认上限、当前温度、目标
inline constexpr int32_t kNvidiaThermalMemory = 2;
inline constexpr uint32_t kNvidiaThermalSensors = 3;

struct NvidiaThermalSensor {
    int32_t controller;
    int32_t min_c;
    int32_t max_c;
    int32_t current_c;
    int32_t target;
};

// 只取目标为显存的那一路。负数是无效；0 ℃保留。公开接口里没有热点这一目标
inline std::optional<float> NvidiaMemoryFromThermal(const NvidiaThermalSensor* sensors, uint32_t count) {
    if (count > kNvidiaThermalSensors) {
        count = kNvidiaThermalSensors;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (sensors[i].target != kNvidiaThermalMemory || sensors[i].current_c < 0) {
            continue;
        }
        if (const auto celsius = NvidiaCelsius(kNvmlSuccess, static_cast<unsigned int>(sensors[i].current_c))) {
            return celsius;
        }
    }
    return std::nullopt;
}

}  // namespace pitot::sensors
