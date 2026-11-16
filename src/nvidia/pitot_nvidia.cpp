// PitotNvidia.dll：NVIDIA 显卡的扩展指标（D-081）。
//
// 只调用用户驱动里已经有的 nvml.dll 和 nvapi64.dll，不链接、也不收录 NVIDIA 的头文件或导入库。
// NVML 按 API 13.4.92 的公开说明自己声明。消费级卡上风扇和显存温度经常不受支持，
// 这时再用公开 NVAPI 的 GetTachReading / GetThermalSettings 补上（MIT 头文件里的编号，不是未公开接口）。
// 换算在 nvidia_decode.h。Pitot.exe 只通过 sensors/nvidia_abi.h 调用这里。只读。
//
// 这里的 NVML 声明是自己写的，但接口来自 nvml.h（Copyright 1993-2026 NVIDIA Corporation）。
// 那份头文件要求使用者在代码注释和用户文档里带上下面两段声明，原文照录（另见 THIRD_PARTY_NOTICES.md）：
//
// NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOURCE
// CODE FOR ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR
// IMPLIED WARRANTY OF ANY KIND.  NVIDIA DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOURCE CODE, INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
// IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL,
// OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
// OF USE, DATA OR PROFITS,  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION,  ARISING OUT OF OR IN CONNECTION WITH THE USE
// OR PERFORMANCE OF THIS SOURCE CODE.
//
// U.S. Government End Users.   This source code is a "commercial item" as
// that term is defined at  48 C.F.R. 2.101 (OCT 1995), consisting  of
// "commercial computer  software"  and "commercial computer software
// documentation" as such terms are  used in 48 C.F.R. 12.212 (SEPT 1995)
// and is provided to the U.S. Government only as a commercial end item.
// Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through
// 227.7202-4 (JUNE 1995), all U.S. Government End Users acquire the
// source code with only those rights set forth herein.

#include <windows.h>

#include "sensors/nvidia_abi.h"
#include "sensors/nvidia_decode.h"

