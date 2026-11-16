#pragma once

#include <windows.h>

#include <pdh.h>

#include <array>
#include <vector>

#include "common/unique_handle.h"
#include "sampler/process_stats.h"

namespace pitot::sampler {

// 每个进程在显卡上的用量（D-033）：
// - 利用率：PDH \GPU Engine(*engtype_3D)\Utilization Percentage，按 pid 汇总；
// - 专用显存：D3DKMTQueryStatistics 的进程显存段统计（非 aperture 段的 BytesCommitted 之和），
//   和 System Informer 同法。没用 PDH \GPU Process Memory：本机实测它给 dwm 报 8.9 GB（显卡只有 8 GB），
//   D3DKMT 同时读到 844 MB，其他进程两者一致。
// GPU Engine 的实例很多（每个进程 × 每个引擎），采集开销明显，只在 GPU 面板打开时开查询，
// 离开面板就关掉并释放缓冲区。
class GpuProcessSampler {
public:
    bool Start(LUID luid);
    void Stop();
    [[nodiscard]] bool active() const noexcept { return static_cast<bool>(query_); }

    // out 按 pid 排好、每个 pid 一项，app 留给调用方填。
    // 返回 false 表示还没有结果（第一次采集只建立基准）；系统里没有任何 3D 引擎实例时返回 true、out 为空
    bool Sample(std::vector<GpuProcessUsage>& out);

private:
    struct QueryTraits {
        using pointer = PDH_HQUERY;
        static pointer invalid() noexcept { return nullptr; }
        static void close(pointer q) noexcept { PdhCloseQuery(q); }
    };
    static constexpr int kMaxSegments = 16;

    PDH_STATUS Items(PDH_HCOUNTER counter, DWORD& count, PDH_FMT_COUNTERVALUE_ITEM_W*& items);
    uint64_t DedicatedBytes(uint32_t pid) const;

    unique_handle_t<QueryTraits> query_;
    PDH_HCOUNTER engine_ = nullptr;
    std::vector<BYTE> buffer_;
    bool primed_ = false;
    bool reported_ = false;  // 每次打开面板只记一次“引擎实例都不在这块显卡上”
    LUID luid_{};
    std::array<bool, kMaxSegments> dedicated_segment_{};  // 非 aperture 段（显卡自己的显存）
    int segment_count_ = 0;
};

}  // namespace pitot::sampler
