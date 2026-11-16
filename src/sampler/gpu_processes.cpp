#include "sampler/gpu_processes.h"

// d3dkmthk.h 要用 NTSTATUS，WIN32_LEAN_AND_MEAN 下 windows.h 不带它
#include <winternl.h>

#include <d3dkmthk.h>
#include <pdhmsg.h>

#include <algorithm>

#include "common/log.h"
#include "sensors/parse.h"

namespace pitot::sampler {
namespace {

constexpr size_t kReserveEntries = 2048;

bool Valid(DWORD cstatus) { return cstatus == PDH_CSTATUS_VALID_DATA || cstatus == PDH_CSTATUS_NEW_DATA; }

// 系统里没有匹配的 3D 引擎实例时 PDH 报 PDH_NO_DATA（本机用不存在的引擎类型实测过）。
// 意思是“这一刻没有进程用这个引擎”，列表应当为空，不是“还没算好”；当成没有结果，独显空闲时面板会一直“正在统计”
bool NoInstances(PDH_STATUS status) { return status == static_cast<PDH_STATUS>(PDH_NO_DATA); }

}  // namespace

bool GpuProcessSampler::Start(LUID luid) {
    if (query_) {
        return true;
    }
    luid_ = luid;
    reported_ = false;
    // 哪些显存段是显卡自己的显存（每次开面板查一次，便宜）
    segment_count_ = 0;
    D3DKMT_QUERYSTATISTICS adapter{};
    adapter.Type = D3DKMT_QUERYSTATISTICS_ADAPTER;
    adapter.AdapterLuid = luid;
    if (D3DKMTQueryStatistics(&adapter) == 0) {
        segment_count_ = static_cast<int>(std::min<ULONG>(adapter.QueryResult.AdapterInformation.NbSegments, kMaxSegments));
    }
    for (int s = 0; s < segment_count_; ++s) {
        D3DKMT_QUERYSTATISTICS segment{};
        segment.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
        segment.AdapterLuid = luid;
        segment.QuerySegment.SegmentId = static_cast<ULONG>(s);
        dedicated_segment_[static_cast<size_t>(s)] =
            D3DKMTQueryStatistics(&segment) == 0 && segment.QueryResult.SegmentInformation.Aperture == 0;
    }

    PDH_HQUERY query = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
        return false;
    }
    query_.reset(query);
    // 只要 3D 引擎（和任务管理器“GPU”列的默认口径一致）
    const PDH_STATUS status =
        PdhAddEnglishCounterW(query, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &engine_);
    if (status != ERROR_SUCCESS) {
        log::Write(L"gpu processes: add counter failed 0x%08lX", static_cast<unsigned long>(status));
        Stop();
        return false;
    }
    primed_ = false;
    return true;
}

void GpuProcessSampler::Stop() {
    query_.reset();
    engine_ = nullptr;
    std::vector<BYTE>().swap(buffer_);
    primed_ = false;
}

PDH_STATUS GpuProcessSampler::Items(PDH_HCOUNTER counter, DWORD& count, PDH_FMT_COUNTERVALUE_ITEM_W*& items) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        DWORD size = static_cast<DWORD>(buffer_.size());
        count = 0;
        items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer_.data());
        const PDH_STATUS status =
            PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items);
        if (status != static_cast<PDH_STATUS>(PDH_MORE_DATA)) {
            return status;
        }
        // 第一次和进程变多时才会走到这里；多留一半余量，免得每轮都重新分配
        buffer_.resize(size + size / 2);
    }
    return static_cast<PDH_STATUS>(PDH_MORE_DATA);
}

uint64_t GpuProcessSampler::DedicatedBytes(uint32_t pid) const {
    // 受保护的进程打不开：显存读不到，不是 0
    const unique_handle process(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid));
    if (!process) {
        return kUnknownBytes;
    }
    uint64_t bytes = 0;
    for (int s = 0; s < segment_count_; ++s) {
        if (!dedicated_segment_[static_cast<size_t>(s)]) {
            continue;
        }
        D3DKMT_QUERYSTATISTICS stats{};
        stats.Type = D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT;
        stats.AdapterLuid = luid_;
        stats.hProcess = process.get();
        stats.QueryProcessSegment.SegmentId = static_cast<ULONG>(s);
        if (D3DKMTQueryStatistics(&stats) == 0) {
            bytes += stats.QueryResult.ProcessSegmentInformation.BytesCommitted;
        }
    }
    return bytes;
}

bool GpuProcessSampler::Sample(std::vector<GpuProcessUsage>& out) {
    out.clear();
    if (out.capacity() < kReserveEntries) {
        out.reserve(kReserveEntries);
    }
    if (!query_) {
        return false;
    }
    const PDH_STATUS collected = PdhCollectQueryData(query_.get());
    if (NoInstances(collected)) {
        primed_ = true;
        return true;
    }
    if (collected != ERROR_SUCCESS) {
        return false;
    }
    if (!primed_) {
        primed_ = true;  // 利用率要两次采集才有
        return false;
    }
    DWORD count = 0;
    PDH_FMT_COUNTERVALUE_ITEM_W* items = nullptr;
    const PDH_STATUS status = Items(engine_, count, items);
    if (NoInstances(status)) {
        return true;
    }
    if (status != ERROR_SUCCESS) {
        return false;
    }
    size_t on_adapter = 0;
    for (DWORD i = 0; i < count; ++i) {
        const auto g = sensors::ParseGpuInstance(items[i].szName);
        if (!g || g->luid_high != static_cast<uint32_t>(luid_.HighPart) || g->luid_low != luid_.LowPart) {
            continue;
        }
        ++on_adapter;
        const double percent = Valid(items[i].FmtValue.CStatus) ? std::max(0.0, items[i].FmtValue.doubleValue) : 0.0;
        out.push_back({g->pid, percent, 0, kHiddenGroup});
    }
    // 系统里有 3D 引擎实例，却没有一个在这块显卡上：可能是 LUID 对不上，也可能只是这块卡这会儿没有 3D 负载。记一行备查
    if (count > 0 && on_adapter == 0 && !reported_) {
        reported_ = true;
        log::Write(L"gpu processes: %lu engine instance(s) in the system, none on the selected adapter (luid 0x%08lX_0x%08lX)",
                   count, static_cast<unsigned long>(luid_.HighPart), static_cast<unsigned long>(luid_.LowPart));
    }
    // 每个进程可能有多个 3D 引擎实例：按 pid 排序后合并成一项，再逐个查显存
    std::sort(out.begin(), out.end(), [](const GpuProcessUsage& a, const GpuProcessUsage& b) { return a.pid < b.pid; });
    size_t w = 0;
    for (size_t r = 0; r < out.size(); ++r) {
        if (w > 0 && out[w - 1].pid == out[r].pid) {
            out[w - 1].percent += out[r].percent;
        } else {
            out[w++] = out[r];
        }
    }
    out.resize(w);
    for (GpuProcessUsage& u : out) {
        u.dedicated_bytes = DedicatedBytes(u.pid);
    }
    return true;
}

}  // namespace pitot::sampler
