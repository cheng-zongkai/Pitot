// ETW 内核事件解析、定长哈希表、磁盘 / 网络进程列表（D-037）

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

#include "etw/events.h"
#include "etw/io_table.h"
#include "sampler/io_prime.h"
#include "sampler/process_stats.h"
#include "test.h"

namespace {

using namespace pitot::etw;

template <size_t N>
void Put32(std::array<unsigned char, N>& buf, size_t offset, uint32_t v) {
    std::memcpy(buf.data() + offset, &v, sizeof(v));
}

}  // namespace

TEST_CASE("etw: thread events give pid and tid") {
    std::array<unsigned char, 74> buf{};
    Put32(buf, 0, 1234);
    Put32(buf, 4, 5678);
    ParsedEvent e = ParseEvent(kThreadGuid, kThreadDcStart, buf.data(), buf.size(), true);
    CHECK(e.kind == EventKind::ThreadStart && e.pid == 1234u && e.tid == 5678u);
    e = ParseEvent(kThreadGuid, kThreadStart, buf.data(), buf.size(), true);
    CHECK(e.kind == EventKind::ThreadStart);
    e = ParseEvent(kThreadGuid, kThreadEnd, buf.data(), buf.size(), true);
    CHECK(e.kind == EventKind::ThreadEnd && e.tid == 5678u);
    // 会话结束时的枚举不用
    CHECK(ParseEvent(kThreadGuid, kThreadDcEnd, buf.data(), buf.size(), true).kind == EventKind::None);
    CHECK(ParseEvent(kThreadGuid, kThreadStart, buf.data(), 6, true).kind == EventKind::None);
}

TEST_CASE("etw: disk events use the issuing thread and transfer size") {
    std::array<unsigned char, 52> buf{};
    Put32(buf, 8, 65536);   // TransferSize
    Put32(buf, 48, 4321);   // IssuingThreadId
    ParsedEvent e = ParseEvent(kDiskIoGuid, kDiskRead, buf.data(), buf.size(), true);
    CHECK(e.kind == EventKind::DiskRead && e.tid == 4321u && e.bytes == 65536u);
    e = ParseEvent(kDiskIoGuid, kDiskWrite, buf.data(), buf.size(), true);
    CHECK(e.kind == EventKind::DiskWrite);
    CHECK(ParseEvent(kDiskIoGuid, 14, buf.data(), buf.size(), true).kind == EventKind::None);  // FlushBuffers
    CHECK(ParseEvent(kDiskIoGuid, kDiskRead, buf.data(), 51, true).kind == EventKind::None);
    CHECK(ParseEvent(kDiskIoGuid, kDiskRead, buf.data(), buf.size(), false).kind == EventKind::None);
}

TEST_CASE("etw: tcp and udp send / receive, loopback dropped") {
    std::array<unsigned char, 36> v4{};
    Put32(v4, 0, 900);
    Put32(v4, 4, 1460);
    v4[8] = 198;  // 远端 198.18.0.2
    v4[9] = 18;
    v4[11] = 2;
    v4[12] = 198;  // 本地 198.18.0.1
    v4[13] = 18;
    v4[15] = 1;
    ParsedEvent e = ParseEvent(kTcpIpGuid, kNetRecvV4, v4.data(), 28, true);
    CHECK(e.kind == EventKind::NetReceive && e.pid == 900u && e.bytes == 1460u);
    e = ParseEvent(kUdpIpGuid, kNetSendV4, v4.data(), 28, true);
    CHECK(e.kind == EventKind::NetSend);
    // 18 是 TCPCopy（应用取走已收到的数据），算进来会重复
    CHECK(ParseEvent(kTcpIpGuid, 18, v4.data(), 28, true).kind == EventKind::None);
    v4[8] = 127;
    v4[12] = 127;
    CHECK(ParseEvent(kTcpIpGuid, kNetSendV4, v4.data(), 36, true).kind == EventKind::None);

    std::array<unsigned char, 52> v6{};
    Put32(v6, 0, 77);
    Put32(v6, 4, 100);
    v6[8] = 0x20;  // 远端 2000::
    v6[24 + 15] = 1;
    e = ParseEvent(kTcpIpGuid, kNetSendV6, v6.data(), v6.size(), true);
    CHECK(e.kind == EventKind::NetSend && e.pid == 77u && e.bytes == 100u);
    v6[8] = 0;
    v6[8 + 15] = 1;  // 两端都是 ::1
    CHECK(ParseEvent(kUdpIpGuid, kNetRecvV6, v6.data(), v6.size(), true).kind == EventKind::None);
    CHECK(ParseEvent(kTcpIpGuid, kNetRecvV6, v6.data(), 39, true).kind == EventKind::None);
    const GUID other = {1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};
    CHECK(ParseEvent(other, kNetRecvV4, v4.data(), 28, true).kind == EventKind::None);
}

