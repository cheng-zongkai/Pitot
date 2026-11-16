#pragma once

#include <guiddef.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

#include "sensors/board_chips.h"

namespace pitot::sampler {

// 每核占用最多记这么多个逻辑处理器（单个处理器组，D-027）
inline constexpr int kMaxCpus = 64;
// 进程列表最多记这么多行，每行展开后最多列这么多个进程
inline constexpr int kMaxTopProcesses = 10;
inline constexpr int kMaxChildren = 8;

struct ProcessChild {
    uint32_t pid = 0;
    double value = 0;
    double value2 = 0;  // GPU 列表的显存字节数；其他列表不用
    int64_t create_time = 0;
};

// 进程列表的一行：同一个程序（同一个可执行文件）的进程合并成一行，像任务管理器那样（D-030）
struct ProcessEntry {
    uint32_t app_id = 0;   // 程序编号，浮窗打开期间稳定，用作展开状态的键
    wchar_t name[64]{};    // 友好名称：文件说明（按系统语言），没有时为映像名
    wchar_t image[40]{};   // 映像名，展开后的子行用
    int icon = -1;         // 图标库里的槽位，-1 表示没有图标
    uint32_t icon_version = 0;
    double value = 0;   // CPU / GPU 列表：利用率百分比；内存列表：专用工作集字节数
    double value2 = 0;  // GPU 列表：专用显存字节数；其他列表不用
    int count = 0;     // 合并了几个进程
    int child_count = 0;
    std::array<ProcessChild, kMaxChildren> children{};  // 按数值从大到小
};

// 物理内存页面列表（SystemMemoryListInformation），字节
struct MemoryLists {
    uint64_t in_use = 0;    // 总量减去下面三项
    uint64_t modified = 0;  // 已修改（含不写回的）
    uint64_t standby = 0;   // 备用（8 个优先级之和）
    uint64_t free = 0;      // 空闲 + 已清零
    uint64_t total = 0;
};

// 传感器（PawnIO）的状态，用来在状态栏说明为什么没有读数
enum class SensorState : uint8_t {
    Starting,     // 还没采过
    Ok,
    NoDriver,     // 没装 PawnIO 或驱动没运行
    Unsupported,  // 模块不支持这台机器（CPU 型号、找不到认识的主板芯片）
    Failed,       // 其他错误，见日志
};

inline constexpr int kBoardTemps = 11;
inline constexpr int kBoardFans = 16;
inline constexpr int kCcds = 8;

// 决定 CPU 温度叫什么、用哪个传感器键（D-084）
enum class CpuVendor : uint8_t { Unknown, Amd, Intel };

// 主板通道是哪颗芯片的哪一路（D-085），用来生成传感器键和默认名称。
// id：Nuvoton EC 系是通道号（寄存器 0x100 + 2id / 0x140 + 2id），Nuvoton bank 系的温度是来源号，其他是芯片内序号
struct BoardChannel {
    sensors::BoardChip chip = sensors::BoardChip::None;
    uint8_t id = 0;
};

// 传感器读数，2 秒一次（有效频率在浮窗打开时 1 秒一次）。每轮快照都带着最近一次的读数
struct SensorReadings {
    SensorState cpu_state = SensorState::Starting;
    SensorState board_state = SensorState::Starting;
    CpuVendor cpu_vendor = CpuVendor::Unknown;

    std::optional<float> cpu_temp;  // ℃：AMD 是 Tctl，Intel 是封装温度
    std::array<std::optional<float>, kCcds> ccd{};  // 只有 AMD 有
    std::optional<float> package_watts;
    std::optional<float> effective_mhz;  // 浮窗打开时才有

