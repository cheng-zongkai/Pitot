#include "sampler/process_stats.h"

#include <algorithm>
#include <utility>

namespace pitot::sampler {
namespace {

// 有序插入展开用的子行，只保留排在最前的 kMaxChildren 个。before(a, b) 为真表示 a 排在 b 前面
template <typename Before>
void InsertChild(ProcessEntry& e, const ProcessChild& child, Before before) {
    int pos = e.child_count;
    while (pos > 0 && !before(e.children[static_cast<size_t>(pos - 1)], child)) {
        --pos;
    }
    if (pos >= kMaxChildren) {
        return;
    }
    const int last = std::min(e.child_count, kMaxChildren - 1);
    for (int m = last; m > pos; --m) {
        e.children[static_cast<size_t>(m)] = e.children[static_cast<size_t>(m - 1)];
    }
    e.children[static_cast<size_t>(pos)] = child;
    e.child_count = std::min(e.child_count + 1, kMaxChildren);
}

}  // namespace

void ProcessStats::Reserve(size_t processes) {
    prev_.reserve(processes);
    next_.reserve(processes);
    deltas_.reserve(processes);
    order_.reserve(processes);
    groups_.reserve(processes);
}

void ProcessStats::Reset() {
    primed_ = false;
    // 用交换释放容量，clear 不会还内存
    std::vector<Prev>().swap(prev_);
    std::vector<Prev>().swap(next_);
    std::vector<int64_t>().swap(deltas_);
    std::vector<uint32_t>().swap(order_);
    std::vector<Group>().swap(groups_);
}

double ProcessStats::ToPercent(int64_t delta) const {
    if (cpu_total_ && busy_ > 0) {
        return static_cast<double>(delta) / static_cast<double>(busy_) * *cpu_total_;
    }
    if (capacity_ > 0) {
        return std::min(100.0, static_cast<double>(delta) / capacity_ * 100.0);
    }
    return 0.0;
}

void ProcessStats::Update(std::span<ProcessRecord> records, std::optional<double> cpu_total, double capacity_100ns,
                          int top_n, Snapshot& out) {
    top_n = std::clamp(top_n, 0, kMaxTopProcesses);
    std::sort(records.begin(), records.end(),
              [](const ProcessRecord& a, const ProcessRecord& b) { return a.pid < b.pid; });

    // 1. 和上一轮按 pid 归并，算每个进程本轮用掉的 CPU 时间
    deltas_.assign(records.size(), 0);
    busy_ = 0;
    if (primed_) {
        size_t j = 0;
        for (size_t i = 0; i < records.size(); ++i) {
            const ProcessRecord& r = records[i];
            while (j < prev_.size() && prev_[j].pid < r.pid) {
                ++j;
            }
            int64_t delta = r.cpu_time;
            if (j < prev_.size() && prev_[j].pid == r.pid && prev_[j].create_time == r.create_time) {
                delta = std::max<int64_t>(0, r.cpu_time - prev_[j].cpu_time);
            }
            deltas_[i] = delta;
            if (r.pid != 0) {
                busy_ += delta;
            }
        }
    }
    next_.clear();
    for (const ProcessRecord& r : records) {
        next_.push_back({r.pid, r.create_time, r.cpu_time});
    }
    prev_.swap(next_);
    const bool have_cpu = primed_;
    primed_ = true;
    cpu_total_ = cpu_total;
    capacity_ = capacity_100ns;

    // 2. 按分组排序后合并
    order_.clear();
    for (uint32_t i = 0; i < records.size(); ++i) {
        if (records[i].pid != 0 && records[i].group != kHiddenGroup) {
            order_.push_back(i);
        }
    }
    std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) {
        return records[a].group != records[b].group ? records[a].group < records[b].group
                                                    : records[a].pid < records[b].pid;
    });
    groups_.clear();
    for (uint32_t i : order_) {
        const ProcessRecord& r = records[i];
        if (groups_.empty() || groups_.back().id != r.group) {
            groups_.push_back({r.group, 0, 0, 0});
        }
        Group& g = groups_.back();
        g.cpu_delta += deltas_[i];
        g.bytes += r.private_bytes;
        ++g.count;
    }

    // 3. 各取前 top_n，再给每行挑出数值最大的几个进程
    const auto n = static_cast<ptrdiff_t>(std::min<size_t>(static_cast<size_t>(top_n), groups_.size()));
    std::partial_sort(groups_.begin(), groups_.begin() + n, groups_.end(), [](const Group& a, const Group& b) {
        return a.bytes != b.bytes ? a.bytes > b.bytes : a.id < b.id;
    });
    out.top_memory_count = static_cast<int>(n);
    for (ptrdiff_t i = 0; i < n; ++i) {
        const Group& g = groups_[static_cast<size_t>(i)];
        ProcessEntry& e = out.top_memory[static_cast<size_t>(i)];
        e.app_id = g.id;
        e.value = static_cast<double>(g.bytes);
        e.count = g.count;
    }
    FillList(records, false, static_cast<int>(n), out.top_memory);

    out.top_cpu_valid = have_cpu;
    out.top_cpu_count = 0;
    if (have_cpu) {
        std::partial_sort(groups_.begin(), groups_.begin() + n, groups_.end(), [](const Group& a, const Group& b) {
            return a.cpu_delta != b.cpu_delta ? a.cpu_delta > b.cpu_delta : a.bytes > b.bytes;
        });
        out.top_cpu_count = static_cast<int>(n);
        for (ptrdiff_t i = 0; i < n; ++i) {
            const Group& g = groups_[static_cast<size_t>(i)];
            ProcessEntry& e = out.top_cpu[static_cast<size_t>(i)];
            e.app_id = g.id;
            e.value = ToPercent(g.cpu_delta);
            e.count = g.count;
        }
        FillList(records, true, static_cast<int>(n), out.top_cpu);
    }
    out.top_valid = true;
}

