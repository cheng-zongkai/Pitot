// 进程列表统计、处理器实例名解析

#include <cmath>
#include <vector>

#include "sampler/cpu_info.h"
#include "sampler/process_stats.h"
#include "test.h"

TEST_CASE("gpu processes: grouped by app, sorted by usage then VRAM") {
    using pitot::sampler::GpuProcessGroups;
    using pitot::sampler::GpuProcessUsage;
    using pitot::sampler::kHiddenGroup;
    std::array<pitot::sampler::ProcessEntry, pitot::sampler::kMaxTopProcesses> out{};
    GpuProcessGroups groups;
    groups.Reserve(16);
    const GpuProcessUsage usage[] = {
        {10, 5.0, 100, 1},             // 程序 1 的两个进程
        {11, 20.0, 300, 1},
        {20, 0.0, 900, 2},             // 程序 2：不占 GPU，但显存最多
        {30, 0.0, 50, 3},              // 程序 3：都比 2 小
        {40, 99.0, 999, kHiddenGroup},  // 不显示的进程
    };
    const int n = groups.Build(usage, 5, out);
    CHECK(n == 3);
    CHECK(out[0].app_id == 1u && out[0].value == 25.0 && out[0].value2 == 400.0 && out[0].count == 2);
    CHECK(out[0].child_count == 2 && out[0].children[0].pid == 11u && out[0].children[1].pid == 10u);
    CHECK(out[1].app_id == 2u && out[1].value2 == 900.0);  // 利用率相同时显存大的在前
    CHECK(out[2].app_id == 3u);
    // 行数上限
    CHECK(groups.Build(usage, 1, out) == 1 && out[0].app_id == 1u);
    CHECK(groups.Build({}, 5, out) == 0);
}

TEST_CASE("gpu processes: idle rows dropped, unreadable VRAM is not zero") {
    using pitot::sampler::GpuProcessGroups;
    using pitot::sampler::GpuProcessUsage;
    using pitot::sampler::kUnknownBytes;
    std::array<pitot::sampler::ProcessEntry, pitot::sampler::kMaxTopProcesses> out{};
    GpuProcessGroups groups;
    const GpuProcessUsage usage[] = {
        {1, 0.0, kUnknownBytes, 1},  // 受保护、空闲：不显示
        {2, 0.0, 0, 2},              // 开过设备但没用：不显示
        {3, 2.0, kUnknownBytes, 3},  // 在用 GPU 但显存读不到：显示，显存为 −1（界面显示“—”）
    };
    const int n = groups.Build(usage, 5, out);
    CHECK(n == 1);
    CHECK(out[0].app_id == 3u && out[0].value == 2.0 && out[0].value2 < 0);
    CHECK(out[0].child_count == 1 && out[0].children[0].value2 < 0);
}

namespace {

using pitot::sampler::kHiddenGroup;
using pitot::sampler::ProcessRecord;
using pitot::sampler::ProcessStats;
using pitot::sampler::Snapshot;

ProcessRecord Proc(uint64_t pid, uint32_t group, int64_t cpu, uint64_t bytes, int64_t create = 1) {
    ProcessRecord r;
    r.pid = pid;
    r.create_time = create;
    r.cpu_time = cpu;
    r.private_bytes = bytes;
    r.group = group;
    return r;
}

bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }

}  // namespace

TEST_CASE("processor instance: group,number") {
    using pitot::sampler::ParseProcessorInstance;
    CHECK(ParseProcessorInstance(L"0,0") == 0);
    CHECK(ParseProcessorInstance(L"0,11") == 11);
    CHECK(ParseProcessorInstance(L"1,3") == 67);
    CHECK(ParseProcessorInstance(L"_Total") == -1);
    CHECK(ParseProcessorInstance(L"0,_Total") == -1);
    CHECK(ParseProcessorInstance(L"0,") == -1);
    CHECK(ParseProcessorInstance(L"0,1x") == -1);
    CHECK(ParseProcessorInstance(L"") == -1);
    CHECK(ParseProcessorInstance(nullptr) == -1);
}

TEST_CASE("process stats: first round has memory but no cpu") {
    ProcessStats stats;
    std::vector<ProcessRecord> r = {Proc(0, kHiddenGroup, 1000, 0), Proc(4, 1, 50, 100), Proc(100, 2, 10, 300)};
    Snapshot s;
    stats.Update(r, 10.0, 0, 5, s);
    CHECK(s.top_valid);
    CHECK(!s.top_cpu_valid);
    CHECK(s.top_cpu_count == 0);
    CHECK(s.top_memory_count == 2);  // 空闲进程不算
    CHECK(s.top_memory[0].app_id == 2);
    CHECK(s.top_memory[0].value == 300);
}

