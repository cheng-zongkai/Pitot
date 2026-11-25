#include "sampler/sampler.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <system_error>

#include "common/log.h"
#include "common/messages.h"
#include "common/names.h"
#include "config/paths.h"
#include "etw/kernel_trace.h"
#include "sampler/clock.h"
#include "sampler/demo.h"
#include "sampler/detail.h"
#include "sampler/gpu_processes.h"
#include "sampler/io_prime.h"
#include "sampler/net.h"
#include "sampler/perf.h"
#include "sampler/ping.h"
#include "sampler/smbios.h"
#include "sampler/traffic.h"
#include "sensors/board.h"
#include "sensors/cpu.h"
#include "sensors/gpu.h"
#include "sensors/storage.h"

namespace pitot::sampler {
namespace {

// 流量数据每分钟落盘一次（见 docs/subsystems/network.md）。
constexpr ULONGLONG kTrafficSaveMs = 60 * 1000;
// 浮窗打开时补采第二轮用的间隔。运行频率和进程 CPU 占用都要两轮差值，第一轮只建立基准；
// 按完整周期等下去会让 CPU 面板空着“—”和“正在统计…”一到两秒
constexpr ULONGLONG kDetailPrimeMs = 250;
// GPU 进程计数器打不开时，隔这么久再试一次，免得每秒都写一行日志
constexpr ULONGLONG kGpuStartRetryMs = 10 * 1000;

void SampleMemory(Snapshot& s) {
    MEMORYSTATUSEX m{sizeof(m)};
    if (!GlobalMemoryStatusEx(&m) || m.ullTotalPhys == 0) {
        return;
    }
    s.memory_total_bytes = m.ullTotalPhys;
    s.memory_used_bytes = m.ullTotalPhys - m.ullAvailPhys;
    s.memory_percent = 100.0 * static_cast<double>(s.memory_used_bytes) / static_cast<double>(m.ullTotalPhys);
}

float Or(const std::optional<float>& v) { return v ? *v : -1.0f; }

void LogSensors(const SensorReadings& r) {
    log::Write(L"sensors: cpu state %d temp %.1f ccd1 %.1f pkg %.1f W; board state %d", static_cast<int>(r.cpu_state),
               Or(r.cpu_temp), Or(r.ccd[0]), Or(r.package_watts), static_cast<int>(r.board_state));
    for (int i = 0; i < kBoardTemps; ++i) {
        if (r.board_temps[static_cast<size_t>(i)]) {
            log::Write(L"sensors: temp 0x%03X = %.1f", 0x100 + 2 * i, *r.board_temps[static_cast<size_t>(i)]);
        }
    }
    for (int i = 0; i < kBoardFans; ++i) {
        if (r.fans[static_cast<size_t>(i)] && *r.fans[static_cast<size_t>(i)] > 0) {
            log::Write(L"sensors: fan 0x%03X = %d rpm", 0x140 + 2 * i, *r.fans[static_cast<size_t>(i)]);
        }
    }
}

void LogGpuStorage(const GpuReadings& g, const StorageReadings& st) {
    log::Write(L"gpu: usage %.1f%% clock %.0f MHz temp %.1f vram %.0f / %.0f MB; extra state %d hotspot %.1f mem %.1f "
               L"power %.1f W fan %d",
               g.usage_percent.value_or(-1.0), Or(g.clock_mhz), Or(g.temperature),
               static_cast<double>(g.vram_used_bytes.value_or(0)) / 1048576.0,
               static_cast<double>(g.vram_total_bytes) / 1048576.0, static_cast<int>(g.extra_state), Or(g.hotspot),
               Or(g.memory_temperature), Or(g.board_power_watts), g.fan_rpm.value_or(-1));
    for (int i = 0; i < st.count; ++i) {
        const DiskReadings& d = st.disks[static_cast<size_t>(i)];
        log::Write(L"storage: disk %u [%s] %s temp %.0f read %.0f write %.0f active %.1f%% used %llu / %llu GB", d.number,
                   d.model, d.letters, Or(d.temperature), d.read_bytes_per_sec.value_or(-1.0),
                   d.write_bytes_per_sec.value_or(-1.0), d.active_percent.value_or(-1.0),
                   (d.total_bytes.value_or(0) - d.free_bytes.value_or(0)) >> 30, d.total_bytes.value_or(0) >> 30);
    }
}

// 把这一轮的收发字节记进流量表；浮窗打开时顺带算出今日 / 本月给网络面板
void AccountTraffic(TrafficBook& book, const NetSampler& net, int reset_day, bool detail, uint32_t& last_day,
                    Snapshot& s) {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    const uint32_t today = MakeDate(local.wYear, local.wMonth, local.wDay);
    if (today != last_day) {
        // 跨天（或刚启动）时清掉保留期以外的旧记录
        book.Prune(AddDays(today, 1 - TrafficBook::kKeepDays));
        last_day = today;
    }
    book.Add(today, kAutoTrafficKey, net.auto_round_rx(), net.auto_round_tx());
    net.ForEachRound([&](const GUID& key, uint64_t rx, uint64_t tx) { book.Add(today, key, rx, tx); });
    if (!detail) {
        return;
    }
    // 范围和速率一致：手动选中且已连接的网卡，否则是“自动”
    const GUID& key = net.counted_guid() ? *net.counted_guid() : kAutoTrafficKey;
    const TrafficTotals day = book.Sum(key, today, today);
    const TrafficTotals period = book.Sum(key, PeriodStart(today, reset_day), today);
    s.traffic_on = true;
    s.traffic_today_rx = day.rx;
    s.traffic_today_tx = day.tx;
    s.traffic_period_rx = period.rx;
    s.traffic_period_tx = period.tx;
}

void LogTopIo(const Snapshot& s) {
    for (int i = 0; i < s.top_net_count && i < 3; ++i) {
        const ProcessEntry& p = s.top_net[static_cast<size_t>(i)];
        log::Write(L"etw: net #%d [%s] down %.0f up %.0f B/s", i + 1, p.image, p.value, p.value2);
    }
    for (int i = 0; i < s.top_disk_count && i < 3; ++i) {
        const ProcessEntry& p = s.top_disk[static_cast<size_t>(i)];
        log::Write(L"etw: disk #%d [%s] read %.0f write %.0f B/s", i + 1, p.image, p.value, p.value2);
    }
}

}  // namespace

bool Sampler::Start(HWND notify) {
    if (thread_.joinable()) {
        return true;
    }
    stop_event_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    wake_event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!stop_event_ || !wake_event_) {
        return false;
    }
    notify_ = notify;
    try {
        thread_ = std::thread([this] {
            if (demo_) {
                RunDemo();
            } else {
                Run();
            }
        });
    } catch (const std::system_error& e) {
        log::Write(L"sampler: thread creation failed: %S", e.what());
        return false;
    }
    return true;
}

