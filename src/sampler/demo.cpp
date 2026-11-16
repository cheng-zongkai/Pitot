#include "sampler/demo.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>

namespace pitot::sampler {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint64_t kMiB = 1024ull * 1024;
constexpr uint64_t kGiB = 1024ull * kMiB;

// 0 到 1 之间的平滑起伏：三个不成整数比的正弦叠加，看上去不规则但没有跳变
double Noise(double t, double seed) {
    const double v = 0.55 * std::sin(t / 7.3 + seed) + 0.30 * std::sin(t / 2.9 + 2.1 * seed) +
                     0.15 * std::sin(t / 1.3 + 3.7 * seed);
    return 0.5 + 0.5 * v;
}

// 偶尔一次的突发（打开网页、编译一下），平时接近 0。周期不同的 seed 让各项错开
double Burst(double t, double period, double seed) {
    const double phase = std::sin(2.0 * kPi * (t / period) + seed);
    return phase > 0.0 ? std::pow(phase, 6.0) : 0.0;
}

double Clamp(double v, double lo, double hi) { return std::min(std::max(v, lo), hi); }

void Copy(wchar_t* dst, size_t size, const wchar_t* src) { wcsncpy_s(dst, size, src, _TRUNCATE); }

template <size_t N>
void Copy(wchar_t (&dst)[N], const wchar_t* src) {
    Copy(dst, N, src);
}

constexpr GUID kEthernetGuid = {0x6f1c0a11, 0x2b7d, 0x4d2e, {0x9a, 0x01, 0x5e, 0x7c, 0x31, 0xd4, 0x00, 0x01}};
constexpr GUID kWifiGuid = {0x6f1c0a11, 0x2b7d, 0x4d2e, {0x9a, 0x01, 0x5e, 0x7c, 0x31, 0xd4, 0x00, 0x02}};

// 虚构的程序表。每项的数值乘上各自的起伏，再按大小排序。
// PID 用奇数：Windows 的 PID 总是 4 的倍数，所以“结束进程”碰不到任何真实进程
struct DemoApp {
    const wchar_t* name;
    const wchar_t* image;
    const wchar_t* path;  // 只用来取图标；找不到时用默认图标
    int count;
    double cpu;           // 利用率百分比
    double memory_mib;    // 专用工作集
    double gpu;           // GPU 利用率百分比
    double vram_mib;
    double disk_read;     // 字节/秒
    double disk_write;
    double net_down;
    double net_up;
};

constexpr DemoApp kDemoApps[] = {
    {L"Microsoft Edge", L"msedge.exe", L"%ProgramFiles(x86)%\\Microsoft\\Edge\\Application\\msedge.exe", 12, 6.0,
     1450, 9.0, 620, 250e3, 420e3, 2.6e6, 190e3},
    {L"Host Process for Windows Services", L"svchost.exe", L"%SystemRoot%\\System32\\svchost.exe", 38, 2.2, 420, 0.4,
     12, 640e3, 310e3, 280e3, 90e3},
    {L"Windows Explorer", L"explorer.exe", L"%SystemRoot%\\explorer.exe", 1, 1.4, 190, 2.0, 70, 110e3, 60e3, 95e3, 24e3},
    {L"Desktop Window Manager", L"dwm.exe", L"%SystemRoot%\\System32\\dwm.exe", 1, 1.9, 140, 5.0, 210, 0, 0, 0, 0},
    {L"Antimalware Service Executable", L"MsMpEng.exe", L"", 1, 1.1, 230, 0, 0, 420e3, 90e3, 40e3, 20e3},
    {L"Task Manager", L"Taskmgr.exe", L"%SystemRoot%\\System32\\Taskmgr.exe", 1, 0.9, 98, 1.2, 40, 20e3, 10e3, 6e3, 2e3},
    {L"Notepad", L"notepad.exe", L"%SystemRoot%\\System32\\notepad.exe", 2, 0.1, 64, 0, 0, 5e3, 8e3, 0, 0},
    {L"Paint", L"mspaint.exe", L"%SystemRoot%\\System32\\mspaint.exe", 1, 0.2, 82, 0.3, 24, 3e3, 2e3, 0, 0},
    {L"Registry Editor", L"regedit.exe", L"%SystemRoot%\\regedit.exe", 1, 0.1, 31, 0, 0, 1e3, 1e3, 3e3, 1e3},
    {L"Start", L"StartMenuExperienceHost.exe", L"", 1, 0.05, 56, 0, 0, 2e3, 1e3, 12e3, 4e3},
};
static_assert(std::size(kDemoApps) == 10);

// 一个应用在时刻 t 的活跃度，0.4 到 1.6 之间
double Activity(int app, double t) { return 0.4 + 1.2 * Noise(t, 1.0 + app * 1.7); }

void ExpandPath(const wchar_t* in, wchar_t (&out)[MAX_PATH]) {
    out[0] = L'\0';
    if (in[0]) {
        ExpandEnvironmentStringsW(in, out, MAX_PATH);
    }
}

}  // namespace

