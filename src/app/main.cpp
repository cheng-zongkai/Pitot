#include <windows.h>

#include <commctrl.h>
#include <imm.h>
#include <shellapi.h>

#include <array>
#include <cwchar>
#include <memory>

#include "app/crash.h"
#include "app/hot_notice.h"
#include "app/pawnio_install.h"
#include "common/log.h"
#include "common/messages.h"
#include "common/names.h"
#include "common/unique_handle.h"
#include "config/config.h"
#include "config/paths.h"
#include "sampler/demo.h"
#include "sampler/sampler.h"
#include "sampler/sensor_keys.h"
#include "ui/flyout_window.h"
#include "ui/format.h"
#include "ui/hot_watch.h"
#include "ui/sensor_list.h"
#include "ui/sensor_share.h"
#include "ui/settings_window.h"
#include "ui/strings.h"
#include "ui/strip_window.h"
#include "ui/taskbar.h"

namespace {

// 浮窗用 kFlyoutPanel 消息把面板编号传给采样线程，两边的编号必须一致
static_assert(static_cast<int>(pitot::ui::FlyoutWindow::Panel::Gpu) == pitot::sampler::Sampler::kPanelGpu);
static_assert(static_cast<int>(pitot::ui::FlyoutWindow::Panel::Disk) == pitot::sampler::Sampler::kPanelDisk);
static_assert(static_cast<int>(pitot::ui::FlyoutWindow::Panel::Network) == pitot::sampler::Sampler::kPanelNetwork);
// 任务栏条把 StripPart 直接当面板编号发过来，两边顺序必须一致（D-066）
static_assert(static_cast<int>(pitot::ui::StripPart::Cpu) ==
              static_cast<int>(pitot::ui::FlyoutWindow::Panel::Cpu));
static_assert(static_cast<int>(pitot::ui::StripPart::Gpu) ==
              static_cast<int>(pitot::ui::FlyoutWindow::Panel::Gpu));
static_assert(static_cast<int>(pitot::ui::StripPart::Memory) ==
              static_cast<int>(pitot::ui::FlyoutWindow::Panel::Memory));
static_assert(static_cast<int>(pitot::ui::StripPart::Storage) ==
              static_cast<int>(pitot::ui::FlyoutWindow::Panel::Disk));
static_assert(static_cast<int>(pitot::ui::StripPart::Network) ==
              static_cast<int>(pitot::ui::FlyoutWindow::Panel::Network));
static_assert(static_cast<int>(pitot::ui::StripPart::Sensors) ==
              static_cast<int>(pitot::ui::FlyoutWindow::Panel::Sensors));

constexpr UINT_PTR kTimerWatchdog = 1;
constexpr UINT_PTR kTimerReattach = 2;
constexpr UINT_PTR kTimerConfig = 3;
constexpr UINT_PTR kTimerHotNotice = 4;
// 通知弹出后多久把临时的通知区图标删掉（系统通知本身大约显示 5 秒）
constexpr UINT kHotNoticeMs = 30000;
// 兜底检查：任务栏条被销毁、换了父窗口、被兄弟窗口盖住、任务栏尺寸或 DPI 变化（D-013）
constexpr UINT kWatchdogMs = 2000;
// explorer 重启后先等任务栏自己的子窗口建好，再挂上去
constexpr UINT kReattachDelayMs = 500;
// 编辑器保存时常连写好几次，等平静下来再读
constexpr UINT kConfigDebounceMs = 300;

struct ChangeNotificationTraits {
    using pointer = HANDLE;
    static pointer invalid() noexcept { return INVALID_HANDLE_VALUE; }
    static void close(pointer h) noexcept { FindCloseChangeNotification(h); }
};

struct App {
    HINSTANCE instance = nullptr;
    HWND main = nullptr;
    UINT taskbar_created = 0;
    wchar_t config_dir[MAX_PATH]{};
    wchar_t config_file[MAX_PATH]{};
    pitot::unique_handle_t<ChangeNotificationTraits> config_watch;
    // 每条任务栏一个任务栏条，主任务栏的排第一个（D-077）。只在挂载时分配，采样循环里不碰
    std::array<std::unique_ptr<pitot::ui::StripWindow>, pitot::ui::kMaxTaskbars> strips;
    pitot::config::Config config;
    pitot::ui::FlyoutWindow flyout;
    pitot::ui::Trends trends;
    pitot::sampler::Sampler sampler;
    // [sensors] 和由它算出的隐藏掩码（D-070）；最高温度、任务栏条的主板 / SSD 温度都按它跳过隐藏的通道
    pitot::config::SensorsConfig sensors;
    pitot::sampler::SensorMask sensor_mask;
    bool sensor_mask_dirty = true;  // 配置变了；主板通道表变了也要重算（看 board_layout）
    // 每轮的传感器列表，同时发布给设置窗口（D-070）
    pitot::ui::SensorList sensor_list;
    pitot::ui::SensorShareWriter sensor_share;
    // 高温通知（D-079）
    pitot::ui::HotWatch hot_watch;
    pitot::app::HotNotice hot_notice;
};

App g_app;

using pitot::ui::ResolveLanguage;
using pitot::ui::SystemUsesChinese;

// 读配置并应用。配置文件不存在时生成带注释的默认模板；老文件缺了新版本加的节，就把那一节的模板追加上去，
// 方便用户直接改
void LoadConfig() {
    if (!g_app.config_file[0]) {
        pitot::ui::SetLanguage(ResolveLanguage(pitot::config::LanguageMode::Auto));
        return;
    }
    // 配置文件里的注释按系统语言写
    const bool english = !SystemUsesChinese();
    if (pitot::config::WriteDefaultConfig(g_app.config_file, english)) {
        pitot::log::Write(L"config: wrote default %s", g_app.config_file);
    } else {
        const int appended = pitot::config::AppendMissingSections(g_app.config_file, english);
        if (appended > 0) {
            pitot::log::Write(L"config: appended %d missing section(s) / key(s)", appended);
        } else if (appended < 0) {
            pitot::log::Write(L"config: file is not UTF-16LE, new sections not appended");
        }
    }
    const pitot::config::Config config = pitot::config::LoadConfig(g_app.config_file);
    const pitot::ui::Language language = ResolveLanguage(config.general.language);
    pitot::ui::SetLanguage(language);
    pitot::ui::SetFahrenheit(config.sensors.fahrenheit);
    g_app.sensors = config.sensors;
    g_app.sensor_mask_dirty = true;
    g_app.trends.window = pitot::ui::HistoryWindow(config.flyout.history_seconds, config.general.interval_ms);
    g_app.sampler.SetEnglishNames(language == pitot::ui::Language::English);
    g_app.sampler.SetProcessRows(config.flyout.process_rows);
    g_app.sampler.SetNetwork(config.network);
    g_app.sampler.SetIntervals(config.general.interval_ms, config.general.sensor_interval_ms);
    g_app.config = config;
    for (auto& strip : g_app.strips) {
        if (strip) {
            strip->SetConfig(config);
        }
    }
    g_app.flyout.SetConfig(config);
}

// 监视配置目录：用户改完 config.ini 保存后当场生效（D-022）
void WatchConfig() {
    g_app.config_watch.reset(FindFirstChangeNotificationW(
        g_app.config_dir, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME));
    if (!g_app.config_watch) {
        pitot::log::Write(L"config: watch %s failed %lu", g_app.config_dir, GetLastError());
    }
}

bool IsElevated() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) {
        return false;
    }
    pitot::unique_handle token(raw);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size) &&
           elevation.TokenIsElevated != 0;
}