void Sampler::Stop() {
    if (thread_.joinable()) {
        SetEvent(stop_event_.get());
        thread_.join();
    }
}

void Sampler::SetDetail(bool on) {
    if (detail_.exchange(on) != on && wake_event_) {
        SetEvent(wake_event_.get());
    }
}

void Sampler::SetNetwork(const config::NetworkConfig& network) {
    AcquireSRWLockExclusive(&network_lock_);
    network_ = network;
    ReleaseSRWLockExclusive(&network_lock_);
    network_dirty_.store(true);
    // 换了网卡要马上看到新数值，不等下一个周期
    if (wake_event_) {
        SetEvent(wake_event_.get());
    }
}

void Sampler::SetPanel(int panel) {
    // 切到要单独采集的面板时立即采一轮，不等下一个周期
    const bool on_demand = panel == kPanelGpu || panel == kPanelDisk || panel == kPanelNetwork;
    if (panel_.exchange(panel) != panel && on_demand && wake_event_) {
        SetEvent(wake_event_.get());
    }
}

// 演示模式的采样线程：不初始化任何硬件接口，每个周期用假数据填一份快照（D-083）
void Sampler::RunDemo() noexcept {
    try {
        SetThreadDescription(GetCurrentThread(), L"Pitot sampler (demo)");
        DemoProcesses processes;
        processes.Init(icons_);
        auto snapshot = std::make_unique<Snapshot>();
        const HANDLE events[2] = {stop_event_.get(), wake_event_.get()};
        const ULONGLONG start = GetTickCount64();
        for (;;) {
            const double t = static_cast<double>(GetTickCount64() - start) / 1000.0;
            FillDemoMetrics(*snapshot, t, english_names_.load());
            processes.Fill(*snapshot, t, icons_);
            buffer_.WriteBuffer() = *snapshot;
            buffer_.Publish();
            PostMessageW(notify_, msg::kSnapshotReady, 0, 0);
            if (WaitForMultipleObjects(2, events, FALSE, static_cast<DWORD>(interval_ms_.load())) == WAIT_OBJECT_0) {
                break;
            }
        }
    } catch (...) {
        log::Write(L"sampler: demo stopped by exception");
    }
}