    // 主板：下标是槽位，每个槽位对应哪颗芯片的哪一路见 *_ch（最多两颗芯片，第二颗接在第一颗后面）。
    // *_seen 的第 i 位表示这个通道自启动以来出现过有效读数；从没出现过的通道视为没接，不显示
    std::array<std::optional<float>, kBoardTemps> board_temps{};
    uint32_t board_temp_seen = 0;
    std::array<std::optional<int>, kBoardFans> fans{};  // RPM，0 是停转
    uint32_t fan_seen = 0;
    std::array<BoardChannel, kBoardTemps> board_temp_ch{};
    std::array<BoardChannel, kBoardFans> fan_ch{};
    uint16_t board_layout = 0;  // 识别出芯片后非 0；变了说明通道表变了，传感器键要重算
};

// 扩展指标的状态。AMD 是 PitotAdlx.dll（D-033），NVIDIA 是 PitotNvidia.dll（D-081）
enum class GpuExtraState : uint8_t {
    Off,                 // 浮窗关着，没加载
    NoDll,               // AMD 卡，没有 PitotAdlx.dll（没下 ADLX SDK 时是正常情况）
    NotAmd,              // 既不是 AMD 也不是 NVIDIA，扩展指标不显示，也不提示
    Unavailable,         // AMD 驱动没有 ADLX 或初始化失败，见日志
    Ok,
    NoNvidiaDll,         // NVIDIA 卡，没有 PitotNvidia.dll
    NvidiaUnavailable,   // 驱动里没有 nvml.dll，或初始化失败，见日志
};

// 显卡读数（D-033、D-081）。基础指标来自 Windows D3DKMT，2 秒一次常驻；
// 热点 / 显存温度、板卡功耗、风扇来自 ADLX 或 NVML，只在浮窗打开时有
struct GpuReadings {
    bool present = false;  // 找到了能渲染的硬件显卡
    wchar_t name[64]{};
    std::optional<double> usage_percent;  // 3D 引擎利用率
    std::optional<float> clock_mhz;       // 3D 引擎频率
    std::optional<float> temperature;     // 核心温度 ℃
    std::optional<uint64_t> vram_used_bytes;
    uint64_t vram_total_bytes = 0;

