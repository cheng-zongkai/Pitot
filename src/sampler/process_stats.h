#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "etw/io_table.h"
#include "sampler/snapshot.h"

namespace pitot::sampler {

// 不进列表的进程（空闲进程、Memory Compression）用这个分组号
inline constexpr uint32_t kHiddenGroup = 0xFFFFFFFF;

// 进程快照里的一个进程，由调用方从系统接口填好
struct ProcessRecord {
    uint64_t pid = 0;
    int64_t create_time = 0;     // 和 pid 一起识别同一个进程，pid 被复用时能分辨出来
    int64_t cpu_time = 0;        // 内核 + 用户时间，100 ns
    uint64_t private_bytes = 0;  // 专用工作集
    uint32_t group = 0;          // 所属程序，调用方按可执行文件路径分配；kHiddenGroup 表示不显示
};

// 从相邻两轮进程快照算 CPU / 内存两个进程列表（D-027、D-030）：
// - 同一个 group 的进程合并成一行，并记下其中数值最大的几个进程（展开用）。
// - CPU 按两轮之间的 CPU 时间差值算；本轮新出现的进程（包括 pid 被复用的）整段 CPU 时间都算在本轮。
// - 有系统总利用率时，各进程按 CPU 时间占比分摊总利用率，和任务管理器一样各行之和等于总数
//   （“% Processor Utility”考虑了频率，单纯的时间占比和总数对不上）。
// 稳态不分配堆内存：容器用 Reserve 预留，进程数超过预留量时才会增长。
// 输出的 ProcessEntry 只填 app_id、value、count、children；名称和图标由调用方按 app_id 补。
class ProcessStats {
public:
    void Reserve(size_t processes);
    // 丢掉上一轮的数据（浮窗关闭时调用），并释放内存
    void Reset();

    // records 会被重新排序。
    // cpu_total：本轮系统总利用率 0–100；为空时退回按时间占比算，此时需要 capacity_100ns
    // （两轮间隔 × 逻辑处理器数，100 ns 单位）。
    void Update(std::span<ProcessRecord> records, std::optional<double> cpu_total, double capacity_100ns, int top_n,
                Snapshot& out);

private:
    struct Prev {
        uint64_t pid;
        int64_t create_time;
        int64_t cpu_time;
    };
    struct Group {
        uint32_t id;
        int64_t cpu_delta;
        uint64_t bytes;
        int count;
    };

    double ToPercent(int64_t delta) const;
    void FillList(std::span<const ProcessRecord> records, bool cpu, int n, std::array<ProcessEntry, kMaxTopProcesses>& list);

    std::vector<Prev> prev_;
    std::vector<Prev> next_;
    std::vector<int64_t> deltas_;
    std::vector<uint32_t> order_;
    std::vector<Group> groups_;
    bool primed_ = false;
    // 本轮的换算参数，ToPercent 用
    std::optional<double> cpu_total_;
    double capacity_ = 0;
    int64_t busy_ = 0;
};

// 一个进程在显卡上的用量（PDH GPU 计数器），app 由调用方按进程快照填好，kHiddenGroup 表示不显示
inline constexpr uint64_t kUnknownBytes = ~0ull;  // 读不到（受保护的进程打不开句柄），区别于 0
struct GpuProcessUsage {
    uint32_t pid = 0;
    double percent = 0;            // 3D 引擎利用率
    uint64_t dedicated_bytes = 0;  // 专用显存；kUnknownBytes 表示读不到
    uint32_t app = kHiddenGroup;
};

// GPU 进程列表：和 CPU / 内存列表一样按程序合并（D-030），按利用率、再按显存从大到小排。
// 稳态不分配堆内存（容器用 Reserve 预留）。
class GpuProcessGroups {
public:
    void Reserve(size_t processes);
    void Reset();
    // 返回行数。输出只填 app_id、value（%）、value2（字节；一个都读不到时为 −1）、count、children。
    // 利用率为 0 且显存为 0 或读不到的进程不算（只是开过显卡设备，没有可显示的用量）
    int Build(std::span<const GpuProcessUsage> usage, int top_n, std::array<ProcessEntry, kMaxTopProcesses>& out);

private:
    struct Group {
        uint32_t app;
        double percent;
        uint64_t bytes;
        int count;
        bool bytes_known;
    };
    std::vector<uint32_t> order_;
    std::vector<Group> groups_;
};

// 一个进程的磁盘或网络速率（ETW，D-037），字节/秒。
// 磁盘：first 读取、second 写入；网络：first 下载（接收）、second 上传（发送）
struct IoProcessUsage {
    uint32_t pid = 0;
    double first = 0;
    double second = 0;
    uint32_t app = kHiddenGroup;
};

// 把这一轮取出的字节数换算成速率。采样前会刷新 ETW 缓冲区，这一轮就是卡片的那一段时间（D-060）。
// 速率不和上一轮平均：平均会让列表比卡片慢一拍。
// 但行会保留：最近 kHoldSeconds 内有过读写的进程，这一轮没有新数据时仍输出一行，速率为 0（D-061）。
// 流量稀疏时（几百 B/s）大多数秒里一个载荷事件都没有，不保留的话列表会一直闪空。
class IoWindow {
public:
    static constexpr double kHoldSeconds = 5.0;

    void Reserve(size_t processes);
    void Reset();
    // current 按 pid 排好，seconds 是这一轮的时长。输出按 pid 排好。
    // 稳态不分配堆内存（Reserve 预留 5 秒内的进程数；极端情况超出时 vector 自己扩容）
    void Update(std::span<const etw::IoSample> current, double seconds, std::vector<IoProcessUsage>& disk,
                std::vector<IoProcessUsage>& net);

private:
    struct Held {
        uint32_t pid;
        double last_seen;  // clock_ 的值
    };
    struct Kind {
        std::vector<Held> held;
        std::vector<Held> scratch;
    };
    // first / second 取这一类的两个字段；写出 usage 并更新 kind.held
    template <typename Fields>
    void Merge(Kind& kind, std::span<const etw::IoSample> current, double seconds, Fields fields,
               std::vector<IoProcessUsage>& out);

    Kind disk_;
    Kind net_;
    double clock_ = 0;  // 累计秒数，只用来判断保留是否过期
};

// 磁盘 / 网络进程列表：按程序合并（D-030），按两列之和从大到小排。稳态不分配堆内存
class IoProcessGroups {
public:
    void Reserve(size_t processes);
    void Reset();
    // 返回行数。输出只填 app_id、value（first）、value2（second）、count、children
    int Build(std::span<const IoProcessUsage> usage, int top_n, std::array<ProcessEntry, kMaxTopProcesses>& out);

private:
    struct Group {
        uint32_t app;
        double first;
        double second;
        int count;
    };
    std::vector<uint32_t> order_;
    std::vector<Group> groups_;
};

}  // namespace pitot::sampler