namespace {

using pitot::sensors::kNvmlMemoryTempField;
using pitot::sensors::kNvmlSuccess;
using pitot::sensors::kNvmlTempHotspot;
using pitot::sensors::NvidiaCelsius;
using pitot::sensors::NvidiaDeviceSelected;
using pitot::sensors::NvidiaFanRpm;
using pitot::sensors::NvidiaFieldCelsius;
using pitot::sensors::NvidiaMemoryFromThermal;
using pitot::sensors::NvidiaNvapiSelected;
using pitot::sensors::NvidiaNvapiVersion;
using pitot::sensors::NvidiaSignedCelsius;
using pitot::sensors::NvidiaStructVersion;
using pitot::sensors::NvidiaThermalSensor;
using pitot::sensors::NvidiaWatts;

// 和 nvmlPciInfo_t 同一布局：旧总线号 16 字节在前，然后五个 uint32，再是 32 字节的总线号
struct PciInfo {
    char bus_id_legacy[16];
    unsigned int domain;
    unsigned int bus;
    unsigned int device;
    unsigned int pci_device_id;
    unsigned int pci_subsystem_id;
    char bus_id[32];
};

// nvmlValue_t。最大的成员是 8 字节，Windows 上 unsigned long 是 4 字节也不改变联合体的大小
union Value {
    double d_val;
    int si_val;
    unsigned int ui_val;
    unsigned long ul_val;
    unsigned long long ull_val;
    long long sll_val;
    unsigned short us_val;
};

struct FieldValue {
    unsigned int field_id;
    unsigned int scope_id;
    long long timestamp;
    long long latency_usec;
    int value_type;
    int nvml_return;
    Value value;
};

struct FanSpeed {
    unsigned int version;
    unsigned int fan;
    unsigned int speed;
};

// nvmlTemperature_v1_t
struct Temperature {
    unsigned int version;
    unsigned int sensor;
    int celsius;
};

static_assert(sizeof(PciInfo) == 68);
static_assert(sizeof(Temperature) == 12);
static_assert(sizeof(Value) == 8);
static_assert(sizeof(FieldValue) == 40);
static_assert(sizeof(FanSpeed) == 12);

using Device = void*;
using InitFn = int (*)();
using ShutdownFn = int (*)();
using CountFn = int (*)(unsigned int*);
using HandleFn = int (*)(unsigned int, Device*);
using PciFn = int (*)(Device, PciInfo*);
using TempFn = int (*)(Device, unsigned int, unsigned int*);
using TempVFn = int (*)(Device, Temperature*);
using PowerFn = int (*)(Device, unsigned int*);
using FieldsFn = int (*)(Device, int, FieldValue*);
using FanFn = int (*)(Device, FanSpeed*);

// 公开 nvapi.h 的热传感器结构第二版。三个传感器，每个五个 int32
struct ThermalSettings {
    unsigned int version;
    unsigned int count;
    NvidiaThermalSensor sensor[3];
};
static_assert(sizeof(ThermalSettings) == 68);

using NvStatus = int;
using NvInitFn = NvStatus (*)();
using NvUnloadFn = NvStatus (*)();
using NvEnumFn = NvStatus (*)(void** gpus, unsigned int* count);
using NvPciFn = NvStatus (*)(void* gpu, unsigned int* device_id, unsigned int* subsystem, unsigned int* revision, unsigned int* ext_device_id);
using NvTachFn = NvStatus (*)(void* gpu, unsigned int* rpm);
using NvThermalFn = NvStatus (*)(void* gpu, unsigned int index, ThermalSettings* settings);
using NvQueryFn = void* (*)(unsigned int id);

// 公开 nvapi_interface.h 里的编号。未公开的热点数组和功耗拓扑不在这里
constexpr unsigned int kNvInitialize = 0x0150e828;
constexpr unsigned int kNvUnload = 0xd22bdd7e;
constexpr unsigned int kNvEnumGpus = 0xe5ac921f;
constexpr unsigned int kNvPciIds = 0x2ddfb66e;
constexpr unsigned int kNvTach = 0x5f608315;
constexpr unsigned int kNvThermal = 0xe3640a56;
constexpr unsigned int kNvThermalAll = 15;

struct State {
    HMODULE module = nullptr;
    ShutdownFn shutdown = nullptr;
    TempFn temperature = nullptr;
    TempVFn temperature_v = nullptr;
    PowerFn power = nullptr;
    FieldsFn fields = nullptr;
    FanFn fan = nullptr;
    Device device = nullptr;
    uint32_t supported = 0;
    uint32_t nvml_supported = 0;  // 其中由 NVML 提供的那些；NVAPI 补上的项不必每轮先问一次 NVML
    bool started = false;  // nvmlInit_v2 成功后才能 Shutdown