void ProcessStats::FillList(std::span<const ProcessRecord> records, bool cpu, int n,
                            std::array<ProcessEntry, kMaxTopProcesses>& list) {
    for (int k = 0; k < n; ++k) {
        list[static_cast<size_t>(k)].child_count = 0;
    }
    for (size_t i = 0; i < records.size(); ++i) {
        const ProcessRecord& r = records[i];
        if (r.pid == 0 || r.group == kHiddenGroup) {
            continue;
        }
        for (int k = 0; k < n; ++k) {
            ProcessEntry& e = list[static_cast<size_t>(k)];
            if (e.app_id != r.group) {
                continue;
            }
            const double value = cpu ? ToPercent(deltas_[i]) : static_cast<double>(r.private_bytes);
            InsertChild(e, {static_cast<uint32_t>(r.pid), value},
                        [](const ProcessChild& a, const ProcessChild& b) { return a.value >= b.value; });
            break;
        }
    }
}

void GpuProcessGroups::Reserve(size_t processes) {
    order_.reserve(processes);
    groups_.reserve(processes);
}

void GpuProcessGroups::Reset() {
    std::vector<uint32_t>().swap(order_);
    std::vector<Group>().swap(groups_);
}

int GpuProcessGroups::Build(std::span<const GpuProcessUsage> usage, int top_n,
                            std::array<ProcessEntry, kMaxTopProcesses>& out) {
    top_n = std::clamp(top_n, 0, kMaxTopProcesses);
    // 没有可显示用量的进程（利用率 0，显存 0 或读不到）不进列表
    auto shown = [](const GpuProcessUsage& u) {
        return u.app != kHiddenGroup && (u.percent > 0 || (u.dedicated_bytes != kUnknownBytes && u.dedicated_bytes > 0));
    };
    order_.clear();
    for (uint32_t i = 0; i < usage.size(); ++i) {
        if (shown(usage[i])) {
            order_.push_back(i);
        }
    }
    std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) {
        return usage[a].app != usage[b].app ? usage[a].app < usage[b].app : usage[a].pid < usage[b].pid;
    });
    groups_.clear();
    for (uint32_t i : order_) {
        const GpuProcessUsage& u = usage[i];
        if (groups_.empty() || groups_.back().app != u.app) {
            groups_.push_back({u.app, 0, 0, 0, false});
        }
        Group& g = groups_.back();
        g.percent += u.percent;
        if (u.dedicated_bytes != kUnknownBytes) {
            g.bytes += u.dedicated_bytes;
            g.bytes_known = true;
        }
        ++g.count;
    }
    const auto n = static_cast<ptrdiff_t>(std::min<size_t>(static_cast<size_t>(top_n), groups_.size()));
    std::partial_sort(groups_.begin(), groups_.begin() + n, groups_.end(), [](const Group& a, const Group& b) {
        if (a.percent != b.percent) {
            return a.percent > b.percent;
        }
        return a.bytes != b.bytes ? a.bytes > b.bytes : a.app < b.app;
    });
    for (ptrdiff_t k = 0; k < n; ++k) {
        const Group& g = groups_[static_cast<size_t>(k)];
        ProcessEntry& e = out[static_cast<size_t>(k)];
        e.app_id = g.app;
        e.value = std::min(100.0, g.percent);
        e.value2 = g.bytes_known ? static_cast<double>(g.bytes) : -1.0;
        e.count = g.count;
        e.child_count = 0;
    }
    // 每行挑出最忙的几个进程（展开用），利用率相同时按显存
    auto before = [](const ProcessChild& a, const ProcessChild& b) {
        return a.value != b.value ? a.value > b.value : a.value2 >= b.value2;
    };
    for (const GpuProcessUsage& u : usage) {
        if (!shown(u)) {
            continue;
        }
        for (ptrdiff_t k = 0; k < n; ++k) {
            ProcessEntry& e = out[static_cast<size_t>(k)];
            if (e.app_id != u.app) {
                continue;
            }
            const double bytes = u.dedicated_bytes == kUnknownBytes ? -1.0 : static_cast<double>(u.dedicated_bytes);
            InsertChild(e, {u.pid, u.percent, bytes}, before);
            break;
        }
    }
    return static_cast<int>(n);
}