void FillDemoMetrics(Snapshot& s, double t, bool english) {
    // 保留进程列表：它由 DemoProcesses 单独填。这里只重置标量部分
    s.sequence += 1;
    s.tick_ms = GetTickCount64();

    // ---- CPU ----
    const double cpu = Clamp(7.0 + 10.0 * Noise(t, 0.3) + 24.0 * Burst(t, 41.0, 0.4), 0.5, 100.0);
    s.cpu_percent = cpu;
    s.cpu_count = 16;
    for (int i = 0; i < kMaxCpus; ++i) {
        s.cpu_core_percent[static_cast<size_t>(i)] =
            i < s.cpu_count ? static_cast<float>(Clamp(cpu * (0.3 + 1.4 * Noise(t, 5.0 + i * 0.9)), 0.0, 100.0)) : 0.0f;
    }

    // ---- 内存：32 GB，用了一半左右 ----
    const double memory = 46.0 + 2.5 * Noise(t / 6.0, 2.0) + 1.5 * Burst(t, 97.0, 1.0);
    s.memory_percent = memory;
    s.memory_total_bytes = 32 * kGiB;
    s.memory_used_bytes = static_cast<uint64_t>(memory / 100.0 * static_cast<double>(s.memory_total_bytes));
    s.memory_speed_mts = 3600;

    // ---- 网络：一块有线、一块无线，无线在用 ----
    const double net_burst = Burst(t, 53.0, 0.2);
    const double rx = 120e3 + 700e3 * Noise(t, 3.0) + 9.5e6 * net_burst;
    const double tx = 15e3 + 80e3 * Noise(t, 4.0) + 600e3 * net_burst;
    s.net_rx_bytes_per_sec = rx;
    s.net_tx_bytes_per_sec = tx;
    s.net_adapter_count = 2;
    s.net_list_count = 2;
    s.net_selected = -1;
    s.net_fallback = NetFallback::None;
    NetAdapter& eth = s.net_list[0];
    eth = NetAdapter{};
    eth.guid = kEthernetGuid;
    Copy(eth.name, english ? L"Ethernet" : L"以太网");
    Copy(eth.description, L"Realtek PCIe 2.5GbE Family Controller");
    eth.physical = true;
    eth.up = false;
    NetAdapter& wifi = s.net_list[1];
    wifi = NetAdapter{};
    wifi.guid = kWifiGuid;
    Copy(wifi.name, L"WLAN");
    Copy(wifi.description, L"Intel(R) Wi-Fi 6E AX211 160MHz");
    wifi.physical = true;
    wifi.wireless = true;
    wifi.up = true;
    wifi.link_bps = 1201000000ull;
    wifi.rx_bytes_per_sec = rx;
    wifi.tx_bytes_per_sec = tx;
    s.ping_state = PingState::Ok;
    s.ping_ms = static_cast<uint32_t>(12 + 6 * Noise(t, 6.0));
    s.traffic_on = true;
    s.traffic_today_rx = 3'400ull * kMiB;
    s.traffic_today_tx = 310ull * kMiB;
    s.traffic_period_rx = 41ull * kGiB;
    s.traffic_period_tx = 4ull * kGiB;

    // ---- 传感器 ----
    SensorReadings& r = s.sensors;
    r = SensorReadings{};
    r.cpu_state = SensorState::Ok;
    r.cpu_vendor = CpuVendor::Amd;
    r.board_state = SensorState::Ok;
    r.cpu_temp = static_cast<float>(41.0 + cpu * 0.45 + 2.0 * Noise(t, 7.0));
    r.ccd[0] = static_cast<float>(*r.cpu_temp - 4.5);
    r.package_watts = static_cast<float>(22.0 + cpu * 1.1);
    r.effective_mhz = static_cast<float>(3700.0 + cpu * 14.0);
    const double board_base[] = {37.0, 34.0, 41.0, 46.0, 36.0, 38.0, 40.0};
    for (int i = 0; i < 7; ++i) {
        r.board_temps[static_cast<size_t>(i)] =
            static_cast<float>(board_base[i] + 0.04 * cpu + 1.5 * Noise(t, 8.0 + i));
    }
    r.board_temp_seen = 0x7F;
    for (int i = 0; i < 7; ++i) {
        r.board_temp_ch[static_cast<size_t>(i)] = {sensors::BoardChip::Nct6687D, static_cast<uint8_t>(i)};
    }
    r.fans[0] = static_cast<int>(820 + cpu * 9 + 40 * Noise(t, 9.0));
    r.fans[1] = static_cast<int>(2300 + 60 * Noise(t, 9.5));
    r.fans[2] = static_cast<int>(690 + cpu * 3 + 25 * Noise(t, 10.0));
    r.fans[3] = static_cast<int>(680 + 25 * Noise(t, 11.0));
    r.fan_seen = 0xF;
    for (int i = 0; i < 4; ++i) {
        r.fan_ch[static_cast<size_t>(i)] = {sensors::BoardChip::Nct6687D, static_cast<uint8_t>(i)};
    }
    r.board_layout = static_cast<uint16_t>(static_cast<unsigned>(sensors::BoardChip::Nct6687D) << 8);

    // ---- 显卡：16 GB 显存的 AMD 卡，扩展指标可用 ----
    GpuReadings& g = s.gpu;
    g = GpuReadings{};
    g.present = true;
    Copy(g.name, L"AMD Radeon RX 7800 XT");
    const double gpu = Clamp(3.0 + 9.0 * Noise(t, 12.0) + 55.0 * Burst(t, 67.0, 2.0), 0.0, 100.0);
    g.usage_percent = gpu;
    g.clock_mhz = static_cast<float>(420.0 + gpu * 20.0);
    g.temperature = static_cast<float>(38.0 + gpu * 0.28);
    g.vram_total_bytes = 16 * kGiB;
    g.vram_used_bytes = static_cast<uint64_t>((2.1 + 0.9 * Noise(t / 8.0, 13.0) + gpu * 0.02) * static_cast<double>(kGiB));
    g.extra_state = GpuExtraState::Ok;
    g.hotspot = static_cast<float>(*g.temperature + 9.0);
    g.memory_temperature = static_cast<float>(*g.temperature + 4.0);
    g.board_power_watts = static_cast<float>(24.0 + gpu * 2.1);
    g.fan_rpm = gpu > 20.0 ? static_cast<int>(900 + gpu * 8) : 0;

    // ---- 存储：两块 NVMe ----
    StorageReadings& st = s.storage;
    st = StorageReadings{};
    st.count = 2;
    const double disk_burst = Burst(t, 37.0, 0.9);
    DiskReadings& c = st.disks[0];
    c.number = 0;
    Copy(c.model, L"Samsung SSD 980 PRO 1TB");
    Copy(c.letters, L"C:");
    c.read_bytes_per_sec = 80e3 + 900e3 * Noise(t, 14.0) + 38e6 * disk_burst;
    c.write_bytes_per_sec = 150e3 + 650e3 * Noise(t, 15.0) + 14e6 * disk_burst;
    c.active_percent = Clamp(1.0 + 4.0 * Noise(t, 16.0) + 60.0 * disk_burst, 0.0, 100.0);
    c.total_bytes = 999'000'000'000ull;
    c.free_bytes = 528'000'000'000ull;
    c.temperature = static_cast<float>(38.0 + 6.0 * disk_burst + 1.5 * Noise(t, 17.0));
    DiskReadings& d = st.disks[1];
    d.number = 1;
    Copy(d.model, L"WD Blue SN580 2TB");
    Copy(d.letters, L"D:");
    d.read_bytes_per_sec = 4e3 + 30e3 * Noise(t, 18.0);
    d.write_bytes_per_sec = 2e3 + 15e3 * Noise(t, 19.0);
    d.active_percent = Clamp(0.2 + 0.8 * Noise(t, 20.0), 0.0, 100.0);
    d.total_bytes = 2'000'000'000'000ull;
    d.free_bytes = 1'310'000'000'000ull;
    d.temperature = static_cast<float>(33.0 + 1.2 * Noise(t, 21.0));

    // ---- 详细指标：演示模式里一直有 ----
    s.detail = true;
    MemoryLists lists;
    lists.total = s.memory_total_bytes;
    lists.modified = 70 * kMiB;
    lists.standby = static_cast<uint64_t>(0.21 * static_cast<double>(lists.total));
    lists.in_use = s.memory_used_bytes;
    lists.free = lists.total > lists.in_use + lists.modified + lists.standby
                     ? lists.total - lists.in_use - lists.modified - lists.standby
                     : 0;
    s.memory_lists = lists;
    s.swap_used_bytes = 82.0 * static_cast<double>(kMiB);
    s.commit_bytes = static_cast<uint64_t>(static_cast<double>(s.memory_used_bytes) * 1.3);
    s.commit_limit_bytes = static_cast<uint64_t>(static_cast<double>(s.memory_total_bytes) * 1.55);
    s.compressed_bytes = 190 * kMiB;
    s.etw_state = EtwState::Ok;
    s.top_io_valid = true;
}