TEST_CASE("etw: physical and tun traffic both reach the process list") {
    std::array<unsigned char, 40> v4{};
    Put32(v4, 0, 900);  // 代理走 WLAN
    Put32(v4, 4, 1460);
    v4[8] = 1;  // 远端 1.1.1.1
    v4[9] = 1;
    v4[10] = 1;
    v4[11] = 1;
    v4[12] = 192;  // 本地 192.168.1.20
    v4[13] = 168;
    v4[14] = 1;
    v4[15] = 20;
    const ParsedEvent wlan = ParseEvent(kTcpIpGuid, kNetRecvV4, v4.data(), 28, true);
    Put32(v4, 0, 901);  // 应用走 TUN
    v4[12] = 198;
    v4[13] = 18;
    v4[14] = 0;
    v4[15] = 1;
    const ParsedEvent tun = ParseEvent(kTcpIpGuid, kNetRecvV4, v4.data(), 28, true);
    CHECK(wlan.kind == EventKind::NetReceive && tun.kind == EventKind::NetReceive);
    const IoSample samples[] = {{wlan.pid, {0, 0, wlan.bytes, 0}}, {tun.pid, {0, 0, tun.bytes, 0}}};
    pitot::sampler::IoWindow window;
    std::vector<pitot::sampler::IoProcessUsage> disk;
    std::vector<pitot::sampler::IoProcessUsage> net;
    window.Update(samples, 1.0, disk, net);
    CHECK(net.size() == 2 && net[0].pid == 900 && net[1].pid == 901);
}

TEST_CASE("io rate uses this interval only") {
    // 上一轮是 0 时不能把这一轮平均成一半，否则卡片已经有流量、进程列表还是小数（D-060）
    pitot::sampler::IoWindow window;
    std::vector<pitot::sampler::IoProcessUsage> disk;
    std::vector<pitot::sampler::IoProcessUsage> net;
    window.Update({}, 1.0, disk, net);
    CHECK(net.empty());
    const IoSample burst[] = {{900, {0, 0, 1000, 0}}};
    window.Update(burst, 1.0, disk, net);
    CHECK(net.size() == 1 && net[0].pid == 900 && net[0].first == 1000.0 && net[0].second == 0.0);
}

TEST_CASE("io startup: collect, flush, wait for delivery and allow cancellation") {
    using pitot::sampler::IoPrime;
    IoPrime startup;
    startup.Start(1000);
    CHECK(startup.pending() && startup.Next(1249) == IoPrime::Action::Wait);
    CHECK(startup.Next(1250) == IoPrime::Action::Flush);
    startup.Flushed(1260);
    CHECK(startup.Next(1359) == IoPrime::Action::Wait);
    CHECK(startup.Next(1360) == IoPrime::Action::Drain);
    startup.Reset();
    CHECK(!startup.pending() && startup.Next(2000) == IoPrime::Action::Wait);
    startup.Start(3000);
    startup.Reset();  // 离开面板后旧的刷新不会在下一次打开时触发
    startup.Start(3100);
    CHECK(startup.Next(3250) == IoPrime::Action::Wait);
    CHECK(startup.Next(3350) == IoPrime::Action::Flush);
}

TEST_CASE("io startup: delayed events do not fall back to the full refresh interval") {
    using pitot::sampler::IoPrime;
    IoPrime startup;
    startup.Start(1000);
    CHECK(!startup.Retry(1250, false));
    startup.Flushed(1250);
    CHECK(!startup.Retry(1349, false));
    CHECK(startup.Retry(1350, false));
    CHECK(startup.deadline() == 1450);
    CHECK(startup.Retry(1450, false));
    CHECK(!startup.Retry(1550, true));
    CHECK(startup.Next(1550) == IoPrime::Action::Drain);
    startup.Reset();
    CHECK(!startup.Retry(2000, false));
}