pitot::ui::StripValues ToStripValues(const pitot::sampler::Snapshot& s) {
    pitot::ui::StripValues v;
    v.cpu_percent = s.cpu_percent;
    v.memory_percent = s.memory_percent;
    if (s.memory_percent) {
        v.memory_used_bytes = static_cast<double>(s.memory_used_bytes);
        v.memory_total_bytes = static_cast<double>(s.memory_total_bytes);
    }
    v.rx_bytes_per_sec = s.net_rx_bytes_per_sec;
    v.tx_bytes_per_sec = s.net_tx_bytes_per_sec;
    v.disk_read_bytes_per_sec = pitot::sampler::TotalDiskReadRate(s.storage);
    v.disk_write_bytes_per_sec = pitot::sampler::TotalDiskWriteRate(s.storage);
    v.disk_active_percent = pitot::sampler::MaxDiskActivePercent(s.storage);
    v.gpu_percent = s.gpu.usage_percent;
    // 下标与 config::StripTempSource 一致：CPU、GPU、SSD、主板
    v.temperature[0] = s.sensors.cpu_temp;
    v.temperature[1] = s.gpu.temperature;
    v.temperature[2] = pitot::sampler::MaxStorageTemperature(s.storage, g_app.sensor_mask);
    v.temperature[3] = pitot::sampler::MaxBoardTemperature(s.sensors, g_app.sensor_mask);
    v.net_adapter_count = s.net_adapter_count;
    if (s.net_fallback == pitot::sampler::NetFallback::None && s.net_selected >= 0) {
        wcscpy_s(v.net_adapter_name, s.net_list[static_cast<size_t>(s.net_selected)].name);
    }
    return v;
}

