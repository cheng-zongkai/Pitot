// PitotAdlx.dll：Pitot 里唯一引用 AMD ADLX SDK 的组件（D-033）。
//
// ADLX SDK 是 AMD 专有许可：它的头文件不在仓库里（scripts/get-adlx.ps1 下载到 git 忽略的
// third_party/adlx/），这个 DLL 只以编译后的形式发布，并附 AMD 协议要求的最终用户协议。
// Pitot.exe 只通过 sensors/adlx_abi.h 的 C 接口调用这里，自己不含 ADLX 的任何部分。
// 本文件是 Pitot 自己写的代码，没有使用 SDK 的示例代码（ADLXHelper 等）。
//
// 只读指标，不调用任何调校（tuning）接口。

#include <windows.h>

#include <cstdlib>

#include "sensors/adlx_abi.h"

#pragma warning(push, 0)
#include "ADLX.h"
#include "IPerformanceMonitoring.h"
#include "IPerformanceMonitoring2.h"
#pragma warning(pop)

namespace {

using namespace adlx;

struct State {
    HMODULE runtime = nullptr;
    ADLXTerminate_Fn terminate = nullptr;  // 初始化成功后才有，Close 时调用
    IADLXSystem* system = nullptr;         // 归 ADLX 所有，不 Release
    IADLXPerformanceMonitoringServicesPtr monitor;
    IADLXGPUPtr gpu;
    uint32_t supported = 0;
};

State g_state;

void CloseAll() {
    // 接口要在 ADLXTerminate 之前放掉
    g_state.gpu = nullptr;
    g_state.monitor = nullptr;
    if (g_state.terminate) {
        g_state.terminate();
    }
    g_state.terminate = nullptr;
    g_state.system = nullptr;
    if (g_state.runtime) {
        FreeLibrary(g_state.runtime);
    }
    g_state.runtime = nullptr;
    g_state.supported = 0;
}

// ADLX 的设备 ID 是 “0x7590” 这样的十六进制字符串
uint32_t ParseHex(const char* text) { return text ? static_cast<uint32_t>(strtoul(text, nullptr, 16)) : 0; }

// 调用 IsSupported 系列方法。不能写成 Supported(call(&flag), flag)：参数求值顺序不确定，
// MSVC 会先读 flag 再调用
template <typename Fn>
bool Supported(Fn&& is_supported) {
    adlx_bool flag = false;
    return ADLX_SUCCEEDED(is_supported(&flag)) && flag;
}

int32_t Open(uint32_t pci_device_id, uint32_t* supported) {
    // 驱动把 amdadlx64.dll 装在 System32；只从那里加载，不走 DLL 搜索路径（Pitot 以管理员运行）
    g_state.runtime = LoadLibraryExW(ADLX_DLL_NAMEW, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_state.runtime) {
        return kPitotAdlxNoRuntime;
    }
    const auto init = reinterpret_cast<ADLXInitialize_Fn>(GetProcAddress(g_state.runtime, ADLX_INIT_FUNCTION_NAME));
    const auto legacy = reinterpret_cast<ADLXInitialize_Fn>(
        GetProcAddress(g_state.runtime, ADLX_INIT_WITH_INCOMPATIBLE_DRIVER_FUNCTION_NAME));
    const auto terminate =
        reinterpret_cast<ADLXTerminate_Fn>(GetProcAddress(g_state.runtime, ADLX_TERMINATE_FUNCTION_NAME));
    if (!init || !terminate) {
        return kPitotAdlxNoRuntime;
    }
    // 驱动比 SDK 旧时普通初始化会失败，再用兼容模式试一次
    if (ADLX_FAILED(init(ADLX_FULL_VERSION, &g_state.system)) || !g_state.system) {
        g_state.system = nullptr;
        if (!legacy || ADLX_FAILED(legacy(ADLX_FULL_VERSION, &g_state.system)) || !g_state.system) {
            g_state.system = nullptr;
            return kPitotAdlxInitFailed;
        }
    }
    g_state.terminate = terminate;

    IADLXGPUListPtr gpus;
    if (ADLX_FAILED(g_state.system->GetGPUs(&gpus)) || !gpus) {
        return kPitotAdlxNoGpu;
    }
    for (adlx_uint i = gpus->Begin(); i != gpus->End(); ++i) {
        IADLXGPUPtr candidate;
        if (ADLX_FAILED(gpus->At(i, &candidate)) || !candidate) {
            continue;
        }
        const char* id = nullptr;
        candidate->DeviceId(&id);
        if (pci_device_id == 0 || ParseHex(id) == pci_device_id) {
            g_state.gpu = candidate;
            break;
        }
    }
    if (!g_state.gpu) {
        return kPitotAdlxNoGpu;
    }
    if (ADLX_FAILED(g_state.system->GetPerformanceMonitoringServices(&g_state.monitor)) || !g_state.monitor) {
        return kPitotAdlxFailed;
    }

    IADLXGPUMetricsSupportPtr support;
    if (ADLX_FAILED(g_state.monitor->GetSupportedGPUMetrics(g_state.gpu, &support)) || !support) {
        return kPitotAdlxFailed;
    }
    IADLXGPUMetricsSupport* s = support;
    if (Supported([s](adlx_bool* f) { return s->IsSupportedGPUHotspotTemperature(f); })) {
        g_state.supported |= kPitotAdlxHotspot;
    }
    if (Supported([s](adlx_bool* f) { return s->IsSupportedGPUTotalBoardPower(f); })) {
        g_state.supported |= kPitotAdlxBoardPower;
    }
    if (Supported([s](adlx_bool* f) { return s->IsSupportedGPUFanSpeed(f); })) {
        g_state.supported |= kPitotAdlxFan;
    }
    if (Supported([s](adlx_bool* f) { return s->IsSupportedGPUTemperature(f); })) {
        g_state.supported |= kPitotAdlxTemperature;
    }
    // 显存温度在 IADLXGPUMetricsSupport1 上，旧驱动没有这个接口
    IADLXGPUMetricsSupport1Ptr support1(support);
    if (IADLXGPUMetricsSupport1* s1 = support1;
        s1 && Supported([s1](adlx_bool* f) { return s1->IsSupportedGPUMemoryTemperature(f); })) {
        g_state.supported |= kPitotAdlxMemoryTemperature;
    }
    *supported = g_state.supported;
    return kPitotAdlxOk;
}

}  // namespace