TEST_CASE("io startup: idle timeout and reopening have bounded waits") {
    using pitot::sampler::IoPrime;
    IoPrime startup;
    startup.Start(1000);
    startup.Flushed(1250);
    CHECK(startup.Retry(2990, false));
    CHECK(startup.deadline() == 3000);
    CHECK(!startup.Retry(3000, false));
    CHECK(startup.Next(3000) == IoPrime::Action::Drain);
    startup.Reset();
    startup.Start(5000);
    startup.Flushed(5250);
    CHECK(startup.Retry(5350, false));
    CHECK(startup.deadline() == 5450);
    startup.Reset();
    CHECK(!startup.pending() && !startup.Retry(5450, false));
}

TEST_CASE("io startup: supplementary flushes are spaced and bounded") {
    using pitot::sampler::IoPrime;
    IoPrime startup;
    startup.Start(1000);
    CHECK(!startup.FlushDue(1250));
    startup.Flushed(1250);
    CHECK(!startup.FlushDue(1449));
    CHECK(startup.FlushDue(1450));
    CHECK(!startup.FlushDue(1450));
    CHECK(!startup.FlushDue(1649));
    CHECK(startup.FlushDue(1650));
    CHECK(startup.FlushDue(1850));
    CHECK(!startup.FlushDue(2050));
    startup.Reset();
    CHECK(!startup.FlushDue(4000));
    startup.Start(5000);
    startup.Flushed(5250);
    CHECK(startup.FlushDue(5450));
    startup.StopFlushing();
    CHECK(!startup.FlushDue(5650));
    startup.Reset();
    startup.Start(6000);
    startup.Flushed(6250);
    CHECK(!startup.FlushDue(8000));
}

TEST_CASE("io startup: sampling can cross the delivery deadline") {
    using pitot::sampler::IoPrime;
    IoPrime startup;
    startup.Start(1000);
    startup.Flushed(1250);
    // 提前醒来后完整采样跨过期限：取数据前应重新询问，空表不能提前结束启动阶段。
    CHECK(startup.Next(1344) == IoPrime::Action::Wait);
    CHECK(!startup.Retry(1344, false));
    CHECK(startup.Next(1360) == IoPrime::Action::Drain);
    CHECK(startup.Retry(1360, false));
    CHECK(startup.deadline() == 1460);
    CHECK(startup.Next(1460) == IoPrime::Action::Drain);
    CHECK(!startup.Retry(1460, true));
}

TEST_CASE("etw: open map insert, find, erase with collisions") {
    OpenMap<uint32_t> map;
    CHECK(map.Init(4));  // 16 个槽，最多装 14 个
    for (uint32_t k = 1; k <= 14; ++k) {
        uint32_t* v = map.FindOrInsert(k * 4);
        CHECK(v != nullptr);
        if (v) {
            *v = k;
        }
    }
    CHECK(map.size() == 14u);
    CHECK(map.FindOrInsert(999) == nullptr);  // 满了
    CHECK(map.FindOrInsert(0) == nullptr);    // 0 不是合法键
    // 删一半，其余都还找得到（后移删除不能把探测链弄断）
    for (uint32_t k = 1; k <= 14; k += 2) {
        map.Erase(k * 4);
    }
    CHECK(map.size() == 7u);
    bool ok = true;
    for (uint32_t k = 1; k <= 14; ++k) {
        const uint32_t* v = map.Find(k * 4);
        ok = ok && (k % 2 == 1 ? v == nullptr : v != nullptr && *v == k);
    }
    CHECK(ok);
    map.Erase(12345);  // 不存在
    CHECK(map.size() == 7u);
    int drained = 0;
    map.Drain([&](uint32_t, uint32_t) { ++drained; });
    CHECK(drained == 7 && map.size() == 0u && map.Find(8) == nullptr);
    map.Release();
    CHECK(!map.ready() && map.Find(8) == nullptr && map.FindOrInsert(8) == nullptr);
}