pitot::ui::StripWindow* StripFor(HWND tray) {
    for (auto& strip : g_app.strips) {
        if (strip && strip->tray() == tray) {
            return strip.get();
        }
    }
    return nullptr;
}

// 主任务栏总有一个任务栏条；[strip] all_taskbars 打开时副显示器的任务栏也各挂一个（D-077）。
// 任务栏没了（拔掉显示器、explorer 重启换了窗口）的任务栏条在这里拆掉
void EnsureAttached() {
    pitot::ui::TaskbarInfo bars[pitot::ui::kMaxTaskbars];
    int count = pitot::ui::FindTaskbars(bars);
    if (count == 0) {
        return;
    }
    if (!g_app.config.strip.all_taskbars) {
        count = bars[0].primary ? 1 : 0;
    }
    for (auto& strip : g_app.strips) {
        if (!strip) {
            continue;
        }
        bool wanted = false;
        for (int i = 0; i < count; ++i) {
            wanted = wanted || bars[i].tray == strip->tray();
        }
        if (!wanted) {
            pitot::log::Write(L"main: removing strip of tray %p", strip->tray());
            strip.reset();
        }
    }
    for (int i = 0; i < count; ++i) {
        const pitot::ui::TaskbarInfo& bar = bars[i];
        pitot::ui::StripWindow* strip = StripFor(bar.tray);
        if (strip && strip->hwnd() && IsWindow(strip->hwnd()) && GetParent(strip->hwnd()) == bar.tray) {
            strip->Sync(bar);
            continue;
        }
        if (!strip) {
            for (auto& slot : g_app.strips) {
                if (!slot) {
                    slot = std::make_unique<pitot::ui::StripWindow>();
                    slot->SetTrends(&g_app.trends);
                    slot->SetConfig(g_app.config);
                    strip = slot.get();
                    break;
                }
            }
        }
        if (!strip) {
            break;
        }
        pitot::log::Write(L"main: attaching (strip=%p tray=%p primary=%d)", strip->hwnd(), bar.tray, bar.primary);
        if (strip->Attach(g_app.instance, bar, g_app.main)) {
            // 新窗口先显示最近一次的数值，不用等下一轮采样
            strip->SetValues(ToStripValues(g_app.sampler.Latest()));
        }
    }
}