void IoWindow::Reserve(size_t processes) {
    for (Kind* kind : {&disk_, &net_}) {
        kind->held.reserve(processes);
        kind->scratch.reserve(processes);
    }
}

void IoWindow::Reset() {
    // 用交换释放容量，clear 不会还内存
    for (Kind* kind : {&disk_, &net_}) {
        std::vector<Held>().swap(kind->held);
        std::vector<Held>().swap(kind->scratch);
    }
    clock_ = 0;
}

template <typename Fields>
void IoWindow::Merge(Kind& kind, std::span<const etw::IoSample> current, double seconds, Fields fields,
                     std::vector<IoProcessUsage>& out) {
    kind.scratch.clear();
    const std::vector<Held>& held = kind.held;
    size_t j = 0;
    // 这一轮没有新数据的老进程：没过期就留一行，速率 0
    auto keep_old = [&](const Held& h) {
        if (clock_ - h.last_seen <= kHoldSeconds) {
            out.push_back({h.pid, 0, 0});
            kind.scratch.push_back(h);
        }
    };
    // held 和 current 都按 pid 排好，归并一遍，输出也按 pid 排好
    for (const etw::IoSample& sample : current) {
        const auto [a, b] = fields(sample.bytes);
        if (a + b == 0) {
            continue;
        }
        while (j < held.size() && held[j].pid < sample.pid) {
            keep_old(held[j++]);
        }
        if (j < held.size() && held[j].pid == sample.pid) {
            ++j;
        }
        out.push_back({sample.pid, static_cast<double>(a) / seconds, static_cast<double>(b) / seconds});
        kind.scratch.push_back({sample.pid, clock_});
    }
    while (j < held.size()) {
        keep_old(held[j++]);
    }
    kind.held.swap(kind.scratch);
}

void IoWindow::Update(std::span<const etw::IoSample> current, double seconds, std::vector<IoProcessUsage>& disk,
                      std::vector<IoProcessUsage>& net) {
    disk.clear();
    net.clear();
    if (seconds <= 0) {
        return;
    }
    clock_ += seconds;
    Merge(disk_, current, seconds,
          [](const etw::IoCounters& c) { return std::pair<uint64_t, uint64_t>{c.disk_read, c.disk_write}; }, disk);
    Merge(net_, current, seconds,
          [](const etw::IoCounters& c) { return std::pair<uint64_t, uint64_t>{c.net_receive, c.net_send}; }, net);
}

void IoProcessGroups::Reserve(size_t processes) {
    order_.reserve(processes);
    groups_.reserve(processes);
}

void IoProcessGroups::Reset() {
    std::vector<uint32_t>().swap(order_);
    std::vector<Group>().swap(groups_);
}

int IoProcessGroups::Build(std::span<const IoProcessUsage> usage, int top_n,
                           std::array<ProcessEntry, kMaxTopProcesses>& out) {
    top_n = std::clamp(top_n, 0, kMaxTopProcesses);
    order_.clear();
    for (uint32_t i = 0; i < usage.size(); ++i) {
        if (usage[i].app != kHiddenGroup) {
            order_.push_back(i);
        }
    }
    std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) {
        return usage[a].app != usage[b].app ? usage[a].app < usage[b].app : usage[a].pid < usage[b].pid;
    });
    groups_.clear();
    for (uint32_t i : order_) {
        const IoProcessUsage& u = usage[i];
        if (groups_.empty() || groups_.back().app != u.app) {
            groups_.push_back({u.app, 0, 0, 0});
        }
        Group& g = groups_.back();
        g.first += u.first;
        g.second += u.second;
        ++g.count;
    }
    const auto n = static_cast<ptrdiff_t>(std::min<size_t>(static_cast<size_t>(top_n), groups_.size()));
    std::partial_sort(groups_.begin(), groups_.begin() + n, groups_.end(), [](const Group& a, const Group& b) {
        const double ta = a.first + a.second;
        const double tb = b.first + b.second;
        return ta != tb ? ta > tb : a.app < b.app;
    });
    for (ptrdiff_t k = 0; k < n; ++k) {
        const Group& g = groups_[static_cast<size_t>(k)];
        ProcessEntry& e = out[static_cast<size_t>(k)];
        e.app_id = g.app;
        e.value = g.first;
        e.value2 = g.second;
        e.count = g.count;
        e.child_count = 0;
    }
    auto before = [](const ProcessChild& a, const ProcessChild& b) { return a.value + a.value2 >= b.value + b.value2; };
    for (const IoProcessUsage& u : usage) {
        if (u.app == kHiddenGroup) {
            continue;
        }
        for (ptrdiff_t k = 0; k < n; ++k) {
            ProcessEntry& e = out[static_cast<size_t>(k)];
            if (e.app_id == u.app) {
                InsertChild(e, {u.pid, u.first, u.second}, before);
                break;
            }
        }
    }
    return static_cast<int>(n);
}

}  // namespace pitot::sampler