TEST_CASE("etw: open map stays consistent under churn") {
    // 和一个简单的参照实现比，反复插入删除
    OpenMap<uint32_t> map;
    CHECK(map.Init(6));
    std::vector<uint32_t> ref(200, 0);
    uint32_t seed = 12345;
    bool ok = true;
    for (int step = 0; step < 5000; ++step) {
        seed = seed * 1103515245u + 12345u;
        const uint32_t key = 1 + (seed >> 16) % 199;
        if ((seed >> 8) % 3 == 0) {
            map.Erase(key);
            ref[key] = 0;
        } else if (uint32_t* v = map.FindOrInsert(key)) {
            *v = static_cast<uint32_t>(step + 1);
            ref[key] = static_cast<uint32_t>(step + 1);
        }
        if (step % 97 == 0) {
            size_t count = 0;
            for (uint32_t k = 1; k < 200; ++k) {
                const uint32_t* v = map.Find(k);
                ok = ok && (ref[k] == 0 ? v == nullptr : v != nullptr && *v == ref[k]);
                count += ref[k] != 0;
            }
            ok = ok && count == map.size();
        }
    }
    CHECK(ok);
}

namespace {
// 按 pid 取出 Drain 结果里的磁盘写字节
std::vector<std::pair<uint32_t, uint64_t>> DrainWrites(IoBuckets& b) {
    std::vector<std::pair<uint32_t, uint64_t>> out;
    b.Drain([&](uint32_t key, const IoCounters& c) { out.push_back({key, c.disk_write}); });
    std::sort(out.begin(), out.end());
    return out;
}
void Write(IoBuckets& b, uint32_t key, int64_t ts, uint64_t bytes) {
    if (IoCounters* c = b.Slot(key, ts)) {
        c->disk_write += bytes;
    }
}
}  // namespace

TEST_CASE("io buckets: events after the cut wait for the next round") {
    IoBuckets b;
    CHECK(b.Init(4));
    // 还没有分界：全部记进本轮
    Write(b, 1, 100, 10);
    b.Cut(200);
    Write(b, 1, 150, 5);   // 分界之前发生，刷新时才送达
    Write(b, 1, 250, 7);   // 分界之后：下一轮
    Write(b, 2, 300, 3);
    using V = std::vector<std::pair<uint32_t, uint64_t>>;
    CHECK(DrainWrites(b) == (V{{1, 15}}));
    // 取走之后再到的、分界之前的事件不丢，记进下一轮
    Write(b, 2, 180, 4);
    b.Cut(400);
    CHECK(DrainWrites(b) == (V{{1, 7}, {2, 7}}));
    CHECK(DrainWrites(b).empty());
}

TEST_CASE("io buckets: merge reports entries lost to a full table") {
    IoBuckets b;
    CHECK(b.Init(3));  // 8 槽，最多 7 项
    b.Cut(100);
    for (uint32_t k = 1; k <= 7; ++k) {
        Write(b, k, 50, 1);   // 本轮占满
    }
    Write(b, 9, 150, 1);      // 下一轮
    CHECK(b.Cut(200) == 1u);  // 并不进去
    b.Release();
    CHECK(b.Slot(1, 0) == nullptr);
}

TEST_CASE("io window: rate is only the latest round") {
    using pitot::sampler::IoProcessUsage;
    using pitot::sampler::IoWindow;
    IoWindow window;
    std::vector<IoProcessUsage> disk;
    std::vector<IoProcessUsage> net;
    const IoSample first[] = {{10, {1000, 0, 0, 0}}, {20, {0, 0, 500, 100}}};
    window.Update(first, 1.0, disk, net);
    CHECK(disk.size() == 1 && disk[0].pid == 10u && disk[0].first == 1000.0 && disk[0].second == 0.0);
    CHECK(net.size() == 1 && net[0].pid == 20u && net[0].first == 500.0 && net[0].second == 100.0);

    // 速率只看这一轮：10 用新字节，20 没有新数据时速率是 0，不把上一轮再留一半（D-060）；
    // 行保留着（D-061）
    const IoSample second[] = {{10, {3000, 2000, 0, 0}}, {30, {0, 0, 0, 800}}};
    window.Update(second, 1.0, disk, net);
    CHECK(disk.size() == 1 && disk[0].first == 3000.0 && disk[0].second == 2000.0);
    CHECK(net.size() == 2 && net[0].pid == 20u && net[0].first == 0.0 && net[0].second == 0.0);
    CHECK(net[1].pid == 30u && net[1].second == 800.0);

    window.Reset();
    window.Update({}, 1.0, disk, net);
    CHECK(disk.empty() && net.empty());
    window.Update({}, 0.0, disk, net);
    CHECK(disk.empty());
}