    HMODULE nvapi = nullptr;
    NvUnloadFn nv_unload = nullptr;
    NvTachFn nv_tach = nullptr;
    NvThermalFn nv_thermal = nullptr;
    void* nv_gpu = nullptr;
    bool nv_started = false;
};

State g_state;

void ReleaseNvapi() {
    if (g_state.nv_started && g_state.nv_unload) {
        g_state.nv_unload();
    }
    if (g_state.nvapi) {
        FreeLibrary(g_state.nvapi);
    }
    g_state.nvapi = nullptr;
    g_state.nv_unload = nullptr;
    g_state.nv_tach = nullptr;
    g_state.nv_thermal = nullptr;
    g_state.nv_gpu = nullptr;
    g_state.nv_started = false;
}

void CloseAll() {
    if (g_state.started && g_state.shutdown) {
        g_state.shutdown();
    }
    if (g_state.module) {
        FreeLibrary(g_state.module);
    }
    ReleaseNvapi();
    g_state = {};
}

// 驱动把 nvml.dll 放在 System32，文档里的位置是 NVSMI 目录。两条都不走默认搜索路径
// （Pitot 以管理员运行）。NVSMI 那份要靠同目录里的依赖，所以用 ALTERED_SEARCH_PATH
HMODULE LoadNvml() {
    if (const HMODULE mod = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        return mod;
    }
    wchar_t path[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"ProgramW6432", path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return nullptr;
    }
    if (wcscat_s(path, L"\\NVIDIA Corporation\\NVSMI\\nvml.dll") != 0) {
        return nullptr;
    }
    return LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

template <typename Fn>
Fn Proc(HMODULE module, const char* name) {
    return reinterpret_cast<Fn>(GetProcAddress(module, name));
}

// 只从 System32 加载。nvapi64.dll 随驱动放在那里，不走默认搜索路径
void* NvProc(unsigned int id) {
    const auto query = Proc<NvQueryFn>(g_state.nvapi, "nvapi_QueryInterface");
    return query ? query(id) : nullptr;
}

int32_t TryNvapi(uint32_t pci_device_id) {
    const bool need_memory = (g_state.supported & kGpuExtraMemoryTemperature) == 0;
    const bool need_fan = (g_state.supported & kGpuExtraFan) == 0;
    if (g_state.device && !need_memory && !need_fan) {
        return kPitotNvidiaOk;
    }
    g_state.nvapi = LoadLibraryExW(L"nvapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_state.nvapi) {
        return kPitotNvidiaNoRuntime;
    }
    const auto init = reinterpret_cast<NvInitFn>(NvProc(kNvInitialize));
    g_state.nv_unload = reinterpret_cast<NvUnloadFn>(NvProc(kNvUnload));
    const auto enumerate = reinterpret_cast<NvEnumFn>(NvProc(kNvEnumGpus));
    const auto pci_of = reinterpret_cast<NvPciFn>(NvProc(kNvPciIds));
    g_state.nv_tach = reinterpret_cast<NvTachFn>(NvProc(kNvTach));
    g_state.nv_thermal = reinterpret_cast<NvThermalFn>(NvProc(kNvThermal));
    if (!init || !g_state.nv_unload || !enumerate || !pci_of) {
        return kPitotNvidiaInitFailed;
    }
    if (init() != kNvmlSuccess) {
        return kPitotNvidiaInitFailed;
    }
    g_state.nv_started = true;

    void* gpus[64]{};
    unsigned int count = 0;
    if (enumerate(gpus, &count) != kNvmlSuccess || count == 0) {
        return kPitotNvidiaNoGpu;
    }
    if (count > 64) {
        count = 64;
    }
    for (unsigned int i = 0; i < count; ++i) {
        unsigned int device_id = 0;
        unsigned int subsystem = 0;
        unsigned int revision = 0;
        unsigned int ext_device_id = 0;
        if (!gpus[i] || pci_of(gpus[i], &device_id, &subsystem, &revision, &ext_device_id) != kNvmlSuccess) {
            continue;
        }
        static_cast<void>(subsystem);
        static_cast<void>(revision);
        if (NvidiaNvapiSelected(pci_device_id, device_id, ext_device_id)) {
            g_state.nv_gpu = gpus[i];
            break;
        }
    }
    if (!g_state.nv_gpu) {
        return kPitotNvidiaNoGpu;
    }
    if (need_memory && g_state.nv_thermal) {
        ThermalSettings thermal{NvidiaNvapiVersion(sizeof(ThermalSettings), 2), 0, {}};
        const int call = g_state.nv_thermal(g_state.nv_gpu, kNvThermalAll, &thermal);
        if (call == kNvmlSuccess && NvidiaMemoryFromThermal(thermal.sensor, thermal.count)) {
            g_state.supported |= kGpuExtraMemoryTemperature;
        }
    }
    if (need_fan && g_state.nv_tach) {
        unsigned int rpm = 0;
        const int call = g_state.nv_tach(g_state.nv_gpu, &rpm);
        if (NvidiaFanRpm(call, rpm)) {
            g_state.supported |= kGpuExtraFan;
        }
    }
    return kPitotNvidiaOk;
}

// NVML 13 起旧的 nvmlDeviceGetTemperature 标了弃用，新接口是 nvmlDeviceGetTemperatureV。
// 新的先试，旧驱动没有或不认就退回旧的；两个都不是必需的导出，缺了只少热点这一项
std::optional<float> ReadHotspot() {
    if (!g_state.device) {
        return std::nullopt;
    }
    if (g_state.temperature_v) {
        Temperature reading{NvidiaStructVersion(sizeof(Temperature), 1), kNvmlTempHotspot, 0};
        const int call = g_state.temperature_v(g_state.device, &reading);
        if (const auto celsius = NvidiaSignedCelsius(call, reading.celsius)) {
            return celsius;
        }
    }
    if (g_state.temperature) {
        unsigned int raw = 0;
        const int call = g_state.temperature(g_state.device, kNvmlTempHotspot, &raw);
        return NvidiaCelsius(call, raw);
    }
    return std::nullopt;
}

int32_t TryNvml(uint32_t pci_device_id) {
    g_state.module = LoadNvml();
    if (!g_state.module) {
        return kPitotNvidiaNoRuntime;
    }
    const auto init = Proc<InitFn>(g_state.module, "nvmlInit_v2");
    g_state.shutdown = Proc<ShutdownFn>(g_state.module, "nvmlShutdown");
    const auto count_of = Proc<CountFn>(g_state.module, "nvmlDeviceGetCount_v2");
    const auto handle_at = Proc<HandleFn>(g_state.module, "nvmlDeviceGetHandleByIndex_v2");
    const auto pci_of = Proc<PciFn>(g_state.module, "nvmlDeviceGetPciInfo_v3");
    g_state.temperature = Proc<TempFn>(g_state.module, "nvmlDeviceGetTemperature");
    g_state.temperature_v = Proc<TempVFn>(g_state.module, "nvmlDeviceGetTemperatureV");
    g_state.power = Proc<PowerFn>(g_state.module, "nvmlDeviceGetPowerUsage");
    g_state.fields = Proc<FieldsFn>(g_state.module, "nvmlDeviceGetFieldValues");
    g_state.fan = Proc<FanFn>(g_state.module, "nvmlDeviceGetFanSpeedRPM");
    // 只有找卡用的这几个是必需的；各项读数的导出缺哪个就少哪一项
    if (!init || !g_state.shutdown || !count_of || !handle_at || !pci_of) {
        return kPitotNvidiaInitFailed;
    }
    if (init() != kNvmlSuccess) {
        return kPitotNvidiaInitFailed;
    }
    g_state.started = true;

    unsigned int count = 0;
    if (count_of(&count) != kNvmlSuccess || count == 0) {
        return kPitotNvidiaNoGpu;
    }
    if (count > 64) {
        count = 64;
    }
    for (unsigned int i = 0; i < count; ++i) {
        Device candidate = nullptr;
        PciInfo pci{};
        if (handle_at(i, &candidate) != kNvmlSuccess || !candidate || pci_of(candidate, &pci) != kNvmlSuccess) {
            continue;
        }
        if (NvidiaDeviceSelected(pci_device_id, pci.pci_device_id)) {
            g_state.device = candidate;
            break;
        }
    }
    if (!g_state.device) {
        return kPitotNvidiaNoGpu;
    }

    if (ReadHotspot()) {
        g_state.supported |= kGpuExtraHotspot;
    }
    if (g_state.fields) {
        FieldValue field{};
        field.field_id = kNvmlMemoryTempField;
        // 调用和读结果必须分开：参数的求值顺序不确定，合在一个表达式里会读到调用前的值
        const int call = g_state.fields(g_state.device, 1, &field);
        if (NvidiaFieldCelsius(call, field.nvml_return, field.value_type, field.value.ui_val)) {
            g_state.supported |= kGpuExtraMemoryTemperature;
        }
    }
    if (g_state.power) {
        unsigned int raw = 0;
        const int call = g_state.power(g_state.device, &raw);
        if (NvidiaWatts(call, raw)) {
            g_state.supported |= kGpuExtraBoardPower;
        }
    }
    // 面板只有一个风扇读数，取 0 号风扇。消费级卡上这个接口经常没有，没有就留给公开 NVAPI 或 D3DKMT
    if (g_state.fan) {
        FanSpeed fan{NvidiaStructVersion(sizeof(FanSpeed), 1), 0, 0};
        const int call = g_state.fan(g_state.device, &fan);
        if (NvidiaFanRpm(call, fan.speed)) {
            g_state.supported |= kGpuExtraFan;
        }
    }
    g_state.nvml_supported = g_state.supported;
    return kPitotNvidiaOk;
}

int32_t Open(uint32_t pci_device_id, uint32_t* supported) {
    const int32_t nvml = TryNvml(pci_device_id);
    const uint32_t before = g_state.supported & (kGpuExtraMemoryTemperature | kGpuExtraFan);
    const int32_t nvapi = TryNvapi(pci_device_id);
    const uint32_t after = g_state.supported & (kGpuExtraMemoryTemperature | kGpuExtraFan);
    // 没补上缺的项就卸掉，避免空占着 nvapi64.dll
    if (g_state.nvapi && before == after) {
        ReleaseNvapi();
    }
    if (g_state.device || g_state.nv_gpu) {
        *supported = g_state.supported;
        return kPitotNvidiaOk;
    }
    if (nvml == kPitotNvidiaNoRuntime && nvapi == kPitotNvidiaNoRuntime) {
        return kPitotNvidiaNoRuntime;
    }
    if (nvml == kPitotNvidiaNoGpu || nvapi == kPitotNvidiaNoGpu) {
        return kPitotNvidiaNoGpu;
    }
    return nvml != kPitotNvidiaOk ? nvml : nvapi;
}

int32_t Read(PitotNvidiaMetrics* out) {
    out->valid = 0;
    if (!g_state.device && !g_state.nv_gpu) {
        return kPitotNvidiaFailed;
    }
    const uint32_t supported = g_state.supported;
    const uint32_t nvml = g_state.device ? g_state.nvml_supported : 0;
    if (nvml & kGpuExtraHotspot) {
        if (const auto celsius = ReadHotspot()) {
            out->hotspot_celsius = *celsius;
            out->valid |= kGpuExtraHotspot;
        }
    }
    if ((nvml & kGpuExtraMemoryTemperature) && g_state.fields) {
        FieldValue field{};
        field.field_id = kNvmlMemoryTempField;
        const int call = g_state.fields(g_state.device, 1, &field);
        if (const auto celsius = NvidiaFieldCelsius(call, field.nvml_return, field.value_type, field.value.ui_val)) {
            out->memory_celsius = *celsius;
            out->valid |= kGpuExtraMemoryTemperature;
        }
    }
    if ((supported & kGpuExtraMemoryTemperature) && (out->valid & kGpuExtraMemoryTemperature) == 0 && g_state.nv_thermal &&
        g_state.nv_gpu) {
        ThermalSettings thermal{NvidiaNvapiVersion(sizeof(ThermalSettings), 2), 0, {}};
        const int call = g_state.nv_thermal(g_state.nv_gpu, kNvThermalAll, &thermal);
        if (call == kNvmlSuccess) {
            if (const auto celsius = NvidiaMemoryFromThermal(thermal.sensor, thermal.count)) {
                out->memory_celsius = *celsius;
                out->valid |= kGpuExtraMemoryTemperature;
            }
        }
    }
    if ((nvml & kGpuExtraBoardPower) && g_state.power) {
        unsigned int raw = 0;
        const int call = g_state.power(g_state.device, &raw);
        if (const auto watts = NvidiaWatts(call, raw)) {
            out->board_power_watts = *watts;
            out->valid |= kGpuExtraBoardPower;
        }
    }
    if ((nvml & kGpuExtraFan) && g_state.fan) {
        FanSpeed fan{NvidiaStructVersion(sizeof(FanSpeed), 1), 0, 0};
        const int call = g_state.fan(g_state.device, &fan);
        if (const auto rpm = NvidiaFanRpm(call, fan.speed)) {
            out->fan_rpm = *rpm;
            out->valid |= kGpuExtraFan;
        }
    }
    if ((supported & kGpuExtraFan) && (out->valid & kGpuExtraFan) == 0 && g_state.nv_tach && g_state.nv_gpu) {
        unsigned int rpm = 0;
        const int call = g_state.nv_tach(g_state.nv_gpu, &rpm);
        if (const auto value = NvidiaFanRpm(call, rpm)) {
            out->fan_rpm = *value;
            out->valid |= kGpuExtraFan;
        }
    }
    return kPitotNvidiaOk;
}

}  // namespace

extern "C" __declspec(dllexport) int32_t PitotNvidiaOpen(uint32_t abi, uint32_t pci_device_id, uint32_t* supported) {
    if (abi != kPitotNvidiaAbi || !supported) {
        return kPitotNvidiaAbiMismatch;
    }
    *supported = 0;
    CloseAll();
    const int32_t result = Open(pci_device_id, supported);
    if (result != kPitotNvidiaOk) {
        CloseAll();
        *supported = 0;
    }
    return result;
}

extern "C" __declspec(dllexport) int32_t PitotNvidiaRead(PitotNvidiaMetrics* out) {
    if (!out || out->size < sizeof(PitotNvidiaMetrics)) {
        return kPitotNvidiaAbiMismatch;
    }
    return Read(out);
}

extern "C" __declspec(dllexport) void PitotNvidiaClose() { CloseAll(); }