// 最高温度在高温阈值以上持续 10 秒时发一条系统通知（D-079）
void CheckHot(int sensor_count) {
    if (!g_app.sensors.notify) {
        g_app.hot_watch.Reset();
        return;
    }
    const int hottest = pitot::ui::HottestSensor(g_app.sensor_list, sensor_count, g_app.sensors);
    std::optional<double> celsius;
    if (hottest >= 0) {
        celsius = *g_app.sensor_list[static_cast<size_t>(hottest)].value;
    }
    if (!g_app.hot_watch.Update(GetTickCount64(), celsius, g_app.sensors.hot)) {
        return;
    }
    const pitot::ui::SensorItem& item = g_app.sensor_list[static_cast<size_t>(hottest)];
    wchar_t value[24];
    wchar_t threshold[24];
    pitot::ui::FormatTemperature(*celsius, 0, value);
    pitot::ui::FormatTemperature(g_app.sensors.hot, 0, threshold);
    wchar_t body[160];
    _snwprintf_s(body, _TRUNCATE, pitot::ui::Str().kHotBody,
                 pitot::config::SensorName(g_app.sensors, item.key, item.name), value, threshold);
    g_app.hot_notice.Show(g_app.main, pitot::msg::kHotNotify, pitot::ui::Str().kHotTitle, body);
    SetTimer(g_app.main, kTimerHotNotice, kHotNoticeMs, nullptr);
}

void PushTrends(const pitot::sampler::Snapshot& s, const pitot::ui::StripValues& values) {
    g_app.trends.cpu.Push(s.cpu_percent);
    g_app.trends.memory.Push(s.memory_percent);
    g_app.trends.rx.Push(s.net_rx_bytes_per_sec);
    g_app.trends.gpu.Push(s.gpu.usage_percent);
    g_app.trends.storage.Push(pitot::sampler::TotalStorageRate(s.storage));
    g_app.trends.temperature.Push(pitot::sampler::MaxTemperature(s.sensors, s.gpu, s.storage, g_app.sensor_mask));
    g_app.trends.disk_active.Push(values.disk_active_percent);
    g_app.trends.temp_cpu.Push(values.temperature[0]);
    g_app.trends.temp_gpu.Push(values.temperature[1]);
    g_app.trends.temp_storage.Push(values.temperature[2]);
    g_app.trends.temp_board.Push(values.temperature[3]);
}

// 演示模式（D-083）：趋势线从空开始要等好几分钟，先按演示数据补满整个历史
void PrefillDemoTrends() {
    auto snapshot = std::make_unique<pitot::sampler::Snapshot>();
    const double step = static_cast<double>(g_app.config.general.interval_ms) / 1000.0;
    for (size_t i = pitot::ui::kHistoryCapacity; i > 0; --i) {
        pitot::sampler::FillDemoMetrics(*snapshot, -static_cast<double>(i) * step, false);
        PushTrends(*snapshot, ToStripValues(*snapshot));
    }
}

void OnSnapshotReady() {
    if (!g_app.sampler.Acquire()) {
        return;
    }
    const auto& s = g_app.sampler.Latest();
    // 主板通道的键要等芯片识别出来才知道（D-085），所以掩码在这里按需重算
    if (g_app.sensor_mask_dirty || s.sensors.board_layout != g_app.sensor_mask.board_layout) {
        g_app.sensor_mask = pitot::sampler::MakeSensorMask(g_app.sensors, s.sensors);
        g_app.sensor_mask_dirty = false;
    }
    const pitot::ui::StripValues values = ToStripValues(s);
    PushTrends(s, values);
    const int sensor_count = pitot::ui::ListSensors(s, g_app.sensor_list);
    g_app.sensor_share.Publish(g_app.sensor_list, sensor_count);
    for (auto& strip : g_app.strips) {
        if (strip) {
            strip->SetValues(values);
        }
    }
    g_app.flyout.SetValues(values, s);
    CheckHot(sensor_count);
}