TEST_CASE("io window: rows are held for a few seconds, then dropped") {
    using pitot::sampler::IoProcessUsage;
    using pitot::sampler::IoWindow;
    IoWindow window;
    window.Reserve(8);
    std::vector<IoProcessUsage> disk;
    std::vector<IoProcessUsage> net;
    const IoSample once[] = {{5, {0, 0, 100, 0}}, {9, {0, 0, 0, 50}}};
    window.Update(once, 1.0, disk, net);
    CHECK(net.size() == 2);
    // 之后的几轮都没有新数据：两行还在，速率 0，按 pid 排好
    bool held = true;
    for (int i = 0; i < 5; ++i) {
        window.Update({}, 1.0, disk, net);
        held = held && net.size() == 2 && net[0].pid == 5u && net[1].pid == 9u && net[0].first == 0.0 &&
               net[1].second == 0.0;
    }
    CHECK(held);
    window.Update({}, 1.0, disk, net);
    CHECK(net.empty());

    // 中途有新数据就重新计时；保留的行和新行按 pid 交错排好
    window.Update(once, 1.0, disk, net);
    for (int i = 0; i < 4; ++i) {
        window.Update({}, 1.0, disk, net);
    }
    const IoSample again[] = {{7, {0, 0, 10, 0}}, {9, {0, 0, 0, 25}}};
    window.Update(again, 1.0, disk, net);
    CHECK(net.size() == 3 && net[0].pid == 5u && net[0].first == 0.0);
    CHECK(net[1].pid == 7u && net[1].first == 10.0 && net[2].pid == 9u && net[2].second == 25.0);
    window.Update({}, 1.0, disk, net);  // 5 已经过期，7、9 刚刷新过
    CHECK(net.size() == 2 && net[0].pid == 7u && net[1].pid == 9u);
    // 磁盘和网络各自计时，互不影响
    CHECK(disk.empty());
}

TEST_CASE("io window: short initial round uses actual elapsed time") {
    using pitot::sampler::IoProcessUsage;
    using pitot::sampler::IoWindow;
    IoWindow window;
    std::vector<IoProcessUsage> disk;
    std::vector<IoProcessUsage> net;
    const IoSample first[] = {{20, {0, 0, 350, 0}}};
    window.Update(first, 0.35, disk, net);
    CHECK(net.size() == 1 && net[0].first > 999.9 && net[0].first < 1000.1);
    const IoSample second[] = {{20, {0, 0, 1000, 0}}};
    window.Update(second, 1.0, disk, net);
    CHECK(net.size() == 1 && net[0].first > 999.9 && net[0].first < 1000.1);
}

TEST_CASE("io groups: merged by app, sorted by total") {
    using pitot::sampler::IoProcessGroups;
    using pitot::sampler::IoProcessUsage;
    using pitot::sampler::kHiddenGroup;
    std::array<pitot::sampler::ProcessEntry, pitot::sampler::kMaxTopProcesses> out{};
    IoProcessGroups groups;
    groups.Reserve(8);
    const IoProcessUsage usage[] = {
        {1, 100, 0, 7},  // 程序 7 两个进程，合计 100 + 900
        {2, 0, 900, 7},
        {3, 800, 100, 8},  // 程序 8：合计 900
        {4, 5000, 0, kHiddenGroup},
    };
    const int n = groups.Build(usage, 5, out);
    CHECK(n == 2);
    CHECK(out[0].app_id == 7u && out[0].value == 100.0 && out[0].value2 == 900.0 && out[0].count == 2);
    CHECK(out[0].child_count == 2 && out[0].children[0].pid == 2u && out[0].children[1].pid == 1u);
    CHECK(out[1].app_id == 8u && out[1].count == 1 && out[1].children[0].value == 800.0);
    CHECK(groups.Build(usage, 1, out) == 1 && out[0].app_id == 7u);
    CHECK(groups.Build({}, 5, out) == 0);
}
