#pragma once

#include <cstdint>

#include "sensors/gpu_extra_abi.h"

// Pitot.exe 与可选组件 PitotNvidia.dll 之间的接口（D-081）。
// DLL 只调用驱动已经装好的 nvml.dll 和 nvapi64.dll，仓库里没有 NVIDIA 的头文件或导入库。
// 改动结构或函数时把 kPitotNvidiaAbi 加一，旧 DLL 会被拒绝而不是读错内存。

inline constexpr uint32_t kPitotNvidiaAbi = 1;
inline constexpr wchar_t kPitotNvidiaDll[] = L"PitotNvidia.dll";

using PitotNvidiaMetrics = GpuExtraMetrics;

enum PitotNvidiaResult : int32_t {
    kPitotNvidiaOk = 0,
    kPitotNvidiaAbiMismatch = 1,
    kPitotNvidiaNoRuntime = 2,  // 驱动里既没有 nvml.dll 也没有 nvapi64.dll
    kPitotNvidiaInitFailed = 3,
    kPitotNvidiaNoGpu = 4,  // 没有和 D3DKMT 选出的那块卡对上的 NVIDIA 设备
    kPitotNvidiaFailed = 5,
};

extern "C" {
// pci_device_id：D3DKMT 的 PCI 设备号，0 表示第一块 NVIDIA 卡。
// supported 返回驱动这次能读到的字段（GpuExtraField）
using PitotNvidiaOpenFn = int32_t (*)(uint32_t abi, uint32_t pci_device_id, uint32_t* supported);
using PitotNvidiaReadFn = int32_t (*)(PitotNvidiaMetrics* out);
using PitotNvidiaCloseFn = void (*)();
}

inline constexpr char kPitotNvidiaOpenName[] = "PitotNvidiaOpen";
inline constexpr char kPitotNvidiaReadName[] = "PitotNvidiaRead";
inline constexpr char kPitotNvidiaCloseName[] = "PitotNvidiaClose";