void DemoProcesses::Init(IconStore& icons) {
    for (int i = 0; i < kAppCount; ++i) {
        wchar_t path[MAX_PATH];
        ExpandPath(kDemoApps[i].path, path);
        icon_[i] = path[0] ? icons.Extract(path, icon_version_[i]) : icons.Default(icon_version_[i]);
    }
}

namespace {

struct Ranked {
    int app;
    double value;
};

// 填一个排行：按 value 从大到小取前 kMaxTopProcesses 个，value 为 0 的不上榜
template <typename Metric>
int FillList(std::array<ProcessEntry, kMaxTopProcesses>& list, double t, const int (&icon)[10],
             const uint32_t (&version)[10], IconStore& icons, Metric metric) {
    Ranked ranked[std::size(kDemoApps)];
    int n = 0;
    for (int i = 0; i < static_cast<int>(std::size(kDemoApps)); ++i) {
        const double v = metric(kDemoApps[i], Activity(i, t), t);
        if (v > 0.0) {
            ranked[n++] = {i, v};
        }
    }
    std::sort(ranked, ranked + n, [](const Ranked& a, const Ranked& b) { return a.value > b.value; });
    const int count = std::min(n, kMaxTopProcesses);
    for (int row = 0; row < count; ++row) {
        const DemoApp& app = kDemoApps[ranked[row].app];
        ProcessEntry& e = list[static_cast<size_t>(row)];
        e = ProcessEntry{};
        e.app_id = 1000 + static_cast<uint32_t>(ranked[row].app);
        Copy(e.name, app.name);
        Copy(e.image, app.image);
        e.icon = icon[ranked[row].app];
        e.icon_version = version[ranked[row].app];
        if (e.icon >= 0) {
            icons.Touch(e.icon);
        }
        e.value = ranked[row].value;
        e.count = app.count;
        e.child_count = std::min(app.count, kMaxChildren);
        // 子进程把合计分成不等的几份，加起来还是合计
        double weights[kMaxChildren];
        double total = 0.0;
        for (int c = 0; c < e.child_count; ++c) {
            weights[c] = 1.0 / (1.0 + c);
            total += weights[c];
        }
        for (int c = 0; c < e.child_count; ++c) {
            ProcessChild& child = e.children[static_cast<size_t>(c)];
            child.pid = 40001u + static_cast<uint32_t>(ranked[row].app) * 200u + static_cast<uint32_t>(c) * 2u;
            child.value = e.value * weights[c] / total;
            child.create_time = 0;
        }
    }
    return count;
}

}  // namespace