extern "C" __declspec(dllexport) int32_t PitotAdlxOpen(uint32_t abi, uint32_t pci_device_id, uint32_t* supported) {
    if (abi != kPitotAdlxAbi || !supported) {
        return kPitotAdlxAbiMismatch;
    }
    *supported = 0;
    CloseAll();
    const int32_t result = Open(pci_device_id, supported);
    if (result != kPitotAdlxOk) {
        CloseAll();
        *supported = 0;
    }
    return result;
}

extern "C" __declspec(dllexport) int32_t PitotAdlxRead(PitotAdlxMetrics* out) {
    if (!out || out->size < sizeof(PitotAdlxMetrics)) {
        return kPitotAdlxAbiMismatch;
    }
    out->valid = 0;
    if (!g_state.monitor || !g_state.gpu) {
        return kPitotAdlxFailed;
    }
    IADLXGPUMetricsPtr metrics;
    if (ADLX_FAILED(g_state.monitor->GetCurrentGPUMetrics(g_state.gpu, &metrics)) || !metrics) {
        return kPitotAdlxFailed;
    }
    const uint32_t s = g_state.supported;
    adlx_double d = 0;
    adlx_int i = 0;
    if ((s & kPitotAdlxHotspot) && ADLX_SUCCEEDED(metrics->GPUHotspotTemperature(&d))) {
        out->hotspot_celsius = d;
        out->valid |= kPitotAdlxHotspot;
    }
    if ((s & kPitotAdlxBoardPower) && ADLX_SUCCEEDED(metrics->GPUTotalBoardPower(&d))) {
        out->board_power_watts = d;
        out->valid |= kPitotAdlxBoardPower;
    }
    if ((s & kPitotAdlxFan) && ADLX_SUCCEEDED(metrics->GPUFanSpeed(&i))) {
        out->fan_rpm = i;
        out->valid |= kPitotAdlxFan;
    }
    if ((s & kPitotAdlxTemperature) && ADLX_SUCCEEDED(metrics->GPUTemperature(&d))) {
        out->temperature_celsius = d;
        out->valid |= kPitotAdlxTemperature;
    }
    if (s & kPitotAdlxMemoryTemperature) {
        IADLXGPUMetrics1Ptr metrics1(metrics);
        if (metrics1 && ADLX_SUCCEEDED(metrics1->GPUMemoryTemperature(&d))) {
            out->memory_celsius = d;
            out->valid |= kPitotAdlxMemoryTemperature;
        }
    }
    return kPitotAdlxOk;
}

extern "C" __declspec(dllexport) void PitotAdlxClose() { CloseAll(); }