void Sampler::Run() noexcept {
    // 异常不能穿出线程入口（AGENTS.md）
    try {
        SetThreadDescription(GetCurrentThread(), L"Pitot sampler");

        PerfSampler perf;
        perf.Init();
        log::Memory(L"after PDH init");
        NetSampler net;
        net.Init();
        config::NetworkConfig network;
        Pinger pinger;
        // 流量统计（D-041）：启动时读一次数据文件，之后每分钟有变化才写
        auto traffic = std::make_unique<TrafficBook>();
        wchar_t traffic_path[MAX_PATH] = L"";
        if (config::GetLocalDataPath(names::kTrafficFile, traffic_path)) {
            traffic->Load(traffic_path);
            log::Write(L"traffic: %d record(s) loaded", traffic->count());
        }
        uint32_t traffic_day = 0;
        ULONGLONG traffic_saved = GetTickCount64();
        auto save_traffic = [&] {
            if (traffic_path[0] && traffic->dirty() && !traffic->Save(traffic_path)) {
                log::Write(L"traffic: save failed %lu", GetLastError());
            }
        };
        log::Memory(L"after net init");
        DetailSampler detail(icons_);
        detail.Init();
        bool detail_active = false;
        bool prime_pending = false;
        bool extra_load_pending = false;
        bool gpu_prime = false;
        bool gpu_panel_was_on = false;
        sensors::CpuSensors cpu_sensors;
        cpu_sensors.Init();
        sensors::BoardSensors board;
        board.Init();
        sensors::GpuSensor gpu;
        gpu.RefreshAdapter(GetTickCount64());
        GpuReadings gpu_readings;
        sensors::StorageSensor storage;
        storage.Init();
        log::Memory(L"after sensors init");
        // 内存速度不会变，启动时读一次
        const std::optional<uint32_t> memory_speed = ReadMemorySpeed();
        log::Write(L"sampler: memory speed %lu MT/s", memory_speed ? static_cast<unsigned long>(*memory_speed) : 0ul);
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        const int cpu_count = static_cast<int>(si.dwNumberOfProcessors);
        // 传感器读数跨轮保留：2 秒才读一次，中间那轮和拿不到锁的轮次沿用上一次
        SensorReadings readings;
        uint64_t sensor_round = 0;
        bool extra_log_pending = false;
        bool extra_loaded = false;
        // GPU 进程列表 2 秒算一次，中间那轮沿用上一次的结果
        GpuProcessSampler gpu_processes;
        std::vector<GpuProcessUsage> gpu_usage;
        std::array<ProcessEntry, kMaxTopProcesses> gpu_top{};
        int gpu_top_count = 0;
        bool gpu_top_valid = false;
        bool gpu_processes_due = false;
        ULONGLONG gpu_retry_at = 0;  // GPU 进程计数器打不开时，下次再试的时刻
        // 磁盘 / 网络进程列表（ETW，D-037）：上次崩溃留下的会话先清掉
        etw::StopStaleSession();
        etw::KernelTrace trace;
        IoWindow io_window;
        IoPrime io_prime;
        std::vector<etw::IoSample> io_samples;
        std::vector<IoProcessUsage> disk_usage;
        std::vector<IoProcessUsage> net_usage;
        EtwState etw_state = EtwState::Off;
        // 进程速率按 ETW 分界之间的 QPC 时长算（D-063），和事件时间戳同一时钟
        int64_t io_last_cut = 0;
        ULONGLONG io_started_tick = 0;
        int io_rounds = 0;
        auto stop_io = [&] {
            if (etw_state == EtwState::Off) {
                return;
            }
            trace.Stop();
            io_prime.Reset();
            io_window.Reset();
            io_rounds = 0;
            std::vector<etw::IoSample>().swap(io_samples);
            std::vector<IoProcessUsage>().swap(disk_usage);
            std::vector<IoProcessUsage>().swap(net_usage);
            etw_state = EtwState::Off;
            log::Memory(L"etw off");
        };
        auto defer_io_drain = [&](ULONGLONG now, int panel) {
            if (!io_prime.Retry(now, trace.HasActivity(panel == kPanelNetwork))) {
                return false;
            }
            if (io_prime.FlushDue(now) && !trace.Flush()) {
                io_prime.StopFlushing();
            }
            return true;
        };

        uint64_t sequence = 0;
        ULONGLONG next = GetTickCount64();
        const HANDLE events[] = {stop_event_.get(), wake_event_.get()};
        for (;;) {
            ULONGLONG now = GetTickCount64();
            const DWORD wait = next > now ? static_cast<DWORD>(next - now) : 0;
            const DWORD result = WaitForMultipleObjects(2, events, FALSE, wait);
            if (result == WAIT_OBJECT_0) {
                break;
            }
            now = GetTickCount64();
            // 等待可能比毫秒时钟的期限提前返回；不要因此运行一整轮采样。
            if (result == WAIT_TIMEOUT && now < next) {
                continue;
            }

            const bool want_detail = detail_.load();
            const int panel = panel_.load();
            // 首批事件未交付时轻量检查并限次补刷；不要按 100 ms 重跑硬件和进程采集。
            // 用户唤醒不走此捷径，关闭浮窗和切换面板仍然立即处理。
            if (result == WAIT_TIMEOUT && want_detail &&
                (panel == kPanelDisk || panel == kPanelNetwork) &&
                etw_state == EtwState::Starting && trace.running() &&
                defer_io_drain(now, panel)) {
                next = io_prime.deadline();
                continue;
            }
            const bool detail_started = want_detail && !detail_active;
            // 切到 GPU 面板也要补采一轮：显卡利用率和 GPU 进程列表都要两轮采样，
            // 不然进程列表会在“正在统计…”停一个完整周期
            const bool gpu_panel_on = want_detail && panel == kPanelGpu;
            const bool gpu_panel_started = gpu_panel_on && !gpu_panel_was_on;
            gpu_panel_was_on = gpu_panel_on;
            if (detail_active && !want_detail) {
                // ADLX 常驻多占约 7 MB，浮窗关了就卸载（D-033）；GPU 进程查询也在整理堆之前关掉
                gpu.SetExtra(false);
                stop_io();
                if (gpu_processes.active()) {
                    gpu_processes.Stop();
                    std::vector<GpuProcessUsage>().swap(gpu_usage);
                    gpu_top_count = 0;
                    gpu_top_valid = false;
                }
                // 浮窗关了：放掉进程快照缓冲区，下次打开重新建立基准
                detail.Reset();
                prime_pending = false;
                extra_load_pending = false;
                gpu_prime = false;
                // 浮窗那边整理堆时这里还没释放，再整理一次
                HEAP_OPTIMIZE_RESOURCES_INFORMATION optimize{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION, 0};
                HeapSetInformation(nullptr, HeapOptimizeResources, &optimize, sizeof(optimize));
                log::Memory(L"detail off");
            }
            detail_active = want_detail;

            if (network_dirty_.exchange(false)) {
                AcquireSRWLockShared(&network_lock_);
                network = network_;
                ReleaseSRWLockShared(&network_lock_);
                net.Configure(network.adapter, network.show_virtual);
            }

            Snapshot s;
            s.sequence = ++sequence;
            s.tick_ms = now;
            perf.Sample(s);
            SampleMemory(s);
            s.memory_speed_mts = memory_speed;
            // 卡片和进程列表按同一时刻切开（D-060、D-063）：先定 ETW 分界，紧接着读网卡和磁盘计数，
            // 再把缓冲区交出来。分界之后的事件按时间戳留给下一轮。启动阶段仍由 IoPrime 控制第一次刷新
            const bool etw_steady = etw_state == EtwState::Ok && trace.running();
            uint32_t etw_mark = 0;
            int64_t io_cut = 0;
            if (etw_steady) {
                io_cut = Qpc();
                trace.Cut(io_cut);
            }
            net.Sample(s);
            storage.Sample(now, s.storage);
            bool etw_flushed = false;
            if (etw_steady) {
                etw_mark = trace.buffers_delivered();
                etw_flushed = trace.Flush();
            }
            if (network.usage) {
                AccountTraffic(*traffic, net, network.reset_day, want_detail, traffic_day, s);
            }
            if (now - traffic_saved >= kTrafficSaveMs) {
                save_traffic();
                traffic_saved = now;
            }
            if (want_detail) {
                const int64_t detail_begin = Qpc();
                detail.Sample(now, s.cpu_percent, process_rows_.load(), english_names_.load(), s);
                if (detail_started) {
                    // 浮窗刚打开的这一轮要读进程名称和图标，耗时记一行，和浮窗的 open 日志对照（D-089）
                    log::Write(L"sampler: first detail sample %.1f ms", QpcSeconds(Qpc() - detail_begin) * 1000.0);
                }
            }

            // 周期可在设置里改（[general] interval / sensor_interval）；传感器按基础周期的整数倍走
            const int base_ms = interval_ms_.load();
            const ULONGLONG period_ms = static_cast<ULONGLONG>(base_ms);
            const uint64_t sensor_every =
                std::max<uint64_t>(1, static_cast<uint64_t>(sensor_interval_ms_.load() / base_ms));
            const bool sensor_turn = sensor_round++ % sensor_every == 0;
            // 刚加载扩展读数或切到 GPU 面板时提前采样，不等传感器周期
            const bool sample_gpu = sensor_turn || extra_loaded || gpu_panel_started || gpu_prime;
            if (sample_gpu && gpu.RefreshAdapter(now)) {
                // 新适配器的引擎计数和进程列表需要重新建立基准
                gpu_readings = {};
                gpu_processes.Stop();
                gpu_top_count = 0;
                gpu_top_valid = false;
                gpu_processes_due = false;
                gpu_retry_at = 0;
                extra_load_pending = want_detail && gpu.present();
                extra_log_pending = false;
            }
            const bool want_gpu_processes = want_detail && panel == kPanelGpu && gpu.present();
            if (want_gpu_processes && !gpu_processes.active() && now >= gpu_retry_at) {
                // 第一次采集只建立基准；刚打开面板时下一轮是 250 ms 的补采轮，马上出结果。
                // 计数器打不开时隔一阵再试，不是每秒都写一行日志
                if (gpu_processes.Start(gpu.luid())) {
                    gpu_processes.Sample(gpu_usage);
                    log::Memory(L"gpu processes on");
                } else {
                    gpu_retry_at = now + kGpuStartRetryMs;
                }
                gpu_top_count = 0;
                gpu_top_valid = false;
                gpu_processes_due = true;
            } else if (want_gpu_processes && (sensor_turn || gpu_processes_due)) {
                const bool valid = gpu_processes.Sample(gpu_usage);
                detail.SampleGpu(gpu_usage, valid, process_rows_.load(), s);
                gpu_top = s.top_gpu;
                gpu_top_count = s.top_gpu_count;
                gpu_top_valid = s.top_gpu_valid;
                gpu_processes_due = !valid;
            } else if (!want_gpu_processes && gpu_processes.active()) {
                gpu_processes.Stop();
                std::vector<GpuProcessUsage>().swap(gpu_usage);
                gpu_top_count = 0;
                gpu_top_valid = false;
                log::Memory(L"gpu processes off");
            }
            if (want_gpu_processes) {
                s.top_gpu = gpu_top;
                s.top_gpu_count = gpu_top_count;
                s.top_gpu_valid = gpu_top_valid;
            }

            // 一个会话同时供网络和存储面板；两个面板都离开就停（M6 验收：2 秒内）。
            // 启动失败后不每轮重试，等下次打开面板
            const bool want_io = want_detail && (panel == kPanelDisk || panel == kPanelNetwork);
            if (!want_io) {
                stop_io();
            } else if (etw_state == EtwState::Off) {
                const ULONGLONG started = GetTickCount64();
                switch (trace.Start()) {
                case etw::StartResult::Ok:
                    etw_state = EtwState::Starting;
                    io_last_cut = Qpc();
                    io_started_tick = started;
                    io_prime.Start(GetTickCount64());
                    io_window.Reserve(etw::KernelTrace::kMaxProcesses);
                    disk_usage.reserve(etw::KernelTrace::kMaxProcesses);
                    net_usage.reserve(etw::KernelTrace::kMaxProcesses);
                    log::Memory(L"etw on");
                    break;
                case etw::StartResult::NoAccess:
                    etw_state = EtwState::NoAccess;
                    break;
                case etw::StartResult::NoSlot:
                    etw_state = EtwState::NoSlot;
                    break;
                case etw::StartResult::Failed:
                    etw_state = EtwState::Failed;
                    break;
                }
            } else if (trace.active() && !trace.running()) {
                // 会话被外部停掉（例如 logman stop）
                trace.Stop();
                io_prime.Reset();
                etw_state = EtwState::Failed;
            } else if (trace.active()) {
                const IoPrime::Action action = io_prime.Next(GetTickCount64());
                if (action == IoPrime::Action::Flush) {
                    if (trace.Flush()) {
                        io_prime.Flushed(GetTickCount64());
                    } else {
                        // 强制刷新失败时退回 ETW 自己的一秒刷新
                        io_prime.Reset();
                    }
                } else if (!io_prime.pending() || action == IoPrime::Action::Drain) {
                    const ULONGLONG sampled = GetTickCount64();
                    // 完整采样可能跨过等待期限，因此取走事件前必须再次检查是否真的已交付。
                    if (action != IoPrime::Action::Drain || !defer_io_drain(sampled, panel)) {
                        if (etw_flushed) {
                            trace.WaitForBuffers(etw_mark);
                        } else {
                            // 启动阶段，或者稳态刷新失败：现在切开、刷新、等交付
                            io_cut = Qpc();
                            trace.Cut(io_cut);
                            trace.FlushThrough();
                        }
                        trace.Drain(io_samples);
                        io_window.Update(io_samples, QpcSeconds(io_cut - io_last_cut), disk_usage, net_usage);
                        io_last_cut = io_cut;
                        io_prime.Reset();
                        detail.SampleIo(disk_usage, net_usage, process_rows_.load(), s);
                        s.top_io_valid = true;
                        etw_state = EtwState::Ok;
                        if (io_rounds == 0) {
                            log::Write(L"etw: first sample after %llu ms; %d net, %d disk groups", sampled - io_started_tick,
                                       s.top_net_count, s.top_disk_count);
                        }
                        // 会话开了几秒后记一次前几名，方便和资源监视器对照
                        if (++io_rounds == 5) {
                            LogTopIo(s);
                        }
                    }
                }
            }
            s.etw_state = etw_state;
            // 延迟只在网络面板显示，也只在那时测
            pinger.Tick(want_detail && panel == kPanelNetwork && network.ping, network.ping_target, now, s);
            if (sensor_turn) {
                cpu_sensors.SampleThermalPower(readings);
                board.Sample(readings);
            }
            if (sample_gpu) {
                gpu.Sample(gpu_readings);
                gpu.SampleExtra(gpu_readings);
            }
            extra_loaded = false;
            gpu_prime = false;
            s.gpu = gpu_readings;
            if (want_detail) {
                cpu_sensors.SampleFrequency(readings, cpu_count);
            } else {
                readings.effective_mhz.reset();
                cpu_sensors.ResetFrequency();
            }
            readings.cpu_state = cpu_sensors.state();
            readings.cpu_vendor = cpu_sensors.vendor();
            readings.board_state = board.state();
            s.sensors = readings;
            // 第二轮传感器读数（功耗要两轮才有）记一次日志，方便和 HWiNFO 对照
            if (sensor_round == sensor_every * 2 + 1) {
                LogSensors(readings);
                LogGpuStorage(s.gpu, s.storage);
            }
            // 浮窗打开后第一次读到扩展指标时也记一次，方便和驱动面板、任务管理器对照
            if (extra_log_pending && s.gpu.extra_state == GpuExtraState::Ok) {
                extra_log_pending = false;
                LogGpuStorage(s.gpu, s.storage);
            }

            buffer_.WriteBuffer() = s;
            buffer_.Publish();
            PostMessageW(notify_, msg::kSnapshotReady, 0, 0);

            // 按固定节拍走，不累积漂移；睡眠唤醒或长时间卡顿后不追赶。
            // 被唤醒（浮窗刚打开）时从现在重新起算
            next = result == WAIT_OBJECT_0 + 1 ? now + period_ms : next + period_ms;
            if (next <= now) {
                next = now + period_ms;
            }

            // 浮窗刚打开、或刚切到 GPU 面板：这一轮已经交出去了，再按短间隔补采一轮。
            // 运行频率、CPU / GPU 进程列表都要两轮差值，等完整周期的话面板会空一到两秒；
            // ADLX 加载要 1 秒多，放到补采之后，免得挡住那一轮
            if (detail_started || gpu_panel_started) {
                prime_pending = true;
                extra_load_pending = true;
                if (gpu_panel_started) {
                    gpu_prime = true;
                }
                next = now + kDetailPrimeMs;
            } else if (prime_pending) {
                prime_pending = false;
                // 补采完回到正常节拍，不把 250 ms 的相位带下去
                next = now + period_ms;
            }
            if (io_prime.pending()) {
                next = std::min<ULONGLONG>(next, io_prime.deadline());
            }
            if (want_detail && extra_load_pending && !prime_pending && !io_prime.pending()) {
                gpu.SetExtra(true);
                extra_loaded = true;
                extra_log_pending = true;
                extra_load_pending = false;
                log::Memory(L"gpu extra on");
            }
        }
        // 退出前把最后不到一分钟的流量写下去
        save_traffic();
    } catch (const std::exception& e) {
        log::Write(L"sampler: stopped by exception: %S", e.what());
    } catch (...) {
        log::Write(L"sampler: stopped by unknown exception");
    }
}

}  // namespace pitot::sampler