void DemoProcesses::Fill(Snapshot& s, double t, IconStore& icons) {
    s.top_valid = true;
    s.top_cpu_valid = true;
    s.top_gpu_valid = true;
    s.top_cpu_count = FillList(s.top_cpu, t, icon_, icon_version_, icons,
                               [](const DemoApp& a, double act, double) { return a.cpu * act; });
    s.top_memory_count = FillList(s.top_memory, t, icon_, icon_version_, icons, [](const DemoApp& a, double, double t_) {
        return a.memory_mib * static_cast<double>(kMiB) * (1.0 + 0.01 * std::sin(t_ / 11.0));
    });
    s.top_gpu_count = FillList(s.top_gpu, t, icon_, icon_version_, icons,
                               [](const DemoApp& a, double act, double) { return a.gpu * act; });
    for (int i = 0; i < s.top_gpu_count; ++i) {
        ProcessEntry& e = s.top_gpu[static_cast<size_t>(i)];
        for (const DemoApp& a : kDemoApps) {
            if (wcscmp(a.name, e.name) == 0) {
                e.value2 = a.vram_mib * static_cast<double>(kMiB);
            }
        }
        for (int c = 0; c < e.child_count; ++c) {
            e.children[static_cast<size_t>(c)].value2 = e.value2 / e.child_count;
        }
    }
    s.top_disk_count = FillList(s.top_disk, t, icon_, icon_version_, icons,
                                [](const DemoApp& a, double act, double) { return a.disk_read * act; });
    s.top_net_count = FillList(s.top_net, t, icon_, icon_version_, icons,
                               [](const DemoApp& a, double act, double) { return a.net_down * act; });
    // value2（写入 / 上传）：按程序表里的比例
    auto fill_second = [](std::array<ProcessEntry, kMaxTopProcesses>& list, int count, bool net) {
        for (int i = 0; i < count; ++i) {
            ProcessEntry& e = list[static_cast<size_t>(i)];
            for (const DemoApp& a : kDemoApps) {
                if (wcscmp(a.name, e.name) == 0) {
                    const double first = net ? a.net_down : a.disk_read;
                    const double second = net ? a.net_up : a.disk_write;
                    e.value2 = first > 0.0 ? e.value * second / first : 0.0;
                }
            }
            for (int c = 0; c < e.child_count; ++c) {
                e.children[static_cast<size_t>(c)].value2 = e.value2 / e.child_count;
            }
        }
    };
    fill_second(s.top_disk, s.top_disk_count, false);
    fill_second(s.top_net, s.top_net_count, true);
}

}  // namespace pitot::sampler
