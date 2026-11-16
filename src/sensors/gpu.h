#pragma once

#include <windows.h>

// d3dkmthk.h 要用 NTSTATUS，WIN32_LEAN_AND_MEAN 下 windows.h 不带它
#include <winternl.h>

#include <d3dkmthk.h>

#include <array>
#include <cstdint>
#include <optional>

#include "sampler/snapshot.h"
#include "sensors/gpu_extra_abi.h"

namespace pitot::sensors {

// 显卡（D-033）。
// 基础指标走 Windows D3DKMT，任何显卡都能用，2 秒一次常驻：3D 利用率（3D 节点累计运行时间差值）、
// 3D 节点频率、温度（驱动提供时）、显存已用（非 aperture 显存段的 BytesResident 之和）/ 总量。
// 扩展指标（热点 / 显存温度、板卡功耗、风扇）只在浮窗打开时加载，关闭时卸载：
// AMD 走可选的 PitotAdlx.dll（加载后常驻多占约 7 MB 内存和 9 个线程，D-033），
// NVIDIA 走可选的 PitotNvidia.dll，调用驱动里的 nvml.dll（D-081）。
class GpuSensor {
public:
    GpuSensor() = default;
    ~GpuSensor();
    GpuSensor(const GpuSensor&) = delete;
    GpuSensor& operator=(const GpuSensor&) = delete;

    // 选出显存最大的硬件显卡
    void Init();
    void Sample(sampler::GpuReadings& out);

    // 浮窗打开 / 关闭
    void SetExtra(bool on);
    void SampleExtra(sampler::GpuReadings& out);

    [[nodiscard]] bool present() const noexcept { return adapter_ != 0; }
    [[nodiscard]] LUID luid() const noexcept { return luid_; }

private:
    static constexpr int kMaxNodes = 4;

    bool QueryInfo(KMTQUERYADAPTERINFOTYPE type, void* data, UINT size) const;
    std::optional<int64_t> RunningTime(UINT node) const;
    void LoadExtra();
    void UnloadExtra();

    D3DKMT_HANDLE adapter_ = 0;
    LUID luid_{};
    uint32_t vendor_id_ = 0;
    uint32_t device_id_ = 0;
    wchar_t name_[64]{};
    uint64_t vram_total_ = 0;
    UINT segment_count_ = 0;
    std::array<UINT, kMaxNodes> nodes_{};  // 3D 引擎节点
    int node_count_ = 0;

    std::array<int64_t, kMaxNodes> last_running_{};
    LARGE_INTEGER last_time_{};
    bool primed_ = false;
    bool fan_seen_ = false;  // D3DKMT 的风扇读数为 0 时分不清“停转”和“不支持”，出现过非 0 才算支持

    HMODULE dll_ = nullptr;
    // 两个 DLL 的读、关函数都是这个签名（GpuExtraMetrics）
    int32_t (*read_)(GpuExtraMetrics*) = nullptr;
    void (*close_)() = nullptr;
    uint32_t supported_ = 0;
    sampler::GpuExtraState extra_state_ = sampler::GpuExtraState::Off;
    bool extra_logged_ = false;
};

}  // namespace pitot::sensors