    GpuExtraState extra_state = GpuExtraState::Off;
    std::optional<float> hotspot;             // ℃
    std::optional<float> memory_temperature;  // ℃
    std::optional<float> board_power_watts;
    std::optional<int> fan_rpm;  // 0 是停转
};

// 存储：每块物理盘一张卡
inline constexpr int kMaxDisks = 8;
struct DiskReadings {
    uint32_t number = 0;     // PhysicalDriveN
    wchar_t model[48]{};     // 型号（IOCTL_STORAGE_QUERY_PROPERTY 的 ProductId）
    wchar_t letters[24]{};   // 这块盘上的卷，“C:”“D: E:”；没有卷时为空
    std::optional<double> read_bytes_per_sec;
    std::optional<double> write_bytes_per_sec;
    std::optional<double> active_percent;  // 活动时间，100 − % Idle Time
    std::optional<uint64_t> total_bytes;   // 盘上各卷容量之和
    std::optional<uint64_t> free_bytes;
    std::optional<float> temperature;      // 只读 NVMe 的（D-033），℃
};
struct StorageReadings {
    int count = 0;
    std::array<DiskReadings, kMaxDisks> disks{};
};

// 所有盘的容量合计，存储面板顶部的总容量条用
struct StorageCapacity {
    uint64_t used_bytes = 0;
    uint64_t total_bytes = 0;
};

// 一块盘的已用 / 总量；没有卷、只读到一半或总量为 0 时为空
inline std::optional<StorageCapacity> DiskCapacity(const DiskReadings& d) {
    if (!d.total_bytes || !d.free_bytes || *d.total_bytes == 0) {
        return std::nullopt;
    }
    // 空闲量不会超过总量；万一超了，按 0 已用算，免得无符号减法绕回成天文数字
    return StorageCapacity{*d.total_bytes - std::min(*d.free_bytes, *d.total_bytes), *d.total_bytes};
}

// 只加有容量读数的盘；一块都没有时为空，面板上整条隐藏而不是画一个 0
inline std::optional<StorageCapacity> TotalStorageCapacity(const StorageReadings& s) {
    std::optional<StorageCapacity> sum;
    for (int i = 0; i < s.count; ++i) {
        const auto disk = DiskCapacity(s.disks[static_cast<size_t>(i)]);
        if (!disk) {
            continue;
        }
        if (!sum) {
            sum.emplace();
        }
        sum->total_bytes += disk->total_bytes;
        sum->used_bytes += disk->used_bytes;
    }
    return sum;
}

// 所有盘读写速率之和，存储磁贴用；一块盘都没有读数时为空
inline std::optional<double> TotalStorageRate(const StorageReadings& s) {
    std::optional<double> total;
    for (int i = 0; i < s.count; ++i) {
        const DiskReadings& d = s.disks[static_cast<size_t>(i)];
        if (d.read_bytes_per_sec || d.write_bytes_per_sec) {
            total = total.value_or(0.0) + d.read_bytes_per_sec.value_or(0.0) + d.write_bytes_per_sec.value_or(0.0);
        }
    }
    return total;
}

// 所有盘读 / 写速率之和，任务栏条的磁盘项用
inline std::optional<double> TotalDiskReadRate(const StorageReadings& s) {
    std::optional<double> total;
    for (int i = 0; i < s.count; ++i) {
        const DiskReadings& d = s.disks[static_cast<size_t>(i)];
        if (d.read_bytes_per_sec) {
            total = total.value_or(0.0) + *d.read_bytes_per_sec;
        }
    }
    return total;
}

inline std::optional<double> TotalDiskWriteRate(const StorageReadings& s) {
    std::optional<double> total;
    for (int i = 0; i < s.count; ++i) {
        const DiskReadings& d = s.disks[static_cast<size_t>(i)];
        if (d.write_bytes_per_sec) {
            total = total.value_or(0.0) + *d.write_bytes_per_sec;
        }
    }
    return total;
}

// 所有已接通道里的最高温度（CPU Tctl、CCD、主板），磁贴“传感器 · 最高”用
inline std::optional<double> MaxTemperature(const SensorReadings& r) {
    std::optional<double> best;
    auto take = [&](const std::optional<float>& t) {
        if (t && (!best || *t > *best)) {
            best = *t;
        }
    };
    take(r.cpu_temp);
    for (const auto& t : r.ccd) {
        take(t);
    }
    for (size_t i = 0; i < r.board_temps.size(); ++i) {
        if (r.board_temp_seen & (1u << i)) {
            take(r.board_temps[i]);
        }
    }
    return best;
}

// 主板通道里的最高温度（不含 CPU），任务栏条的主板温度项用
inline std::optional<double> MaxBoardTemperature(const SensorReadings& r) {
    std::optional<double> best;
    for (size_t i = 0; i < r.board_temps.size(); ++i) {
        if ((r.board_temp_seen & (1u << i)) && r.board_temps[i] && (!best || *r.board_temps[i] > *best)) {
            best = *r.board_temps[i];
        }
    }
    return best;
}

// 最忙的一块盘的活动时间百分比（100 − 空闲），任务栏条的磁盘活动项用；没有读数时为空
inline std::optional<double> MaxDiskActivePercent(const StorageReadings& s) {
    std::optional<double> best;
    for (int i = 0; i < s.count; ++i) {
        const std::optional<double>& active = s.disks[static_cast<size_t>(i)].active_percent;
        if (active && (!best || *active > *best)) {
            best = *active;
        }
    }
    return best;
}

// 磁盘里的最高温度（只有 NVMe 有），任务栏条的 SSD 温度项用
inline std::optional<double> MaxStorageTemperature(const StorageReadings& s) {
    std::optional<double> best;
    for (int i = 0; i < s.count; ++i) {
        const std::optional<float>& t = s.disks[static_cast<size_t>(i)].temperature;
        if (t && (!best || *t > *best)) {
            best = *t;
        }
    }
    return best;
}

// 再加上显卡和存储：传感器磁贴显示的是全部温度里的最高值。
// 不含热点 / 显存温度：它们只在浮窗打开时有（ADLX），算进来的话磁贴的 60 秒趋势一开浮窗就跳一下
inline std::optional<double> MaxTemperature(const SensorReadings& r, const GpuReadings& g,
                                            const StorageReadings& s) {
    std::optional<double> best = MaxTemperature(r);
    auto take = [&](const std::optional<float>& t) {
        if (t && (!best || *t > *best)) {
            best = *t;
        }
    };
    take(g.temperature);
    for (int i = 0; i < s.count; ++i) {
        take(s.disks[static_cast<size_t>(i)].temperature);
    }
    return best;
}

// 网卡（M7）。列表里有全部物理网卡和已连接的虚拟网卡（VPN、TUN 等），选中的网卡即使断开也在
inline constexpr int kMaxNetAdapters = 8;
struct NetAdapter {
    GUID guid{};
    wchar_t name[40]{};         // 别名：“WLAN”“以太网”
    wchar_t description[64]{};  // 驱动描述：“MediaTek Wi-Fi 6E MT7902 …”
    bool physical = false;
    bool wireless = false;
    bool up = false;
    uint64_t link_bps = 0;  // 接收方向的连接速率，0 是未知
    std::optional<double> rx_bytes_per_sec;
    std::optional<double> tx_bytes_per_sec;
};

// 手动选了网卡但用不了，暂时回到“自动”（D-011）
enum class NetFallback : uint8_t {
    None,
    Missing,       // 找不到这块网卡（拔掉、卸载）
    Disconnected,  // 网卡在，但没连上
};

// 网络延迟（M7）
enum class PingState : uint8_t {
    Off,          // 没开或网络面板没打开
    Waiting,      // 正在解析目标或等第一个回复
    Ok,
    Timeout,      // 1 秒内没有回复
    Unreachable,  // 目标不可达、没有路由等
    Unresolved,   // 域名解析失败
};

// 按进程统计磁盘 / 网络读写（ETW 内核会话，D-037）的状态
enum class EtwState : uint8_t {
    Off,       // 网络、存储面板都没打开
    Starting,  // 会话刚开，还没有第一轮结果
    Ok,
    NoAccess,  // 不是管理员
    NoSlot,    // 系统记录器会话已满
    Failed,    // 其他错误或会话被外部停掉，见日志
};

// 一轮采样的完整结果。UI 只读完整的一份（D-006）。
// std::optional 为空表示“不可用”，和“值为 0”区分开。
// 只放定长数据，复制时不分配堆内存。
struct Snapshot {
    uint64_t sequence = 0;  // 第几轮采样，从 1 开始
    uint64_t tick_ms = 0;   // 采样时刻，GetTickCount64

