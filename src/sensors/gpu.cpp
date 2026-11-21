#include "sensors/gpu.h"

#include <algorithm>
#include <cwchar>

#include "common/log.h"
#include "sensors/adlx_abi.h"
#include "sensors/nvidia_abi.h"
#include "sensors/nvidia_decode.h"
#include "sensors/parse.h"

namespace pitot::sensors {
namespace {

constexpr uint32_t kVendorAmd = 0x1002;
constexpr int kMaxAdapters = 16;
constexpr ULONGLONG kAdapterRetryMs = 10 * 1000;
// D3DKMT 温度单位是 0.1 ℃
constexpr float kDeciCelsius = 0.1f;

void CloseAdapter(D3DKMT_HANDLE handle) {
    D3DKMT_CLOSEADAPTER close{handle};
    D3DKMTCloseAdapter(&close);
}

}  // namespace

GpuSensor::~GpuSensor() {
    UnloadExtra();
    if (adapter_) {
        CloseAdapter(adapter_);
    }
}

bool GpuSensor::QueryInfo(KMTQUERYADAPTERINFOTYPE type, void* data, UINT size) const {
    D3DKMT_QUERYADAPTERINFO query{};
    query.hAdapter = adapter_;
    query.Type = type;
    query.pPrivateDriverData = data;
    query.PrivateDriverDataSize = size;
    return D3DKMTQueryAdapterInfo(&query) == 0;
}

bool GpuSensor::QueryAdapterStatistics(D3DKMT_QUERYSTATISTICS& stats) const {
    stats.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
    stats.AdapterLuid = luid_;
    return D3DKMTQueryStatistics(&stats) == 0;
}

bool GpuSensor::RefreshAdapter(ULONGLONG now) {
    const bool had_adapter = present();
    if (had_adapter) {
        D3DKMT_QUERYSTATISTICS stats{};
        D3DKMT_ADAPTERTYPE type{};
        const bool has_statistics = QueryAdapterStatistics(stats);
        if (has_statistics == has_statistics_ && QueryInfo(KMTQAITYPE_ADAPTERTYPE, &type, sizeof(type))) {
            return false;
        }

        log::Write(L"gpu: refreshing adapter (luid 0x%08lX_0x%08lX, statistics %d -> %d)",
                   static_cast<unsigned long>(luid_.HighPart), luid_.LowPart, has_statistics_, has_statistics);
        UnloadExtra();
        CloseAdapter(adapter_);
        adapter_ = 0;
    } else if (now < adapter_retry_at_) {
        return false;
    }

    Init();
    adapter_retry_at_ = present() ? 0 : now + kAdapterRetryMs;
    return had_adapter || present();
}

void GpuSensor::Init() {
    D3DKMT_ADAPTERINFO adapters[kMaxAdapters]{};
    D3DKMT_ENUMADAPTERS2 enumerate{};
    enumerate.NumAdapters = kMaxAdapters;
    enumerate.pAdapters = adapters;
    if (D3DKMTEnumAdapters2(&enumerate) != 0) {
        if (adapter_retry_at_ == 0) {
            log::Write(L"gpu: D3DKMTEnumAdapters2 failed");
        }
        return;
    }
    // 显存最大的硬件显卡（混合显卡的笔记本上就是独显）
    int best = -1;
    uint64_t best_vram = 0;
    for (ULONG i = 0; i < enumerate.NumAdapters; ++i) {
        adapter_ = adapters[i].hAdapter;
        D3DKMT_ADAPTERTYPE type{};
        D3DKMT_SEGMENTSIZEINFO segments{};
        if (QueryInfo(KMTQAITYPE_ADAPTERTYPE, &type, sizeof(type)) && type.RenderSupported && !type.SoftwareDevice &&
            QueryInfo(KMTQAITYPE_GETSEGMENTSIZE, &segments, sizeof(segments)) &&
            (best < 0 || segments.DedicatedVideoMemorySize > best_vram)) {
            best = static_cast<int>(i);
            best_vram = segments.DedicatedVideoMemorySize;
        }
    }
    adapter_ = 0;
    for (ULONG i = 0; i < enumerate.NumAdapters; ++i) {
        if (static_cast<int>(i) != best) {
            CloseAdapter(adapters[i].hAdapter);
        }
    }
    if (best < 0) {
        if (adapter_retry_at_ == 0) {
            log::Write(L"gpu: no hardware adapter");
        }
        return;
    }
    adapter_ = adapters[best].hAdapter;
    luid_ = adapters[best].AdapterLuid;
    vram_total_ = best_vram;
    vendor_id_ = 0;
    device_id_ = 0;
    name_[0] = L'\0';
    segment_count_ = 0;
    node_count_ = 0;
    primed_ = false;
    fan_seen_ = false;
    extra_logged_ = false;

    D3DKMT_ADAPTERREGISTRYINFO registry{};
    if (QueryInfo(KMTQAITYPE_ADAPTERREGISTRYINFO, &registry, sizeof(registry))) {
        wcsncpy_s(name_, registry.AdapterString, _TRUNCATE);
    }
    D3DKMT_QUERY_DEVICE_IDS ids{};
    if (QueryInfo(KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &ids, sizeof(ids))) {
        vendor_id_ = ids.DeviceIds.VendorID;
        device_id_ = ids.DeviceIds.DeviceID;
    }

    D3DKMT_QUERYSTATISTICS stats{};
    UINT node_total = 0;
    has_statistics_ = QueryAdapterStatistics(stats);
    if (has_statistics_) {
        node_total = stats.QueryResult.AdapterInformation.NodeCount;
        segment_count_ = stats.QueryResult.AdapterInformation.NbSegments;
    }
    for (UINT n = 0; n < node_total && node_count_ < kMaxNodes; ++n) {
        D3DKMT_NODEMETADATA meta{};
        meta.NodeOrdinalAndAdapterIndex = n;  // 物理适配器 0
        if (QueryInfo(KMTQAITYPE_NODEMETADATA, &meta, sizeof(meta)) &&
            meta.NodeData.EngineType == DXGK_ENGINE_TYPE_3D) {
            nodes_[static_cast<size_t>(node_count_++)] = n;
        }
    }
    log::Write(L"gpu: [%s] %04X:%04X vram %llu MB, %u nodes (%d 3D), %u segments, luid 0x%08lX_0x%08lX", name_,
               vendor_id_, device_id_, vram_total_ >> 20, node_total, node_count_, segment_count_,
               static_cast<unsigned long>(luid_.HighPart), luid_.LowPart);
}

std::optional<int64_t> GpuSensor::RunningTime(UINT node) const {
    D3DKMT_QUERYSTATISTICS stats{};
    stats.Type = D3DKMT_QUERYSTATISTICS_NODE;
    stats.AdapterLuid = luid_;
    stats.QueryNode.NodeId = node;
    if (D3DKMTQueryStatistics(&stats) != 0) {
        return std::nullopt;
    }
    return stats.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart;
}

void GpuSensor::Sample(sampler::GpuReadings& out) {
    out.present = adapter_ != 0;
    if (!adapter_) {
        return;
    }
    wcscpy_s(out.name, name_);
    out.vram_total_bytes = vram_total_;

    // 3D 利用率：多个 3D 节点时取最忙的那个，和任务管理器的 3D 曲线一致
    LARGE_INTEGER now{};
    LARGE_INTEGER freq{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    const auto elapsed =
        static_cast<int64_t>(static_cast<double>(now.QuadPart - last_time_.QuadPart) * 1e7 / static_cast<double>(freq.QuadPart));
    std::optional<double> usage;
    bool all_read = true;
    std::array<int64_t, kMaxNodes> running{};
    for (int i = 0; i < node_count_; ++i) {
        const auto t = RunningTime(nodes_[static_cast<size_t>(i)]);
        if (!t) {
            all_read = false;
            continue;
        }
        running[static_cast<size_t>(i)] = *t;
        if (primed_) {
            if (const auto u = NodeUtilization(last_running_[static_cast<size_t>(i)], *t, elapsed)) {
                usage = std::max(usage.value_or(0.0), *u);
            }
        }
    }
    last_running_ = running;
    last_time_ = now;
    primed_ = all_read && node_count_ > 0;
    out.usage_percent = usage;

    out.clock_mhz.reset();
    if (node_count_ > 0) {
        D3DKMT_NODE_PERFDATA node{};
        node.NodeOrdinal = nodes_[0];
        // 深度空闲时频率是真的 0（时钟门控），查询成功就照实显示
        if (QueryInfo(KMTQAITYPE_NODEPERFDATA, &node, sizeof(node))) {
            out.clock_mhz = static_cast<float>(static_cast<double>(node.Frequency) / 1e6);
        }
    }
    D3DKMT_ADAPTER_PERFDATA perf{};
    out.temperature.reset();
    if (QueryInfo(KMTQAITYPE_ADAPTERPERFDATA, &perf, sizeof(perf))) {
        if (perf.Temperature > 0) {
            out.temperature = static_cast<float>(perf.Temperature) * kDeciCelsius;
        }
        fan_seen_ = fan_seen_ || perf.FanRPM > 0;
        // ADLX 的风扇读数（浮窗打开时）优先，这里只在没有它时填
        if (fan_seen_ && extra_state_ != sampler::GpuExtraState::Ok) {
            out.fan_rpm = static_cast<int>(perf.FanRPM);
        }
    }

    // 显存已用：非 aperture 段（显卡自己的显存）里常驻的字节数，与任务管理器“专用 GPU 内存”一致
    uint64_t resident = 0;
    bool any = false;
    for (UINT s = 0; s < segment_count_; ++s) {
        D3DKMT_QUERYSTATISTICS stats{};
        stats.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
        stats.AdapterLuid = luid_;
        stats.QuerySegment.SegmentId = s;
        if (D3DKMTQueryStatistics(&stats) != 0) {
            continue;
        }
        const auto& info = stats.QueryResult.SegmentInformation;
        if (!info.Aperture) {
            resident += info.BytesResident;
            any = true;
        }
    }
    out.vram_used_bytes = any ? std::optional<uint64_t>(resident) : std::nullopt;
}

void GpuSensor::SetExtra(bool on) {
    if (on) {
        LoadExtra();
    } else {
        UnloadExtra();
    }
}

void GpuSensor::LoadExtra() {
    if (dll_ || !adapter_) {
        return;
    }
    const bool amd = vendor_id_ == kVendorAmd;
    const bool nvidia = vendor_id_ == kVendorNvidia;
    // Intel 等没有扩展指标的卡不提示，基础读数照常显示
    if (!amd && !nvidia) {
        extra_state_ = sampler::GpuExtraState::NotAmd;
        return;
    }
    const wchar_t* dll_name = amd ? kPitotAdlxDll : kPitotNvidiaDll;
    const char* open_name = amd ? kPitotAdlxOpenName : kPitotNvidiaOpenName;
    const char* read_name = amd ? kPitotAdlxReadName : kPitotNvidiaReadName;
    const char* close_name = amd ? kPitotAdlxCloseName : kPitotNvidiaCloseName;
    const uint32_t abi = amd ? kPitotAdlxAbi : kPitotNvidiaAbi;
    const ULONGLONG started = GetTickCount64();
    // 只从 Pitot.exe 所在目录加载，不走 DLL 搜索路径（Pitot 以管理员运行）
    dll_ = LoadLibraryExW(dll_name, nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    if (!dll_) {
        extra_state_ = amd ? sampler::GpuExtraState::NoDll : sampler::GpuExtraState::NoNvidiaDll;
        if (!extra_logged_) {
            extra_logged_ = true;
            log::Write(L"gpu: %s not found, extended metrics hidden", dll_name);
        }
        return;
    }
    using OpenFn = int32_t (*)(uint32_t, uint32_t, uint32_t*);
    const auto open = reinterpret_cast<OpenFn>(GetProcAddress(dll_, open_name));
    read_ = reinterpret_cast<int32_t (*)(GpuExtraMetrics*)>(GetProcAddress(dll_, read_name));
    close_ = reinterpret_cast<void (*)()>(GetProcAddress(dll_, close_name));
    const int32_t result = open && read_ && close_ ? open(abi, device_id_, &supported_) : 1;
    if (!extra_logged_ || result != 0) {
        extra_logged_ = true;
        log::Write(L"gpu: %s open result %d, supported 0x%02X, %llu ms", dll_name, result, supported_,
                   GetTickCount64() - started);
    }
    if (result != 0) {
        UnloadExtra();
        extra_state_ = amd ? sampler::GpuExtraState::Unavailable : sampler::GpuExtraState::NvidiaUnavailable;
        return;
    }
    extra_state_ = sampler::GpuExtraState::Ok;
}

void GpuSensor::UnloadExtra() {
    if (dll_) {
        if (close_) {
            close_();
        }
        FreeLibrary(dll_);
    }
    dll_ = nullptr;
    read_ = nullptr;
    close_ = nullptr;
    supported_ = 0;
    extra_state_ = sampler::GpuExtraState::Off;
}

void GpuSensor::SampleExtra(sampler::GpuReadings& out) {
    out.extra_state = extra_state_;
    out.hotspot.reset();
    out.memory_temperature.reset();
    out.board_power_watts.reset();
    if (extra_state_ != sampler::GpuExtraState::Ok || !read_) {
        if (!fan_seen_) {
            out.fan_rpm.reset();
        }
        return;
    }
    GpuExtraMetrics m{};
    m.size = sizeof(m);
    if (read_(&m) != kPitotAdlxOk) {
        return;
    }
    if (m.valid & kPitotAdlxHotspot) {
        out.hotspot = static_cast<float>(m.hotspot_celsius);
    }
    if (m.valid & kPitotAdlxMemoryTemperature) {
        out.memory_temperature = static_cast<float>(m.memory_celsius);
    }
    if (m.valid & kPitotAdlxBoardPower) {
        out.board_power_watts = static_cast<float>(m.board_power_watts);
    }
    if (m.valid & kPitotAdlxFan) {
        out.fan_rpm = m.fan_rpm;
    } else if (!fan_seen_) {
        out.fan_rpm.reset();
    }
}

}  // namespace pitot::sensors
