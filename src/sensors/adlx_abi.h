#pragma once

#include <cstdint>

#include "sensors/gpu_extra_abi.h"

// Pitot.exe 与可选组件 PitotAdlx.dll 之间的接口（D-033）。
// 这是 Pitot 自己定义的纯 C 接口，不含任何 ADLX 的类型或代码：ADLX 是 AMD 专有许可，
// 只有 PitotAdlx.dll（src/adlx/）引用它的头文件，Pitot.exe 只认这个文件。
// 改动结构或函数时把 kPitotAdlxAbi 加一，旧 DLL 会被拒绝而不是读错内存。

inline constexpr uint32_t kPitotAdlxAbi = 1;
inline constexpr wchar_t kPitotAdlxDll[] = L"PitotAdlx.dll";

// 字段位和结构与 NVIDIA 共用（gpu_extra_abi.h）。名字留着，已发出的 DLL 仍按这些符号编译
using PitotAdlxField = GpuExtraField;
inline constexpr uint32_t kPitotAdlxHotspot = kGpuExtraHotspot;
inline constexpr uint32_t kPitotAdlxMemoryTemperature = kGpuExtraMemoryTemperature;
inline constexpr uint32_t kPitotAdlxBoardPower = kGpuExtraBoardPower;
inline constexpr uint32_t kPitotAdlxFan = kGpuExtraFan;
inline constexpr uint32_t kPitotAdlxTemperature = kGpuExtraTemperature;

using PitotAdlxMetrics = GpuExtraMetrics;

// 返回值
enum PitotAdlxResult : int32_t {
    kPitotAdlxOk = 0,
    kPitotAdlxAbiMismatch = 1,
    kPitotAdlxNoRuntime = 2,  // 驱动里没有 amdadlx64.dll
    kPitotAdlxInitFailed = 3,
    kPitotAdlxNoGpu = 4,      // 没有匹配的 AMD 显卡
    kPitotAdlxFailed = 5,
};

extern "C" {
// pci_device_id：要用的显卡的 PCI 设备 ID（来自 D3DKMT），0 表示第一块 AMD 卡。
// supported 返回驱动支持的字段（PitotAdlxField 位）
using PitotAdlxOpenFn = int32_t (*)(uint32_t abi, uint32_t pci_device_id, uint32_t* supported);
using PitotAdlxReadFn = int32_t (*)(PitotAdlxMetrics* out);
// 关闭 ADLX 并卸载驱动运行库；之后调用方 FreeLibrary(PitotAdlx.dll)
using PitotAdlxCloseFn = void (*)();
}

inline constexpr char kPitotAdlxOpenName[] = "PitotAdlxOpen";
inline constexpr char kPitotAdlxReadName[] = "PitotAdlxRead";
inline constexpr char kPitotAdlxCloseName[] = "PitotAdlxClose";