// 浮窗里点了设置：设置窗口另起一个进程（D-042）；已经开着就提到前面
void OpenSettings() {
    if (HWND existing = FindWindowW(pitot::names::kSettingsWindowClass, nullptr)) {
        if (IsIconic(existing)) {
            ShowWindow(existing, SW_RESTORE);
        }
        SetForegroundWindow(existing);
        return;
    }
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) {
        return;
    }
    wchar_t command[MAX_PATH + 32];
    _snwprintf_s(command, _TRUNCATE, L"\"%s\" %s", exe, pitot::names::kSettingsArg);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        pitot::log::Write(L"main: cannot start settings window %lu", GetLastError());
        return;
    }
    // 新进程的窗口要能拿到前台
    AllowSetForegroundWindow(pi.dwProcessId);
    pitot::unique_handle process(pi.hProcess);
    pitot::unique_handle thread(pi.hThread);
}

// 浮窗里选了网卡：写回 config.ini，目录通知会让配置重新读一遍；采样线程这里先直接换，不等那 0.3 秒
void SelectAdapter(const GUID* adapter) {
    const std::optional<GUID> choice = adapter ? std::optional<GUID>(*adapter) : std::nullopt;
    if (g_app.config_file[0] && !pitot::config::WriteNetworkAdapter(g_app.config_file, choice)) {
        pitot::log::Write(L"config: cannot save adapter choice");
    }
    pitot::config::Config config = pitot::config::LoadConfig(g_app.config_file);
    config.network.adapter = choice;
    g_app.sampler.SetNetwork(config.network);
}