TEST_CASE("process stats: same program merges and lists its processes") {
    ProcessStats stats;
    std::vector<ProcessRecord> r = {Proc(8, 7, 0, 100), Proc(12, 7, 0, 200), Proc(16, 3, 0, 250)};
    Snapshot s;
    stats.Update(r, std::nullopt, 0, 5, s);
    CHECK(s.top_memory_count == 2);
    const auto& top = s.top_memory[0];
    CHECK(top.app_id == 7);
    CHECK(top.value == 300);
    CHECK(top.count == 2);
    CHECK(top.child_count == 2);
    CHECK(top.children[0].pid == 12);  // 大的在前
    CHECK(top.children[0].value == 200);
    CHECK(top.children[1].pid == 8);
    CHECK(s.top_memory[1].child_count == 1);
}

TEST_CASE("process stats: children are capped and sorted") {
    ProcessStats stats;
    std::vector<ProcessRecord> r;
    for (uint64_t i = 0; i < 12; ++i) {
        r.push_back(Proc(100 + i * 4, 5, 0, (i + 1) * 10));
    }
    Snapshot s;
    stats.Update(r, std::nullopt, 0, 5, s);
    const auto& e = s.top_memory[0];
    CHECK(e.count == 12);
    CHECK(e.child_count == pitot::sampler::kMaxChildren);
    CHECK(e.children[0].value == 120);
    CHECK(e.children[7].value == 50);
}

TEST_CASE("process stats: cpu share scaled to total utility") {
    ProcessStats stats;
    std::vector<ProcessRecord> r1 = {Proc(0, kHiddenGroup, 0, 0), Proc(8, 1, 100, 1), Proc(12, 2, 100, 1)};
    Snapshot s;
    stats.Update(r1, 0.0, 0, 5, s);
    // 第二轮：pid 8 用了 300，pid 12 用了 100，空闲进程用了 10000（不计入）
    std::vector<ProcessRecord> r2 = {Proc(12, 2, 200, 1), Proc(0, kHiddenGroup, 10000, 0), Proc(8, 1, 400, 1)};
    stats.Update(r2, 20.0, 0, 5, s);
    CHECK(s.top_cpu_valid);
    CHECK(s.top_cpu_count == 2);
    CHECK(s.top_cpu[0].app_id == 1);
    CHECK(Near(s.top_cpu[0].value, 15.0));
    CHECK(Near(s.top_cpu[0].children[0].value, 15.0));
    CHECK(Near(s.top_cpu[1].value, 5.0));
}

TEST_CASE("process stats: exited, new and reused pids") {
    ProcessStats stats;
    std::vector<ProcessRecord> r1 = {Proc(8, 1, 1000, 1, 1), Proc(12, 2, 500, 1, 1)};
    Snapshot s;
    stats.Update(r1, std::nullopt, 0, 5, s);
    // pid 8 被新进程复用（创建时间不同），pid 12 退出，pid 20 新出现
    std::vector<ProcessRecord> r2 = {Proc(8, 3, 30, 1, 2), Proc(20, 4, 10, 1, 3)};
    stats.Update(r2, std::nullopt, 100.0, 5, s);
    CHECK(s.top_cpu_count == 2);
    // 复用 pid 的进程不能拿旧进程的时间做差（否则是负数或巨大值），整段 30 都算本轮
    CHECK(s.top_cpu[0].app_id == 3);
    CHECK(Near(s.top_cpu[0].value, 30.0));
    CHECK(Near(s.top_cpu[1].value, 10.0));
}

TEST_CASE("process stats: hidden group excluded, keeps top N") {
    ProcessStats stats;
    std::vector<ProcessRecord> r;
    for (uint32_t i = 0; i < 7; ++i) {
        r.push_back(Proc(100 + i * 4, i + 1, 0, (i + 1) * 10));
    }
    r.push_back(Proc(300, kHiddenGroup, 0, 1'000'000));  // Memory Compression
    Snapshot s;
    stats.Update(r, std::nullopt, 0, 5, s);
    CHECK(s.top_memory_count == 5);
    CHECK(s.top_memory[0].app_id == 7);
    CHECK(s.top_memory[4].app_id == 3);
}

TEST_CASE("process stats: reset drops the baseline") {
    ProcessStats stats;
    std::vector<ProcessRecord> r = {Proc(8, 1, 100, 1)};
    Snapshot s;
    stats.Update(r, 5.0, 0, 5, s);
    stats.Reset();
    Snapshot s2;
    stats.Update(r, 5.0, 0, 5, s2);
    CHECK(!s2.top_cpu_valid);
}