    std::optional<double> cpu_percent;  // 0–100，口径同任务管理器（D-012）
    // 每个逻辑处理器的占用，下标是处理器编号；cpu_count 为 0 表示不可用
    int cpu_count = 0;
    std::array<float, kMaxCpus> cpu_core_percent{};

    std::optional<double> memory_percent;  // 0–100
    uint64_t memory_used_bytes = 0;        // memory_percent 有值时才有意义
    uint64_t memory_total_bytes = 0;
    // 内存运行速度 MT/s（SMBIOS 的 Configured Memory Speed），启动时读一次，读不到为空
    std::optional<uint32_t> memory_speed_mts;

    // 选定网卡的速率；“自动”时是所有物理网卡之和
    std::optional<double> net_rx_bytes_per_sec;
    std::optional<double> net_tx_bytes_per_sec;
    int net_adapter_count = 0;  // 物理网卡数（“自动”统计的范围）
    int net_list_count = 0;
    std::array<NetAdapter, kMaxNetAdapters> net_list{};
    // 配置里选定的网卡在 net_list 里的下标；-1 是“自动”或找不到。net_fallback 为 None 时统计的就是它
    int net_selected = -1;
    NetFallback net_fallback = NetFallback::None;
    PingState ping_state = PingState::Off;
    std::optional<uint32_t> ping_ms;  // 最近一次往返时间，毫秒
    // 今日 / 本月流量（D-041），范围和上面的速率相同；流量统计关着时 traffic_on 为假
    bool traffic_on = false;
    uint64_t traffic_today_rx = 0;
    uint64_t traffic_today_tx = 0;
    uint64_t traffic_period_rx = 0;  // 本统计周期（从每月重置日起）
    uint64_t traffic_period_tx = 0;

    SensorReadings sensors;
    GpuReadings gpu;
    StorageReadings storage;

    // ---- 以下是详细指标，只在浮窗打开时采集（D-027），其余时候都为空 ----
    bool detail = false;  // 这一轮是否采了详细指标
    std::optional<MemoryLists> memory_lists;  // 需要管理员
    std::optional<double> swap_used_bytes;
    std::optional<uint64_t> commit_bytes;
    std::optional<uint64_t> commit_limit_bytes;
    std::optional<uint64_t> compressed_bytes;  // “Memory Compression”进程的工作集
    // 进程快照：内存列表第一轮就有；CPU 列表要两轮之间的差值，第一轮 top_cpu_valid 为假。
    // 浮窗刚打开时 250 ms 后补采第二轮（D-046），不用等一个完整周期
    bool top_valid = false;
    bool top_cpu_valid = false;
    int top_cpu_count = 0;
    std::array<ProcessEntry, kMaxTopProcesses> top_cpu{};
    int top_memory_count = 0;
    std::array<ProcessEntry, kMaxTopProcesses> top_memory{};
    // GPU 进程列表只在 GPU 面板打开时采（PDH GPU 计数器开销大）；利用率要两轮，第一轮 top_gpu_valid 为假
    bool top_gpu_valid = false;
    int top_gpu_count = 0;
    std::array<ProcessEntry, kMaxTopProcesses> top_gpu{};
    // 磁盘 / 网络进程列表（ETW）只在存储或网络面板打开时采。value 是读取 / 下载，value2 是写入 / 上传，字节/秒
    EtwState etw_state = EtwState::Off;
    bool top_io_valid = false;  // 会话开了一轮以上
    int top_disk_count = 0;
    std::array<ProcessEntry, kMaxTopProcesses> top_disk{};
    int top_net_count = 0;
    std::array<ProcessEntry, kMaxTopProcesses> top_net{};
};

}  // namespace pitot::sampler
