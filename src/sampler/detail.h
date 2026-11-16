#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sampler/icon_store.h"
#include "sampler/process_stats.h"
#include "sampler/snapshot.h"

namespace pitot::sampler {

// 浮窗打开时才采的详细指标（D-027）：进程快照（进程列表、已压缩）、内存页面列表、已提交。
// 浮窗关闭后调用 Reset 释放进程快照的缓冲区（几百 KB）和程序表。
class DetailSampler {
public:
    explicit DetailSampler(IconStore& icons) : icons_(icons) {}
    ~DetailSampler() { Reset(); }
    DetailSampler(const DetailSampler&) = delete;
    DetailSampler& operator=(const DetailSampler&) = delete;

    // 打开读内存页面列表需要的特权（管理员令牌里有，但默认是禁用的）
    void Init();
    // english 为真时进程友好名称取文件自带的中性（英文）版本，否则取系统显示语言的本地化版本
    void Sample(ULONGLONG now, std::optional<double> cpu_total, int top_n, bool english, Snapshot& s);
    // GPU 进程列表：usage 来自 GpuProcessSampler（按 pid 排好），在 Sample 之后调用，借用同一轮的进程 → 程序映射。
    // valid 为假表示利用率还没有（第一轮）
    void SampleGpu(std::vector<GpuProcessUsage>& usage, bool valid, int top_n, Snapshot& s);
    // 磁盘 / 网络进程列表（ETW，D-037），同样在 Sample 之后调用。两组 usage 都按 pid 排好
    void SampleIo(std::vector<IoProcessUsage>& disk, std::vector<IoProcessUsage>& net, int top_n, Snapshot& s);
    void Reset();

private:
    // 一个程序：同一个可执行文件路径的进程合并成一行（D-030）
    struct App {
        std::wstring path;     // Win32 路径；拿不到时为空
        wchar_t image[40]{};   // 映像名
        wchar_t name[64]{};    // 友好名称，Resolve 之后才有
        bool resolved = false;
        int icon = -1;
        uint32_t icon_version = 0;
    };
    // 进程 → 程序，按 pid 排序，跨轮复用，只有新进程才去查路径
    struct ProcessApp {
        uint64_t pid;
        int64_t create_time;
        uint32_t app;
    };

    bool QueryProcesses();
    // 按 pid 归并找程序编号：pids 递增调用，cursor 从 0 开始。进程快照里没有的（刚退出）返回 kHiddenGroup
    uint32_t AppOfPid(uint32_t pid, size_t& cursor) const;
    void SampleProcesses(ULONGLONG now, std::optional<double> cpu_total, int top_n, Snapshot& s);
    void SampleMemoryLists(Snapshot& s);
    void FreeBuffer();
    uint32_t AppFor(uint64_t pid, const wchar_t* image, size_t image_length);
    void Resolve(App& app);
    void FillNames(std::array<ProcessEntry, kMaxTopProcesses>& list, int count);

    void SampleSwap(Snapshot& s);
    IconStore& icons_;
    void* buffer_ = nullptr;
    ULONG buffer_size_ = 0;
    std::vector<ProcessRecord> records_;
    ProcessStats stats_;
    GpuProcessGroups gpu_groups_;
    IoProcessGroups io_groups_;
    std::vector<App> apps_;
    std::unordered_map<std::wstring, uint32_t> app_index_;  // 小写路径（或 "name:" + 小写映像名）→ 程序编号
    std::vector<ProcessApp> process_apps_;
    std::vector<ProcessApp> next_process_apps_;
    ULONGLONG last_tick_ = 0;
    DWORD cpu_count_ = 1;
    bool english_names_ = false;
    bool lists_denied_ = false;  // 没有权限就不再每轮重试
    uint64_t page_size_ = 4096;
    uint64_t physical_total_ = 0;
};

}  // namespace pitot::sampler