void RefreshThemes() {
    for (auto& strip : g_app.strips) {
        if (strip) {
            strip->RefreshTheme();
        }
    }
    g_app.flyout.RefreshTheme();
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == g_app.taskbar_created && msg != 0) {
        pitot::log::Write(L"main: TaskbarCreated received");
        SetTimer(hwnd, kTimerReattach, kReattachDelayMs, nullptr);
        return 0;
    }
    switch (msg) {
    case pitot::msg::kSnapshotReady:
        OnSnapshotReady();
        return 0;
    case pitot::msg::kStripLost:
        pitot::log::Write(L"main: strip lost, scheduling reattach");
        SetTimer(hwnd, kTimerReattach, kReattachDelayMs, nullptr);
        return 0;
    case pitot::msg::kTaskbarChanged:
        EnsureAttached();
        return 0;
    case pitot::msg::kStripClicked: {
        // lParam 是被点的那条任务栏（D-077）；浮窗弹在它所在的显示器上
        const HWND tray = reinterpret_cast<HWND>(lparam);
        auto taskbar = pitot::ui::FindTaskbar(tray);
        if (!taskbar) {
            taskbar = pitot::ui::FindTaskbar();
        }
        if (taskbar) {
            using pitot::ui::FlyoutWindow;
            // StripPart 与 FlyoutWindow::Panel 的顺序一一对应（strip_window.h）
            const auto panel = static_cast<FlyoutWindow::Panel>(wparam);
            // 任务栏条靠右时，浮窗的右边和任务栏条的右边对齐（D-074）
            std::optional<int> anchor_right;
            if (g_app.config.strip.position == pitot::config::StripPosition::Right) {
                if (const pitot::ui::StripWindow* strip = StripFor(taskbar->tray)) {
                    anchor_right = strip->ScreenRect().right;
                }
            }
            g_app.flyout.Toggle(*taskbar, panel, anchor_right);
        }
        return 0;
    }
    case pitot::msg::kHotNotify:
        switch (LOWORD(lparam)) {
        case NIN_BALLOONUSERCLICK:
        case NIN_SELECT:
        case WM_LBUTTONUP:
            // 点通知：打开浮窗的传感器面板
            if (const auto taskbar = pitot::ui::FindTaskbar(); taskbar && !g_app.flyout.visible()) {
                g_app.flyout.Show(*taskbar, pitot::ui::FlyoutWindow::Panel::Sensors);
            }
            g_app.hot_notice.Remove();
            break;
        case NIN_BALLOONTIMEOUT:
            g_app.hot_notice.Remove();
            break;
        default:
            break;
        }
        return 0;
    case pitot::msg::kOpenSettings:
        OpenSettings();
        return 0;
    case pitot::msg::kFlyoutVisibility:
        // 详细指标（进程快照、内存页面列表）只在浮窗打开时采（D-027）
        g_app.sampler.SetDetail(wparam != 0);
        return 0;
    case pitot::msg::kFlyoutPanel:
        g_app.sampler.SetPanel(static_cast<int>(wparam));
        return 0;
    case pitot::msg::kSelectAdapter:
        SelectAdapter(reinterpret_cast<const GUID*>(lparam));
        return 0;
    case WM_SETTINGCHANGE:
        // 深浅色切换时 lParam 是 "ImmersiveColorSet"
        if (lparam && wcscmp(reinterpret_cast<const wchar_t*>(lparam), L"ImmersiveColorSet") == 0) {
            RefreshThemes();
        }
        return 0;
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        // 强调色变了
        RefreshThemes();
        return 0;
    case WM_TIMER:
        if (wparam == kTimerHotNotice) {
            KillTimer(hwnd, kTimerHotNotice);
            g_app.hot_notice.Remove();
            return 0;
        }
        if (wparam == kTimerConfig) {
            KillTimer(hwnd, kTimerConfig);
            pitot::log::Write(L"config: reloading");
            LoadConfig();
            EnsureAttached();
            return 0;
        }
        if (wparam == kTimerReattach) {
            KillTimer(hwnd, kTimerReattach);
        }
        EnsureAttached();
        return 0;
    case WM_ENDSESSION:
        // 关机或注销：先停采样线程，让它把最后不到一分钟的流量写下去
        if (wparam) {
            g_app.sampler.Stop();
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kTimerWatchdog);
        KillTimer(hwnd, kTimerReattach);
        KillTimer(hwnd, kTimerConfig);
        KillTimer(hwnd, kTimerHotNotice);
        g_app.hot_notice.Remove();
        g_app.sampler.Stop();
        g_app.flyout.Destroy();
        for (auto& strip : g_app.strips) {
            strip.reset();
        }
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

// 管理员进程默认收不到普通权限进程发来的这些消息（UIPI），要显式放行
void AllowMessagesFromShell(HWND hwnd) {
    const UINT messages[] = {g_app.taskbar_created, WM_SETTINGCHANGE, WM_DWMCOLORIZATIONCOLORCHANGED,
                             pitot::msg::kHotNotify};
    for (UINT m : messages) {
        if (!ChangeWindowMessageFilterEx(hwnd, m, MSGFLT_ALLOW, nullptr)) {
            pitot::log::Write(L"main: ChangeWindowMessageFilterEx(0x%04X) failed %lu", m, GetLastError());
        }
    }
}

}  // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR command_line, _In_ int) {
    if (command_line && (wcscmp(command_line, pitot::names::kOfferPawnIoArg) == 0 ||
                         wcscmp(command_line, pitot::names::kInstallPawnIoArg) == 0)) {
        pitot::log::Init();
        pitot::app::InstallCrashHandler();
        return pitot::app::RunPawnIoInstall(wcscmp(command_line, pitot::names::kOfferPawnIoArg) == 0);
    }
    // 设置窗口是同一个 exe 的另一个进程：不占主进程的单实例互斥量，也不禁用输入法（要输入文字）
    if (command_line && wcsstr(command_line, pitot::names::kSettingsArg)) {
        pitot::log::Init();
        pitot::app::InstallCrashHandler();
        pitot::log::Write(L"settings: start pid=%lu", GetCurrentProcessId());
        return pitot::ui::RunSettings(instance);
    }
    // 验收崩溃记录用：不占单实例互斥量，记一份转储就退出（D-078）
    if (command_line && wcsstr(command_line, pitot::names::kCrashTestArg)) {
        pitot::log::Init();
        pitot::app::InstallCrashHandler();
        pitot::log::Write(L"main: crash test pid=%lu", GetCurrentProcessId());
        volatile int* null_pointer = nullptr;
        *null_pointer = 1;
        return 1;
    }
    // 演示模式要在浮窗创建之前打开：浮窗创建时就会读 CPU 型号和核心拓扑
    const bool demo = command_line && wcsstr(command_line, pitot::names::kDemoArg);
    pitot::sampler::SetDemoMode(demo);
    pitot::unique_handle instance_mutex(CreateMutexW(nullptr, FALSE, pitot::names::kSingleInstanceMutex));
    if (!instance_mutex) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    pitot::log::Init();
    pitot::app::InstallCrashHandler();
    pitot::log::Write(L"main: start pid=%lu elevated=%d", GetCurrentProcessId(), IsElevated());
    // 本进程没有文字输入。浮窗一获得焦点，系统就会把输入法（TSF）加载进来且不再卸载，
    // 实测多占约 18 MB 常驻内存，所以整个进程禁用输入法（D-024）。必须在创建任何窗口之前调用。
    ImmDisableIME(static_cast<DWORD>(-1));
    pitot::log::Memory(L"startup");

    // 悬停提示控件
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    pitot::log::Memory(L"after comctl32");

    g_app.instance = instance;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = pitot::names::kMainWindowClass;
    if (!RegisterClassExW(&wc) || !pitot::ui::StripWindow::Register(instance) ||
        !pitot::ui::FlyoutWindow::Register(instance)) {
        return 1;
    }

    // 隐藏的顶层窗口，负责生命周期和接收广播。不能用 HWND_MESSAGE 窗口，它收不到广播。
    g_app.main = CreateWindowExW(WS_EX_TOOLWINDOW, pitot::names::kMainWindowClass, L"Pitot", WS_OVERLAPPED, 0, 0, 0,
                                 0, nullptr, nullptr, instance, nullptr);
    if (!g_app.main) {
        return 1;
    }
    g_app.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    AllowMessagesFromShell(g_app.main);
    pitot::log::Memory(L"after main window");
    g_app.flyout.Create(instance, g_app.main, &g_app.trends);
    g_app.flyout.SetIconStore(&g_app.sampler.icons());
    pitot::log::Memory(L"after flyout create");

    if (pitot::config::GetConfigPaths(g_app.config_dir, g_app.config_file)) {
        LoadConfig();
        WatchConfig();
    } else {
        pitot::log::Write(L"config: cannot resolve %%APPDATA%%, using defaults");
    }
    pitot::log::Memory(L"after config");

    g_app.sensor_share.Open();
    if (demo) {
        pitot::log::Write(L"main: demo mode, all numbers are made up");
        g_app.sampler.SetDemo(true);
        PrefillDemoTrends();
    }
    if (!g_app.sampler.Start(g_app.main)) {
        pitot::log::Write(L"main: sampler failed to start");
    }
    EnsureAttached();
    pitot::log::Memory(L"after strip attach");
    SetTimer(g_app.main, kTimerWatchdog, kWatchdogMs, nullptr);
    if (!demo) {
        pitot::app::LaunchPawnIoInstall(true);
    }

    // 消息循环同时等配置目录的变更通知，不为它单开线程
    MSG msg{};
    for (;;) {
        HANDLE watch = g_app.config_watch.get();
        const DWORD count = g_app.config_watch ? 1 : 0;
        const DWORD result = MsgWaitForMultipleObjectsEx(count, &watch, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (count == 1 && result == WAIT_OBJECT_0) {
            if (!FindNextChangeNotification(watch)) {
                g_app.config_watch.reset();
            }
            SetTimer(g_app.main, kTimerConfig, kConfigDebounceMs, nullptr);
            continue;
        }
        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) {
            break;
        }
    }
    pitot::log::Write(L"main: exit");
    return static_cast<int>(msg.wParam);
}
