#include "ui/flyout_window.h"

#include <dwrite_1.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <timeapi.h>
#include <tlhelp32.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <memory>
#include <new>

#include "common/log.h"
#include "common/messages.h"
#include "sampler/cpu_info.h"
#include "ui/core_bars.h"
#include "ui/format.h"
#include "ui/logo.h"
#include "ui/sensor_list.h"
#include "ui/strings.h"
#include "ui/system_theme.h"

namespace pitot::ui {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kFlyoutClass[] = L"Pitot.Flyout";
// 结束进程结果提示的消失定时器
constexpr UINT_PTR kTimerEndToast = 1;
constexpr UINT kEndToastMs = 3000;
// 展开动画的定时器，约 60 帧每秒。不用 DwmFlush 等 DWM 合成：那会把整个消息循环挡住，任务栏条也跟着卡（D-089）
constexpr UINT_PTR kTimerAnimation = 2;
constexpr UINT kAnimFrameMs = 16;

// 以下尺寸都是 DIP，取自设计稿“浮窗 · 深色”和“桌面全景”
constexpr float kWidth = 380.0f;
constexpr float kHeight = 700.0f;
constexpr int kScreenMarginDip = 12;  // 距屏幕左边、距任务栏顶边
constexpr float kSidePad = 16.0f;
constexpr float kHeaderHeight = 48.0f;  // 一行标题 + 两个 32 DIP 按钮，上下各 8
// 标题栏左上角的单色标志，和字标的大写字母差不多高（D-082）
constexpr float kHeaderLogoWidth = 22.0f;
constexpr float kHeaderLogoHeight = 15.0f;
constexpr float kHeaderLogoGap = 8.0f;
constexpr float kTileHeight = 68.0f;
constexpr float kTileGap = 8.0f;
constexpr float kTilesBottomPad = 12.0f;
constexpr float kFooterHeight = 44.0f;
constexpr float kSectionGap = 8.0f;
constexpr float kCardPadX = 12.0f;
constexpr float kCardPadY = 10.0f;
constexpr float kCardRadius = 6.0f;

constexpr double kAnimSeconds = 0.2;
constexpr float kAnimOffset = 24.0f;
// 收起后这么短的时间里再收到任务栏条单击，说明是同一次点击先让浮窗失焦收起，不要马上又打开
constexpr ULONGLONG kReopenGuardMs = 300;

// Segoe Fluent Icons 字形
constexpr wchar_t kGlyphSun = 0xE706;
constexpr wchar_t kGlyphPin = 0xE718;
constexpr wchar_t kGlyphPinned = 0xE840;
constexpr wchar_t kGlyphSettings = 0xE713;
constexpr wchar_t kGlyphOpenExternal = 0xE8A7;
constexpr wchar_t kGlyphWifi = 0xE701;
constexpr wchar_t kGlyphChevronDown = 0xE70D;
constexpr wchar_t kGlyphChevronRight = 0xE76C;
constexpr wchar_t kGlyphGlobe = 0xE774;
constexpr wchar_t kGlyphEthernet = 0xE839;
constexpr wchar_t kGlyphVpn = 0xE705;

int Scale(int dip, UINT dpi) { return MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI); }

// 从 start 到现在的毫秒数，打开浮窗的耗时日志用
double MsSince(const LARGE_INTEGER& start) {
    LARGE_INTEGER now{};
    LARGE_INTEGER freq{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return static_cast<double>(now.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}

D2D1_RECT_F Rect(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }

bool Contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

wchar_t AdapterGlyph(const sampler::NetAdapter& a) {
    if (!a.physical) {
        return kGlyphVpn;
    }
    return a.wireless ? kGlyphWifi : kGlyphEthernet;
}

bool FontInstalled(IDWriteFactory* factory, const wchar_t* family) {
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    return SUCCEEDED(factory->GetSystemFontCollection(&fonts, FALSE)) &&
           SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists;
}

// ---------- 结束进程的后台任务（D-048）。不在 UI 线程上做系统调用 ----------

struct EndProcessRequest {
    HWND hwnd = nullptr;
    bool group = false;
    sampler::ProcessChild first{};
    std::array<sampler::ProcessChild, sampler::kMaxChildren> children{};
    int child_count = 0;
};

bool SameIdentity(HANDLE process, const sampler::ProcessChild& child) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) {
        return false;
    }
    const uint64_t time = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return time == static_cast<uint64_t>(child.create_time);
}

// 关键进程（csrss、winlogon 等）不结束，也不计入尝试数
bool CanEnd(HANDLE process) {
    BOOL critical = TRUE;
    return IsProcessCritical(process, &critical) && !critical;
}

bool ReadProcessPath(HANDLE process, wchar_t* path, size_t capacity) {
    DWORD length = static_cast<DWORD>(capacity);
    path[0] = L'\0';
    if (!QueryFullProcessImageNameW(process, 0, path, &length)) {
        path[0] = L'\0';
        return false;
    }
    return true;
}

DWORD WINAPI EndProcessWorker(void* raw) {
    std::unique_ptr<EndProcessRequest> request(static_cast<EndProcessRequest*>(raw));
    int attempted = 0;
    int ended = 0;
    if (!request->group) {
        unique_handle process(
            OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, request->first.pid));
        if (process && SameIdentity(process.get(), request->first) && CanEnd(process.get())) {
            ++attempted;
            if (TerminateProcess(process.get(), 1)) {
                ++ended;
            }
        }
    } else {
        // 分组口径是完整的可执行文件路径（D-030）：先从一个还活着的已知进程取路径，
        // 再结束同一路径的所有进程，和任务管理器“结束任务”对合并行的做法一致
        wchar_t target[1024] = L"";
        for (int i = 0; i < request->child_count; ++i) {
            unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, request->children[i].pid));
            if (process && SameIdentity(process.get(), request->children[i]) &&
                ReadProcessPath(process.get(), target, std::size(target))) {
                break;
            }
            target[0] = L'\0';
        }
        if (target[0]) {
            // 不结束 Pitot 自己这一组：设置窗口也是同一个 exe
            wchar_t self[1024] = L"";
            const DWORD self_length = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
            const bool own_exe =
                self_length > 0 && CompareStringOrdinal(self, -1, target, -1, TRUE) == CSTR_EQUAL;
            if (!own_exe) {
                unique_file_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
                if (snapshot) {
                    PROCESSENTRY32W entry{sizeof(entry)};
                    for (BOOL ok = Process32FirstW(snapshot.get(), &entry); ok;
                         ok = Process32NextW(snapshot.get(), &entry)) {
                        const DWORD pid = entry.th32ProcessID;
                        if (pid <= 4 || pid == GetCurrentProcessId()) {
                            continue;
                        }
                        unique_handle process(
                            OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
                        if (!process) {
                            continue;
                        }
                        wchar_t path[1024] = L"";
                        if (!ReadProcessPath(process.get(), path, std::size(path)) ||
                            CompareStringOrdinal(path, -1, target, -1, TRUE) != CSTR_EQUAL ||
                            !CanEnd(process.get())) {
                            continue;
                        }
                        ++attempted;
                        if (TerminateProcess(process.get(), 1)) {
                            ++ended;
                        }
                    }
                }
            }
        } else {
            // 路径拿不到（受保护或都已退出）：只结束快照里认识的那几个，逐个核对创建时间
            for (int i = 0; i < request->child_count; ++i) {
                unique_handle process(OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                                  request->children[i].pid));
                if (!process || !SameIdentity(process.get(), request->children[i]) || !CanEnd(process.get())) {
                    continue;
                }
                ++attempted;
                if (TerminateProcess(process.get(), 1)) {
                    ++ended;
                }
            }
        }
    }
    PostMessageW(request->hwnd, msg::kEndProcessResult, static_cast<WPARAM>(ended), static_cast<LPARAM>(attempted));
    return 0;
}

}  // namespace

FlyoutWindow::~FlyoutWindow() { Destroy(); }

bool FlyoutWindow::Register(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kFlyoutClass;
    return RegisterClassExW(&wc) != 0;
}

bool FlyoutWindow::Create(HINSTANCE instance, HWND notify, const Trends* trends) {
    notify_ = notify;
    trends_ = trends;
    wchar_t brand[64];
    sampler::ReadCpuBrand(brand, std::size(brand));
    sampler::ShortCpuName(brand, cpu_name_);
    if (!sampler::ReadCpuTopology(topology_)) {
        log::Write(L"flyout: cpu topology unavailable");
    }
    // 所有者是隐藏主窗口：不进任务栏和 Alt+Tab。
    // 必须带 WS_CAPTION：DWM 只为“有边框”的窗口画系统背景（亚克力），纯 WS_POPUP 只会拿到
    // 实心底色。标题栏本身由 WM_NCCALCSIZE 收掉，客户区仍铺满整个窗口（D-049）
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kFlyoutClass, Str().kFlyoutTitle,
                             WS_POPUP | WS_CAPTION, 0, 0, 0, 0, notify, nullptr, instance, this);
    if (!hwnd_) {
        log::Write(L"flyout: CreateWindowEx failed %lu", GetLastError());
        return false;
    }
    log::Write(L"flyout: created, cpu=[%s]", cpu_name_);
    return true;
}

void FlyoutWindow::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
}

void FlyoutWindow::Toggle(const TaskbarInfo& taskbar, Panel target, std::optional<int> anchor_right) {
    const std::optional<Panel> wanted =
        config_.strip.click_item ? std::optional<Panel>(ShownPanel(target)) : std::nullopt;
    const bool other_panel = wanted && *wanted != panel_;
    if (visible_) {
        // 固定或关了“点击外部时收起”时浮窗还开着：点另一部分就切过去，点同一部分收起
        if (other_panel) {
            SelectPanel(*wanted);
        } else {
            Hide();
        }
    } else if (GetTickCount64() - hidden_at_ms_ >= kReopenGuardMs || other_panel) {
        // 收起后马上收到的单击，是同一次点击先让浮窗失焦收起了：点的是同一部分就到此为止，
        // 点的是另一部分就在那个面板重新打开
        Show(taskbar, wanted, anchor_right);
    }
}

bool FlyoutWindow::TileShown(Panel panel) const {
    return (config_.flyout.tiles & (1u << static_cast<int>(panel))) != 0;
}

FlyoutWindow::Panel FlyoutWindow::ShownPanel(Panel wanted) const {
    if (TileShown(wanted)) {
        return wanted;
    }
    for (int i = 0; i < 6; ++i) {
        if (TileShown(static_cast<Panel>(i))) {
            return static_cast<Panel>(i);
        }
    }
    return wanted;
}

float FlyoutWindow::TilesHeight() const {
    int shown = 0;
    for (int i = 0; i < 6; ++i) {
        shown += TileShown(static_cast<Panel>(i)) ? 1 : 0;
    }
    const int rows = std::max(1, (shown + 2) / 3);
    return static_cast<float>(rows) * kTileHeight + static_cast<float>(rows - 1) * kTileGap;
}

void FlyoutWindow::SelectPanel(Panel panel) {
    panel_ = ShownPanel(panel);
    scroll_ = 0.0f;
    adapter_menu_ = false;
    end_confirm_ = {};
    end_hover_ = 0;
    PostMessageW(notify_, msg::kFlyoutPanel, static_cast<WPARAM>(panel_), 0);
    Render();
}

void FlyoutWindow::Show(const TaskbarInfo& taskbar, std::optional<Panel> target, std::optional<int> anchor_right) {
    LARGE_INTEGER begin{};
    QueryPerformanceCounter(&begin);
    if (!hwnd_ || visible_ || !EnsureFactories()) {
        return;
    }
    theme_ = LoadFlyoutTheme(config_.general);
    ApplyWindowAttributes();

    const UINT dpi = taskbar.dpi;
    const int w = Scale(static_cast<int>(kWidth), dpi);
    const int h = Scale(static_cast<int>(kHeight), dpi);
    const int margin = Scale(kScreenMarginDip, dpi);
    // 默认离任务栏左缘 12 px；任务栏条靠右时和它右对齐，但不超出这条任务栏
    int x = taskbar.rect.left + margin;
    if (anchor_right) {
        x = std::clamp(*anchor_right - w, static_cast<int>(taskbar.rect.left) + margin,
                       std::max(static_cast<int>(taskbar.rect.left) + margin, static_cast<int>(taskbar.rect.right) - margin - w));
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, taskbar.rect.top - margin - h, w, h, SWP_NOACTIVATE);
    // 让窗口按 WM_NCCALCSIZE 折叠后的边框重算一次，否则第一次显示会露出标题栏（D-049）。
    // 窗口此时还没显示，不会闪；尺寸不变，所以放在定位之后
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    dpi_ = GetDpiForWindow(hwnd_);

    // 点中任务栏条某一部分时打开对应面板；否则按设置里打开时的面板，没指定就停在上次看的
    if (target) {
        panel_ = *target;
    } else if (config_.flyout.open_panel >= 0) {
        panel_ = static_cast<Panel>(config_.flyout.open_panel);
    }
    // 磁贴关掉了的面板打不开，换成第一个开着的（D-072）
    panel_ = ShownPanel(panel_);
    visible_ = true;
    animating_ = true;
    anim_frames_ = 0;
    scroll_ = 0.0f;
    hovered_ = -1;
    open_timing_ = {};
    open_timing_.start = begin;
    QueryPerformanceCounter(&anim_start_);
    open_timing_.setup_ms = MsSince(begin);
    ShowWindow(hwnd_, SW_SHOW);
    // 浮窗必须成为前台窗口，点到别处时才会收到 WM_ACTIVATE(WA_INACTIVE) 而收起
    const BOOL foreground = SetForegroundWindow(hwnd_);
    if (!foreground) {
        log::Write(L"flyout: SetForegroundWindow failed");
    }
    open_timing_.show_ms = MsSince(begin) - open_timing_.setup_ms;
    Render();
    open_timing_.first_frame_ms = MsSince(begin);
    QueryPerformanceCounter(&open_timing_.last_frame);
    if (!StartAnimationTimer()) {
        // 没有定时器就不会再有下一帧：直接画完整内容，免得一直停在动画起点（内容透明、错位）
        animating_ = false;
        Render();
    }
    PostMessageW(notify_, msg::kFlyoutPanel, static_cast<WPARAM>(panel_), 0);
    PostMessageW(notify_, msg::kFlyoutVisibility, 1, 0);
}

void FlyoutWindow::Hide() {
    if (!visible_) {
        return;
    }
    visible_ = false;
    animating_ = false;
    StopAnimationTimer();
    ShowWindow(hwnd_, SW_HIDE);
    ReleaseDeviceResources();
    ReleaseFactories();
    hidden_at_ms_ = GetTickCount64();
    adapter_menu_ = false;
    end_confirm_ = {};
    end_hover_ = 0;
    toast_[0] = L'\0';
    KillTimer(hwnd_, kTimerEndToast);
    // 详细指标停采后就过时了，下次打开不显示旧的 Top 进程
    snapshot_ = sampler::Snapshot{};
    expanded_count_ = 0;
    shown_count_ = 0;
    mouse_in_ = false;
    PostMessageW(notify_, msg::kFlyoutVisibility, 0, 0);
    log::Memory(L"flyout closed");
}

void FlyoutWindow::SetValues(const StripValues& values, const sampler::Snapshot& snapshot) {
    values_ = values;
    // 关着时不存：收起后的一两轮可能还带着详细指标，下次打开会闪一下旧数据。
    // 打开时采样线程会立即补一轮
    if (visible_) {
        snapshot_ = snapshot;
        if (snapshot.detail && open_timing_.detail_ms < 0.0) {
            open_timing_.detail_ms = MsSince(open_timing_.start);
            log::Write(L"flyout: detail data %.1f ms after open", open_timing_.detail_ms);
        }
    }
    if (visible_ && !animating_) {
        Render();
    }
}

void FlyoutWindow::SetConfig(const config::Config& config) {
    config_ = config;
    if (!TileShown(panel_)) {
        panel_ = ShownPanel(panel_);
        if (visible_) {
            PostMessageW(notify_, msg::kFlyoutPanel, static_cast<WPARAM>(panel_), 0);
        }
    }
    RefreshTheme();
}

void FlyoutWindow::RefreshTheme() {
    theme_ = LoadFlyoutTheme(config_.general);
    if (visible_) {
        ApplyWindowAttributes();
        Render();
    }
}

void FlyoutWindow::ApplyWindowAttributes() {
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    // 深色模式决定 Acrylic 的底色
    const BOOL dark = theme_.dark;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TRANSIENTWINDOW;
    DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    // 整个客户区都当作“玻璃”，透明像素处露出 Acrylic
    const MARGINS margins{-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
}

bool FlyoutWindow::EnsureFactories() {
    if (round_stroke_ && dwrite_) {
        return true;
    }
    HRESULT hr = S_OK;
    if (!d2d_) {
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.ReleaseAndGetAddressOf());
    }
    if (SUCCEEDED(hr)) {
        // 独立工厂：它的缓存只属于浮窗，关闭时能整个释放；共享工厂的缓存是全进程的
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwrite_.ReleaseAndGetAddressOf()));
    }
    if (SUCCEEDED(hr)) {
        hr = dwrite_->CreateTypography(typography_.ReleaseAndGetAddressOf());
    }
    if (SUCCEEDED(hr)) {
        hr = typography_->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
    }
    if (SUCCEEDED(hr) && !round_stroke_) {
        const auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                       D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        hr = d2d_->CreateStrokeStyle(&props, nullptr, 0, round_stroke_.ReleaseAndGetAddressOf());
    }
    if (SUCCEEDED(hr) && !logo_ && FAILED(CreateLogoGeometry(d2d_.Get(), logo_.ReleaseAndGetAddressOf()))) {
        // 标志画不出来只少一个图形，不影响浮窗
        log::Write(L"flyout: logo geometry failed");
    }
    if (FAILED(hr)) {
        log::Write(L"flyout: factory init failed hr=0x%08lX", hr);
        round_stroke_.Reset();
        return false;
    }
    font_family_ = FontInstalled(dwrite_.Get(), L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    icon_family_ = FontInstalled(dwrite_.Get(), L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    return true;
}

bool FlyoutWindow::EnsureDeviceResources() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (target_ && width == surface_width_ && height == surface_height_) {
        return true;
    }
    ReleaseDeviceResources();
    if (width <= 0 || height <= 0) {
        return false;
    }

    // 不用 ID2D1HwndRenderTarget：硬件模式会加载显卡驱动且反复创建会泄漏，
    // 软件模式走 WARP，会按核数起一堆工作线程并长期占着内存。DC 渲染目标 + DIB 没有这两个问题。
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_.reset(CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0));
    memory_dc_.reset(CreateCompatibleDC(nullptr));
    if (!dib_ || !memory_dc_) {
        log::Write(L"flyout: DIB/DC creation failed %lu", GetLastError());
        ReleaseDeviceResources();
        return false;
    }
    SelectObject(memory_dc_.get(), dib_.get());

    const auto dpi = static_cast<float>(dpi_);
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    HRESULT hr = d2d_->CreateDCRenderTarget(&props, target_.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = target_->BindDC(memory_dc_.get(), &rc);
    }
    if (SUCCEEDED(hr)) {
        hr = target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush_.ReleaseAndGetAddressOf());
    }
    if (FAILED(hr)) {
        log::Write(L"flyout: render target creation failed hr=0x%08lX", hr);
        ReleaseDeviceResources();
        return false;
    }
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    surface_width_ = width;
    surface_height_ = height;
    return true;
}

void FlyoutWindow::ReleaseDeviceResources() {
    for (auto& bitmap : icon_bitmaps_) {
        bitmap.Reset();
    }
    brush_.Reset();
    target_.Reset();
    memory_dc_.reset();
    dib_.reset();
    surface_width_ = 0;
    surface_height_ = 0;
}

void FlyoutWindow::ReleaseFactories() {
    // 连工厂一起放掉（D-024）：字形缓存挂在 DirectWrite 工厂上，几何缓存和 WARP 设备挂在 D2D 工厂上。
    // 实测保留 D2D 工厂时关闭后仍占约 33 MB，放掉后约 13 MB。
    for (auto& entry : fonts_) {
        entry = {};
    }
    round_stroke_.Reset();
    logo_.Reset();
    typography_.Reset();
    dwrite_.Reset();
    d2d_.Reset();
    // 把各个堆里刚释放的空闲块还给系统，否则私有内存统计不会降
    HEAP_OPTIMIZE_RESOURCES_INFORMATION optimize{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION, 0};
    HeapSetInformation(nullptr, HeapOptimizeResources, &optimize, sizeof(optimize));
}

IDWriteTextFormat* FlyoutWindow::Font(float size, bool semibold) {
    for (auto& entry : fonts_) {
        if (entry.format && entry.size == size && entry.semibold == semibold) {
            return entry.format.Get();
        }
    }
    for (auto& entry : fonts_) {
        if (!entry.format) {
            const DWRITE_FONT_WEIGHT weight = semibold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
            if (FAILED(dwrite_->CreateTextFormat(font_family_, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                                 DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &entry.format))) {
                return nullptr;
            }
            entry.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            entry.format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            entry.size = size;
            entry.semibold = semibold;
            return entry.format.Get();
        }
    }
    return nullptr;
}

float FlyoutWindow::Px(float dip) const { return dip * static_cast<float>(dpi_) / USER_DEFAULT_SCREEN_DPI; }

double FlyoutWindow::AnimationProgress() const {
    if (!animating_) {
        return 1.0;
    }
    LARGE_INTEGER now{};
    LARGE_INTEGER freq{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    const double t = static_cast<double>(now.QuadPart - anim_start_.QuadPart) / static_cast<double>(freq.QuadPart);
    return std::clamp(t / kAnimSeconds, 0.0, 1.0);
}

// ---------------------------------------------------------------- 绘制

bool FlyoutWindow::StartAnimationTimer() {
    // 系统默认的定时器精度约 15.6 ms，16 ms 的定时器实际隔 25 ms 左右才触发，0.2 秒里只有 8 帧。动画期间临时提到 1 ms（D-089）
    if (!timer_resolution_raised_) {
        timer_resolution_raised_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    }
    if (!SetTimer(hwnd_, kTimerAnimation, kAnimFrameMs, nullptr)) {
        StopAnimationTimer();
        return false;
    }
    return true;
}

void FlyoutWindow::StopAnimationTimer() {
    KillTimer(hwnd_, kTimerAnimation);
    if (timer_resolution_raised_) {
        timeEndPeriod(1);
        timer_resolution_raised_ = false;
    }
}

void FlyoutWindow::AdvanceAnimation() {
    if (!animating_ || !visible_) {
        StopAnimationTimer();
        return;
    }
    open_timing_.longest_gap_ms = std::max(open_timing_.longest_gap_ms, MsSince(open_timing_.last_frame));
    QueryPerformanceCounter(&open_timing_.last_frame);
    ++anim_frames_;
    const bool done = AnimationProgress() >= 1.0;
    if (done) {
        animating_ = false;
        StopAnimationTimer();
    }
    // 结束那一帧 animating_ 已经清掉，AnimationProgress 返回 1，画出完整内容
    Render();
    if (done) {
        // 真机验收要求 60 fps：0.2 秒动画应有约 12 帧
        log::Write(L"flyout: open %d frames, setup %.1f ms, show %.1f ms, first frame %.1f ms, longest gap %.1f ms",
                   anim_frames_, open_timing_.setup_ms, open_timing_.show_ms, open_timing_.first_frame_ms,
                   open_timing_.longest_gap_ms);
        log::Memory(L"flyout open");
    }
}

void FlyoutWindow::Render(HDC paint_dc) {
    if (!visible_ || !EnsureDeviceResources()) {
        return;
    }
    hit_count_ = 0;
    const double p = AnimationProgress();
    const double eased = 1.0 - std::pow(1.0 - p, 3.0);  // ease-out cubic

    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0, 0, 0, 0));
    target_->SetTransform(D2D1::Matrix3x2F::Identity());

    // 背景不参与动画：窗口一出现 Acrylic 就在，内容从下方滑入并淡入
    Fill(Rect(0, 0, kWidth, kHeight), theme_.panel);
    const float footer_top = kHeight - kFooterHeight;
    Fill(Rect(0, footer_top, kWidth, kHeight), theme_.footer);

    const bool fading = eased < 1.0;
    if (fading) {
        target_->SetTransform(D2D1::Matrix3x2F::Translation(0, kAnimOffset * static_cast<float>(1.0 - eased)));
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                 D2D1::IdentityMatrix(), static_cast<float>(eased)),
                           nullptr);
    }

    DrawHeader();
    DrawTiles(kHeaderHeight);
    const float separator = kHeaderHeight + TilesHeight() + kTilesBottomPad;
    Fill(Rect(0, separator, kWidth, separator + 1), theme_.stroke);
    DrawPanel(Rect(0, separator + 1, kWidth, footer_top - 1));
    Fill(Rect(0, footer_top - 1, kWidth, footer_top), theme_.stroke);
    DrawFooter(footer_top);
    if (adapter_menu_ && panel_ == Panel::Network) {
        DrawAdapterMenu();
    }
    DrawToast();
    // 二次确认最后画，盖住列表和其他内容（D-048）
    if (end_confirm_.active) {
        DrawEndConfirm();
    }

    if (fading) {
        target_->PopLayer();
    }
    target_->SetTransform(D2D1::Matrix3x2F::Identity());

    const HRESULT hr = target_->EndDraw();
    if (FAILED(hr)) {
        ReleaseDeviceResources();
        return;
    }
    // 32 位整块拷贝会连 alpha 一起写进窗口表面；客户区已延伸为“玻璃”，透明处由 DWM 露出 Acrylic
    HDC dc = paint_dc ? paint_dc : GetDC(hwnd_);
    BitBlt(dc, 0, 0, surface_width_, surface_height_, memory_dc_.get(), 0, 0, SRCCOPY);
    if (!paint_dc) {
        ReleaseDC(hwnd_, dc);
    }
}

void FlyoutWindow::DrawHeader() {
    // 只有名称和两个按钮，不加副标题（docs/design.md 规则 1）。CPU 型号放到 CPU 面板里。
    // 单色标志 + 字标（D-082）；字标全大写、拉开字距（D-048）
    brush_->SetColor(theme_.text);
    FillLogo(target_.Get(), logo_.Get(), brush_.Get(),
             Rect(kSidePad, kHeaderHeight / 2 - kHeaderLogoHeight / 2, kSidePad + kHeaderLogoWidth,
                  kHeaderHeight / 2 + kHeaderLogoHeight / 2));
    const float wordmark_left = logo_ ? kSidePad + kHeaderLogoWidth + kHeaderLogoGap : kSidePad;
    TextSpaced(Str().kFlyoutLogo, 15.0f, true, Rect(wordmark_left, 12, 250, 38), theme_.text, 2.2f);

    // 右上角三个 32 × 32 按钮：主题切换、固定、设置
    const float top = 8.0f;
    const D2D1_RECT_F settings = Rect(kWidth - 12 - 32, top, kWidth - 12, top + 32);
    const D2D1_RECT_F pin = Rect(settings.left - 2 - 32, top, settings.left - 2, top + 32);
    const D2D1_RECT_F theme = Rect(pin.left - 2 - 32, top, pin.left - 2, top + 32);
    for (const auto& [rect, kind] :
         {std::pair{theme, HitKind::Theme}, std::pair{pin, HitKind::Pin}, std::pair{settings, HitKind::Settings}}) {
        AddHit(rect, kind);
        if (hovered_ == hit_count_ - 1) {
            Fill(rect, theme_.hover, 4.0f);
        }
    }
    Icon(kGlyphSun, 16.0f, theme, theme_.text2);
    Icon(pinned_ ? kGlyphPinned : kGlyphPin, 16.0f, pin, pinned_ ? theme_.accent : theme_.text2);
    Icon(kGlyphSettings, 16.0f, settings, theme_.text2);
}

void FlyoutWindow::DrawTiles(float top) {
    // 磁贴只有名称、数值、趋势线，角上不放说明小字（docs/design.md 规则 2）
    struct TileText {
        const wchar_t* label;
        wchar_t value[24];
    };
    TileText tiles[6] = {{Str().kTileCpu}, {Str().kTileGpu},     {Str().kTileMemory},
                         {Str().kTileStorage}, {Str().kTileNetwork}, {Str().kTileSensors}};
    FormatPercent(values_.cpu_percent, tiles[0].value);
    FormatPercent(snapshot_.gpu.usage_percent, tiles[1].value);
    FormatPercent(values_.memory_percent, tiles[2].value);
    // 存储：所有盘读写之和
    FormatRate(sampler::TotalStorageRate(snapshot_.storage), tiles[3].value);
    FormatNetworkRate(values_.rx_bytes_per_sec, NetworkDirection::Download, tiles[4].value);
    // 最高温度和它的来源都从传感器列表取：隐藏的不算，改过名的用别名（D-070）
    const int sensor_count = ListSensors(snapshot_, sensor_list_);
    const int hottest = HottestSensor(sensor_list_, sensor_count, config_.sensors);
    if (hottest >= 0) {
        FormatTemperature(*sensor_list_[static_cast<size_t>(hottest)].value, 0, tiles[5].value);
    } else {
        wcscpy_s(tiles[5].value, Str().kUnavailable);
    }
    const float tile_w = (kWidth - 2 * kSidePad - 2 * kTileGap) / 3.0f;
    // 关掉的磁贴不占位置，后面的往前补（D-072）
    int slot = 0;
    for (int i = 0; i < 6; ++i) {
        if (!TileShown(static_cast<Panel>(i))) {
            continue;
        }
        const float x = kSidePad + static_cast<float>(slot % 3) * (tile_w + kTileGap);
        const float y = top + static_cast<float>(slot / 3) * (kTileHeight + kTileGap);
        ++slot;
        const D2D1_RECT_F r = Rect(x, y, x + tile_w, y + kTileHeight);
        AddHit(r, HitKind::Tile, i);
        const bool selected = static_cast<int>(panel_) == i;
        const bool hovered = hovered_ == hit_count_ - 1;

        Fill(r, selected ? theme_.sel_bg : theme_.card, kCardRadius);
        if (hovered && !selected) {
            Fill(r, theme_.hover, kCardRadius);
        }
        Outline(r, selected ? theme_.sel_border : theme_.card_stroke, kCardRadius);

        Text(tiles[i].label, 12.0f, selected, Rect(x + 10, y + 8, x + tile_w - 10, y + 24), theme_.text2);
        float value_left = x + 10;
        if (i == 5) {
            const float label_width = TextWidth(Str().kTemperatureMax, 11.0f, false);
            Text(Str().kTemperatureMax, 11.0f, false,
                 Rect(value_left, y + 25, value_left + label_width, y + 47), theme_.text3);
            value_left += label_width + 6;
        }
        Text(tiles[i].value, 16.0f, true, Rect(value_left, y + 25, x + tile_w - 10, y + 47), theme_.text);

        const D2D1_RECT_F spark = Rect(x + 10, y + 48, x + 10 + 88, y + 62);
        switch (i) {
        case 0:
            Sparkline(trends_->cpu, spark, 5.0);
            break;
        case 1:
            Sparkline(trends_->gpu, spark, 5.0);
            break;
        case 2:
            Sparkline(trends_->memory, spark, 2.0);
            break;
        case 3:
            Sparkline(trends_->storage, spark, 1024.0 * 1024);
            break;
        case 4:
            Sparkline(trends_->rx, spark, 16.0 * 1024);
            break;
        default:
            if (hottest >= 0) {
                const SensorItem& item = sensor_list_[static_cast<size_t>(hottest)];
                Text(config::SensorName(config_.sensors, item.key, item.name), 11.0f, false, spark, theme_.text3,
                     DWRITE_TEXT_ALIGNMENT_LEADING, true);
            }
            break;
        }
    }
}

void FlyoutWindow::DrawPanel(const D2D1_RECT_F& area) {
    panel_area_ = area;
    target_->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);
    const float x = kSidePad;
    const float w = kWidth - 2 * kSidePad;
    const float start = area.top + 8.0f - scroll_;
    float end = start;
    switch (panel_) {
    case Panel::Cpu:
        end = DrawCpuPanel(x, start, w);
        break;
    case Panel::Gpu:
        end = DrawGpuPanel(x, start, w);
        break;
    case Panel::Memory:
        end = DrawMemoryPanel(x, start, w);
        break;
    case Panel::Disk:
        end = DrawDiskPanel(x, start, w);
        break;
    case Panel::Network:
        end = DrawNetworkPanel(x, start, w);
        break;
    case Panel::Sensors:
        end = DrawSensorsPanel(x, start, w);
        break;
    }
    // 内容总高（不含滚动）减去可视高度，就是最多能滚多远
    const float content_bottom = end - kSectionGap + 12.0f + scroll_;
    max_scroll_ = std::max(0.0f, content_bottom - area.bottom);
    scroll_ = std::clamp(scroll_, 0.0f, max_scroll_);

    if (max_scroll_ > 0.0f) {
        const float view = area.bottom - area.top;
        const float total = view + max_scroll_;
        const float thumb = std::max(24.0f, view * view / total);
        const float y = area.top + (view - thumb) * (scroll_ / max_scroll_);
        D2D1_COLOR_F c = theme_.text3;
        c.a *= 0.5f;
        Fill(Rect(kWidth - 6, y + 2, kWidth - 3, y + thumb - 2), c, 1.5f);
    }
    target_->PopAxisAlignedClip();
}

void FlyoutWindow::DrawFooter(float top) {
    const float cy = top + kFooterHeight / 2.0f;
    const auto& r = snapshot_.sensors;
    const bool all_ok = r.cpu_state == sampler::SensorState::Ok && r.board_state == sampler::SensorState::Ok;
    const bool any_ok = r.cpu_state == sampler::SensorState::Ok || r.board_state == sampler::SensorState::Ok;
    const bool starting =
        r.cpu_state == sampler::SensorState::Starting || r.board_state == sampler::SensorState::Starting;
    // 状态栏只报异常（docs/design.md 规则 4）：正常和刚打开还没读到时都不显示
    if (!all_ok && !starting) {
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kSidePad + 3, cy), 3, 3), [&] {
            brush_->SetColor(any_ok ? theme_.warm : theme_.hot);
            return brush_.Get();
        }());
        Text(SensorStatusText(), 12.0f, false, Rect(kSidePad + 14, top, 260, top + kFooterHeight), theme_.text3,
             DWRITE_TEXT_ALIGNMENT_LEADING, true);
    }

    const float text_w = TextWidth(Str().kFooterTaskManager, 13.0f, false);
    const float right = kWidth - 8.0f;
    const float left = right - (10 + text_w + 6 + 14 + 10);
    const D2D1_RECT_F button = Rect(left, cy - 16, right, cy + 16);
    AddHit(button, HitKind::TaskManager);
    if (hovered_ == hit_count_ - 1) {
        Fill(button, theme_.hover, 4.0f);
    }
    Text(Str().kFooterTaskManager, 13.0f, false, Rect(left + 10, button.top, left + 10 + text_w, button.bottom),
         theme_.accent_text);
    Icon(kGlyphOpenExternal, 14.0f, Rect(right - 10 - 14, button.top, right - 10, button.bottom), theme_.accent_text);
}

const wchar_t* FlyoutWindow::SensorStatusText() const {
    using sampler::SensorState;
    const SensorState cpu = snapshot_.sensors.cpu_state;
    const SensorState board = snapshot_.sensors.board_state;
    if (cpu == SensorState::Ok && board == SensorState::Ok) {
        return Str().kSensorsOk;
    }
    if (cpu == SensorState::Starting || board == SensorState::Starting) {
        return Str().kSensorsStarting;
    }
    if (cpu == SensorState::NoDriver || board == SensorState::NoDriver) {
        return Str().kSensorsNoDriver;
    }
    if (cpu == SensorState::Ok || board == SensorState::Ok) {
        return Str().kSensorsPartial;
    }
    if (cpu == SensorState::Unsupported && board == SensorState::Unsupported) {
        return Str().kSensorsUnsupported;
    }
    return Str().kSensorsFailed;
}

// ---------------------------------------------------------------- 面板

float FlyoutWindow::DrawCpuPanel(float x, float y, float w) {
    const auto& r = snapshot_.sensors;
    if (r.cpu_state != sampler::SensorState::Ok && r.cpu_state != sampler::SensorState::Starting) {
        y = Notice(x, y, w, SensorStatusText());
    }
    // CPU 型号作为面板第一节的标题，不放在标题栏的副标题里
    y = SectionTitle(x, y, w, cpu_name_);
    wchar_t freq[24];
    wchar_t power[24];
    wchar_t temp[24];
    wcscpy_s(freq, Str().kUnavailable);
    wcscpy_s(power, Str().kUnavailable);
    wcscpy_s(temp, Str().kUnavailable);
    if (r.effective_mhz) {
        _snwprintf_s(freq, _TRUNCATE, Str().kGhzFormat, *r.effective_mhz / 1000.0);
    }
    if (r.package_watts) {
        _snwprintf_s(power, _TRUNCATE, Str().kWattFormat, *r.package_watts);
    }
    int tone = 0;
    if (r.cpu_temp) {
        FormatTemperature(*r.cpu_temp, 1, temp);
        tone = TemperatureTone(*r.cpu_temp);
    }
    const wchar_t* temp_label =
        r.cpu_vendor == sampler::CpuVendor::Intel ? Str().kStatCpuPackage : Str().kStatTctl;
    const Stat stats[] = {
        {Str().kStatFrequency, freq, 0}, {Str().kStatPackagePower, power, 0}, {temp_label, temp, tone}};
    y = StatsCard(x, y, w, stats, 3);
    y = DrawCoreGrid(x, y, w);
    y = SectionTitle(x, y, w, Str().kTopProcesses);
    return TopProcesses(x, y, w, snapshot_.top_cpu.data(), snapshot_.top_cpu_count,
                        snapshot_.top_valid && snapshot_.top_cpu_valid, ProcessValue::Percent);
}

float FlyoutWindow::DrawCoreGrid(float x, float y, float w) {
    // 每个物理核一个圆环；拿不到拓扑时每个逻辑处理器当成一个核
    sampler::CpuTopology fallback;
    const sampler::CpuTopology* topo = &topology_;
    if (topology_.core_count == 0) {
        // 拿不到拓扑就每个逻辑处理器当一个核
        fallback.core_count = snapshot_.cpu_count;
        fallback.thread_count = snapshot_.cpu_count;
        for (int i = 0; i < snapshot_.cpu_count; ++i) {
            fallback.cores[static_cast<size_t>(i)].threads[0] = i;
            fallback.cores[static_cast<size_t>(i)].thread_count = 1;
        }
        topo = &fallback;
    }
    y = SectionTitle(x, y, w, Str().kPerCoreTitle);
    if (topo->core_count == 0) {
        return y;
    }

    // 一行圆环（D-058）：每个物理核一个，弧长取该核各线程里较高的占用。不写数字，也不标核号（D-087）。
    const CoreBarLayout layout = LayoutCoreBars(topo->core_count, w);
    const int cols = layout.cols;
    const float group_w = layout.group_w;
    const float diameter = CoreRingDiameter(group_w);
    const float pitch = group_w + layout.core_gap;
    const float block = diameter;
    for (int c = 0; c < topo->core_count; ++c) {
        const sampler::CpuCore& core = topo->cores[static_cast<size_t>(c)];
        const float gx = x + static_cast<float>(c % cols) * pitch;
        const float top = y + static_cast<float>(c / cols) * (block + kSectionGap);
        float load = 0.0f;
        for (int t = 0; t < core.thread_count; ++t) {
            const int cpu = core.threads[static_cast<size_t>(t)];
            if (cpu >= 0 && cpu < snapshot_.cpu_count) {
                load = std::max(load, std::clamp(snapshot_.cpu_core_percent[static_cast<size_t>(cpu)], 0.0f, 100.0f));
            }
        }
        DrawLoadRing(gx + group_w / 2.0f, top + diameter / 2.0f, diameter, load);
    }
    return y + static_cast<float>(layout.rows) * (block + kSectionGap);
}

void FlyoutWindow::DrawLoadRing(float cx, float cy, float diameter, float load) {
    const float thickness = std::clamp(diameter * 0.18f, 2.0f, 4.5f);
    const float radius = diameter / 2.0f - thickness / 2.0f;
    if (radius <= 0.5f) {
        return;
    }
    const D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
    brush_->SetColor(theme_.track);
    target_->DrawEllipse(ellipse, brush_.Get(), thickness);
    const float sweep = CoreRingSweepDegrees(load);
    if (sweep <= 0.0f || !target_) {
        return;
    }
    brush_->SetColor(theme_.accent);
    if (sweep >= 360.0f) {
        target_->DrawEllipse(ellipse, brush_.Get(), thickness);
        return;
    }
    constexpr float kPi = 3.14159265f;
    const float a0 = -kPi / 2.0f;
    const float a1 = a0 + sweep * kPi / 180.0f;
    const D2D1_POINT_2F start{cx + radius * std::cos(a0), cy + radius * std::sin(a0)};
    const D2D1_POINT_2F end{cx + radius * std::cos(a1), cy + radius * std::sin(a1)};
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(d2d_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) {
        return;
    }
    sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(end, D2D1::SizeF(radius, radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                  sweep > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    target_->DrawGeometry(path.Get(), brush_.Get(), thickness, round_stroke_.Get());
}

float FlyoutWindow::TopProcesses(float x, float y, float w, const sampler::ProcessEntry* entries, int count,
                                 bool valid, ProcessValue kind, const wchar_t* idle) {
    // 鼠标停在列表上（或右键菜单开着）时锁住行的顺序，免得要右键的那一行被新数据挤走（D-044）。
    // 锁住期间只刷新还在榜上的行和子进程的数值，不增删、不重排
    const bool hold = shown_count_ > 0 && shown_panel_ == panel_ &&
                      (process_menu_ || (mouse_in_ && Contains(list_rect_, mouse_.x, mouse_.y)));
    if (hold) {
        for (int i = 0; i < shown_count_; ++i) {
            RefreshHeldEntry(shown_[static_cast<size_t>(i)], entries, std::min(count, sampler::kMaxTopProcesses));
        }
    } else {
        shown_count_ = 0;
        list_rect_ = {};
        if (!valid || (count == 0 && !idle)) {
            Text(Str().kProcessCollecting, 13.0f, false, Rect(x, y, x + w, y + 32), theme_.text3);
            return y + 32 + kSectionGap;
        }
        if (count == 0) {
            // 磁盘、网络列表：统计在跑，只是这会儿没有进程在读写
            Text(idle, 13.0f, false, Rect(x, y, x + w, y + 32), theme_.text3);
            return y + 32 + kSectionGap;
        }
        shown_count_ = std::min(count, sampler::kMaxTopProcesses);
        std::copy_n(entries, shown_count_, shown_.begin());
        shown_panel_ = panel_;
    }
    entries = shown_.data();
    count = shown_count_;
    const float list_top = y;
    // 布局像任务管理器“进程”页：展开箭头、程序图标、名称（多个进程时带个数）、占比条、数值；
    // 展开后逐个列出进程和 PID（D-030）。两列数值时不画占比条，上方加一行列名
    count = std::min(count, sampler::kMaxTopProcesses);
    const bool two = kind != ProcessValue::Percent && kind != ProcessValue::Bytes;
    const bool rates = kind == ProcessValue::ReadWrite || kind == ProcessValue::DownUp;
    const float right = x + w;
    // 两列时的第二列（显存 / 写入 / 上传）；速率比百分比宽
    const float value2_left = right - (rates ? 72 : 64);
    const float value_right = two ? value2_left - 8 : right;
    const float value_left = value_right - (rates ? 72 : two ? 56 : 68);
    const float bar_left = value_left - 10 - 56;
    const float name_right = two ? value_left - 10 : bar_left - 10;
    double max_value = 0;
    for (int i = 0; i < count; ++i) {
        max_value = std::max(max_value, entries[i].value);
    }
    auto format = [kind, rates](double v, wchar_t (&out)[24]) {
        if (kind == ProcessValue::DownUp) {
            FormatNetworkRate(v, NetworkDirection::Download, out);
        } else if (rates) {
            FormatRate(v, out);
        } else if (kind == ProcessValue::Bytes) {
            FormatBytesDetailed(v, out);
        } else {
            FormatPercentDetailed(v, out);
        }
    };
    auto format2 = [kind, rates](double v, wchar_t (&out)[24]) {
        if (kind == ProcessValue::DownUp) {
            FormatNetworkRate(v, NetworkDirection::Upload, out);
        } else if (rates) {
            FormatRate(v, out);
        } else {
            FormatBytesDetailed(v >= 0 ? std::optional<double>(v) : std::nullopt, out);
        }
    };
    if (two) {
        const wchar_t* h1 = kind == ProcessValue::ReadWrite ? Str().kColumnRead
                            : kind == ProcessValue::DownUp  ? Str().kNetDownload
                                                            : Str().kColumnGpu;
        const wchar_t* h2 = kind == ProcessValue::ReadWrite ? Str().kColumnWrite
                            : kind == ProcessValue::DownUp  ? Str().kNetUpload
                                                            : Str().kColumnVram;
        Text(h1, 11.5f, false, Rect(value_left, y, value_right, y + 18), theme_.text3, DWRITE_TEXT_ALIGNMENT_TRAILING);
        Text(h2, 11.5f, false, Rect(value2_left, y, right, y + 18), theme_.text3, DWRITE_TEXT_ALIGNMENT_TRAILING);
        y += 18;
    }

    for (int i = 0; i < count; ++i) {
        const sampler::ProcessEntry& e = entries[i];
        const bool expandable = e.count > 1;
        const bool expanded = expandable && IsExpanded(e.app_id);
        {
            const D2D1_RECT_F row = Rect(x - 6, y, right + 6, y + 32);
            if (AddPanelHit(row, HitKind::ProcessRow, static_cast<int>(e.app_id)) && hovered_ == hit_count_ - 1) {
                Fill(row, theme_.hover, 4.0f);
            }
            // 右键结束进程要用（D-048）：单进程行记下唯一的孩子；合并行记下第一个孩子和组大小
            if (hit_count_ > 0 && hits_[static_cast<size_t>(hit_count_ - 1)].index == static_cast<int>(e.app_id) &&
                e.child_count > 0) {
                Hit& row_hit = hits_[static_cast<size_t>(hit_count_ - 1)];
                row_hit.process = e.children[0];
                row_hit.group_count = e.count > 1 ? e.count : 0;
            }
            if (expandable) Icon(expanded ? kGlyphChevronDown : kGlyphChevronRight, 10.0f, Rect(x, y, x + 12, y + 32),
                 theme_.text3);
        }
        DrawProcessIcon(e.icon, e.icon_version, Rect(x + 18, y + 6, x + 38, y + 26));
        wchar_t name[80];
        if (expandable) {
            _snwprintf_s(name, _TRUNCATE, Str().kProcessNameCount, e.name, e.count);
        } else {
            wcscpy_s(name, e.name);
        }
        Text(name, 13.0f, false, Rect(x + 46, y, name_right, y + 32), theme_.text, DWRITE_TEXT_ALIGNMENT_LEADING,
             true);
        if (!two) {
            Meter(Rect(bar_left, y + 14, bar_left + 56, y + 18), max_value > 0 ? e.value / max_value : 0,
                  theme_.accent);
        }
        wchar_t value[24];
        format(e.value, value);
        Text(value, 13.0f, true, Rect(value_left, y, value_right, y + 32), theme_.text, DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (two) {
            format2(e.value2, value);
            Text(value, 13.0f, false, Rect(value2_left, y, right, y + 32), theme_.text2,
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
        y += 32;

        if (expanded) {
            for (int c = 0; c < e.child_count; ++c) {
                const sampler::ProcessChild& child = e.children[static_cast<size_t>(c)];
                if (AddPanelHit(Rect(x, y, right, y + 26), HitKind::None, static_cast<int>(e.app_id))) hits_[static_cast<size_t>(hit_count_ - 1)].process = child;
                wchar_t label[64];
                _snwprintf_s(label, _TRUNCATE, Str().kProcessChild, e.image, static_cast<unsigned long>(child.pid));
                Text(label, 12.0f, false, Rect(x + 46, y, value_left - 10, y + 26), theme_.text2,
                     DWRITE_TEXT_ALIGNMENT_LEADING, true);
                format(child.value, value);
                Text(value, 12.0f, false, Rect(value_left, y, value_right, y + 26), theme_.text2,
                     DWRITE_TEXT_ALIGNMENT_TRAILING);
                if (two) {
                    format2(child.value2, value);
                    Text(value, 12.0f, false, Rect(value2_left, y, right, y + 26), theme_.text2,
                         DWRITE_TEXT_ALIGNMENT_TRAILING);
                }
                y += 26;
            }
            if (e.count > e.child_count) {
                wchar_t more[48];
                _snwprintf_s(more, _TRUNCATE, Str().kProcessMore, e.count - e.child_count);
                Text(more, 12.0f, false, Rect(x + 46, y, right, y + 26), theme_.text3);
                y += 26;
            }
            y += 4;
        }
    }
    // 只算可视区里的部分：滚到磁贴或状态栏底下的行不该让列表锁住
    list_rect_ = Rect(x - 6, std::max(list_top, panel_area_.top), right + 6, std::min(y, panel_area_.bottom));
    return y + kSectionGap;
}

void FlyoutWindow::RefreshHeldEntry(sampler::ProcessEntry& held, const sampler::ProcessEntry* latest, int count) {
    for (int i = 0; i < count; ++i) {
        const sampler::ProcessEntry& now = latest[i];
        if (now.app_id != held.app_id) {
            continue;
        }
        held.value = now.value;
        held.value2 = now.value2;
        held.icon = now.icon;
        held.icon_version = now.icon_version;
        for (int c = 0; c < held.child_count; ++c) {
            sampler::ProcessChild& child = held.children[static_cast<size_t>(c)];
            for (int k = 0; k < now.child_count; ++k) {
                const sampler::ProcessChild& fresh = now.children[static_cast<size_t>(k)];
                if (fresh.pid == child.pid && fresh.create_time == child.create_time) {
                    child.value = fresh.value;
                    child.value2 = fresh.value2;
                    break;
                }
            }
        }
        return;
    }
    // 掉出榜单的行保留最后一次的数值，移开鼠标后就换成新榜单
}

void FlyoutWindow::DrawProcessIcon(int slot, uint32_t version, const D2D1_RECT_F& rect) {
    ID2D1Bitmap* bitmap = nullptr;
    if (icons_ && slot >= 0 && slot < sampler::IconStore::kSlots) {
        auto& cached = icon_bitmaps_[static_cast<size_t>(slot)];
        if (!cached || icon_versions_[static_cast<size_t>(slot)] != version) {
            cached.Reset();
            uint32_t pixels[sampler::IconStore::kPixels];
            if (icons_->Read(slot, version, pixels)) {
                const D2D1_BITMAP_PROPERTIES props =
                    D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                const auto size = static_cast<UINT32>(sampler::IconStore::kSize);
                if (SUCCEEDED(target_->CreateBitmap(D2D1::SizeU(size, size), pixels, size * 4, props, &cached))) {
                    icon_versions_[static_cast<size_t>(slot)] = version;
                }
            }
        }
        bitmap = cached.Get();
    }
    if (bitmap) {
        target_->DrawBitmap(bitmap, rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        Fill(rect, theme_.track, 4.0f);
    }
}

bool FlyoutWindow::AddPanelHit(const D2D1_RECT_F& rect, HitKind kind, int index) {
    // 滚出可视区的行不接收点击，否则会盖住状态栏和磁贴的按钮
    D2D1_RECT_F clipped = rect;
    clipped.top = std::max(clipped.top, panel_area_.top);
    clipped.bottom = std::min(clipped.bottom, panel_area_.bottom);
    if (clipped.bottom <= clipped.top) {
        return false;
    }
    const int before = hit_count_;
    AddHit(clipped, kind, index);
    return hit_count_ > before;
}

bool FlyoutWindow::IsExpanded(uint32_t app_id) const {
    for (int i = 0; i < expanded_count_; ++i) {
        if (expanded_[static_cast<size_t>(i)] == app_id) {
            return true;
        }
    }
    return false;
}

void FlyoutWindow::ToggleExpanded(uint32_t app_id) {
    for (int i = 0; i < expanded_count_; ++i) {
        if (expanded_[static_cast<size_t>(i)] == app_id) {
            expanded_[static_cast<size_t>(i)] = expanded_[static_cast<size_t>(--expanded_count_)];
            return;
        }
    }
    if (expanded_count_ < static_cast<int>(expanded_.size())) {
        expanded_[static_cast<size_t>(expanded_count_++)] = app_id;
    }
}
float FlyoutWindow::DrawGpuPanel(float x, float y, float w) {
    using sampler::GpuExtraState;
    const sampler::GpuReadings& g = snapshot_.gpu;
    if (!g.present) {
        return Notice(x, y, w, Str().kGpuNotFound);
    }
    // 扩展指标为什么没有：只在能补救的情况下说（缺 DLL、驱动不支持）。
    // 既不是 AMD 也不是 NVIDIA 就安静地不显示。风扇不写进 NVIDIA 那句：D3DKMT 可能已经有转速
    if (g.extra_state == GpuExtraState::NoDll) {
        y = Notice(x, y, w, Str().kGpuNoAdlx);
    } else if (g.extra_state == GpuExtraState::Unavailable) {
        y = Notice(x, y, w, Str().kGpuAdlxUnavailable);
    } else if (g.extra_state == GpuExtraState::NoNvidiaDll) {
        y = Notice(x, y, w, Str().kGpuNoNvidia);
    } else if (g.extra_state == GpuExtraState::NvidiaUnavailable) {
        y = Notice(x, y, w, Str().kGpuNvidiaUnavailable);
    }
    // 显卡型号作为面板第一节的标题，和 CPU 面板一致
    y = SectionTitle(x, y, w, g.name);

    // Windows 接口总有的三项始终显示（第一轮还没读到时是“—”）；ADLX 的两项有值才放
    constexpr double kGiB = 1024.0 * 1024 * 1024;
    wchar_t usage[24];
    wchar_t clock[24];
    wchar_t vram[32];
    wchar_t power[24];
    wchar_t fan[24];
    FormatPercent(g.usage_percent, usage);
    wcscpy_s(clock, Str().kUnavailable);
    if (g.clock_mhz) {
        _snwprintf_s(clock, _TRUNCATE, Str().kMhzFormat, *g.clock_mhz);
    }
    wcscpy_s(vram, Str().kUnavailable);
    if (g.vram_used_bytes) {
        _snwprintf_s(vram, _TRUNCATE, Str().kVramFormat, static_cast<double>(*g.vram_used_bytes) / kGiB,
                     static_cast<double>(g.vram_total_bytes) / kGiB);
    }
    Stat stats[5];
    int n = 0;
    stats[n++] = {Str().kGpuLoad3d, usage, 0};
    stats[n++] = {Str().kGpuClock, clock, 0};
    stats[n++] = {Str().kGpuVram, vram, 0};
    if (g.board_power_watts) {
        _snwprintf_s(power, _TRUNCATE, Str().kWattFormat, *g.board_power_watts);
        stats[n++] = {Str().kGpuBoardPower, power, 0};
    }
    if (g.fan_rpm) {
        // 停转也照实写 0 RPM（用户要求，D-033）
        _snwprintf_s(fan, _TRUNCATE, Str().kRpmFormat, static_cast<double>(*g.fan_rpm));
        stats[n++] = {Str().kGpuFan, fan, 0};
    }
    y = StatsCard(x, y, w, stats, n);

    // 温度单独一节（不在上面的数值卡里重复，docs/design.md 规则 3）。只有三项、名字都短，
    // 用和上面相同的“小标题 + 数字”卡片，数字按温度着色，和 CPU 面板的 Tctl 同一规则（D-038）
    Stat temps[3];
    wchar_t temp_text[3][24];
    int t = 0;
    auto add_temp = [&](const wchar_t* label, float celsius) {
        FormatTemperature(celsius, 1, temp_text[t]);
        temps[t] = {label, temp_text[t], TemperatureTone(celsius)};
        ++t;
    };
    if (g.temperature) {
        add_temp(Str().kGpuCore, *g.temperature);
    }
    if (g.hotspot) {
        add_temp(Str().kGpuHotspotRow, *g.hotspot);
    }
    if (g.memory_temperature) {
        add_temp(Str().kGpuVramTemp, *g.memory_temperature);
    }
    if (t > 0) {
        y = SectionTitle(x, y, w, Str().kGpuTemperatures);
        y = StatsCard(x, y, w, temps, t);
    }

    y = SectionTitle(x, y, w, Str().kTopProcesses);
    return TopProcesses(x, y, w, snapshot_.top_gpu.data(), snapshot_.top_gpu_count, snapshot_.top_gpu_valid,
                        ProcessValue::PercentAndBytes, Str().kGpuNoActivity);
}

float FlyoutWindow::DrawMemoryPanel(float x, float y, float w) {
    const float card_h = kCardPadY + 20 + 8 + 8 + 8 + 40 + kCardPadY;
    const D2D1_RECT_F card = Rect(x, y, x + w, y + card_h);
    Card(card);
    const float ix = x + kCardPadX;
    const float iw = w - 2 * kCardPadX;
    float cy = y + kCardPadY;
    // “8.0 GB / 15.9 GB”：已用是主角，总量紧跟其后用次要色，不拆到右上角
    wchar_t used[24];
    wchar_t total[24];
    FormatBytes(values_.memory_used_bytes, used);
    FormatBytes(values_.memory_total_bytes, total);
    wchar_t of_total[32];
    _snwprintf_s(of_total, _TRUNCATE, L" / %s", total);
    const float used_w = TextWidth(used, 14.0f, true);
    Text(used, 14.0f, true, Rect(ix, cy, ix + used_w + 1, cy + 20), theme_.text);
    Text(of_total, 14.0f, false, Rect(ix + used_w, cy, ix + iw, cy + 20), theme_.text3);
    cy += 28;

    // 四段构成（页面列表），浮窗刚打开的一瞬间或接口失败时没有
    struct Segment {
        const wchar_t* name;
        wchar_t value[24];
        float pct;
        D2D1_COLOR_F color;
        bool ring;
    };
    D2D1_COLOR_F standby = theme_.accent;
    standby.a = 0.35f;
    Segment segs[] = {{Str().kMemInUse, {}, 0, theme_.accent, false},
                      {Str().kMemModified, {}, 0, theme_.warm, false},
                      {Str().kMemStandby, {}, 0, standby, false},
                      {Str().kMemFree, {}, 0, D2D1::ColorF(0, 0, 0, 0), true}};
    const auto& lists = snapshot_.memory_lists;
    const std::optional<double> parts[] = {
        lists ? std::optional<double>(static_cast<double>(lists->in_use)) : std::nullopt,
        lists ? std::optional<double>(static_cast<double>(lists->modified)) : std::nullopt,
        lists ? std::optional<double>(static_cast<double>(lists->standby)) : std::nullopt,
        lists ? std::optional<double>(static_cast<double>(lists->free)) : std::nullopt};
    for (size_t i = 0; i < std::size(segs); ++i) {
        FormatBytes(parts[i], segs[i].value);
        if (lists && lists->total) {
            segs[i].pct = static_cast<float>(*parts[i] / static_cast<double>(lists->total) * 100.0);
        }
    }
    const D2D1_RECT_F bar = Rect(ix, cy, ix + iw, cy + 8);
    if (lists) {
        float fractions[std::size(segs)];
        D2D1_COLOR_F colors[std::size(segs)];
        for (size_t i = 0; i < std::size(segs); ++i) {
            fractions[i] = segs[i].pct / 100.0f;
            colors[i] = segs[i].color;
        }
        StackedBar(bar, fractions, colors, static_cast<int>(std::size(segs)));
    } else {
        Fill(bar, theme_.track, 4.0f);
    }
    cy += 16;

    const float col_w = (iw - 12) / 2;
    for (int i = 0; i < 4; ++i) {
        const float lx = ix + static_cast<float>(i % 2) * (col_w + 12);
        const float ly = cy + static_cast<float>(i / 2) * 22;
        const D2D1_RECT_F swatch = Rect(lx, ly + 5, lx + 8, ly + 13);
        Fill(swatch, segs[i].color, 2.0f);
        if (segs[i].ring) {
            Outline(swatch, theme_.text3, 2.0f);
        }
        Text(segs[i].name, 12.0f, false, Rect(lx + 14, ly, lx + col_w, ly + 18), theme_.text2);
        Text(segs[i].value, 12.0f, true, Rect(lx + 14, ly, lx + col_w, ly + 18), theme_.text,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    y += card_h + kSectionGap;

    constexpr double kGiB = 1024.0 * 1024 * 1024;
    wchar_t commit[32];
    if (snapshot_.commit_bytes && snapshot_.commit_limit_bytes) {
        _snwprintf_s(commit, _TRUNCATE, Str().kCommitFormat, static_cast<double>(*snapshot_.commit_bytes) / kGiB,
                     static_cast<double>(*snapshot_.commit_limit_bytes) / kGiB);
    } else {
        wcscpy_s(commit, Str().kUnavailable);
    }
    wchar_t compressed[24];
    FormatBytes(snapshot_.compressed_bytes ? std::optional<double>(static_cast<double>(*snapshot_.compressed_bytes))
                                           : std::nullopt,
                compressed);
    // 内存速度启动时读一次；固件没给就一直没有，整格隐藏而不是常驻一个“—”
    wchar_t speed[24] = L"";
    if (snapshot_.memory_speed_mts) {
        _snwprintf_s(speed, _TRUNCATE, Str().kMemSpeedFormat, static_cast<unsigned long>(*snapshot_.memory_speed_mts));
    }
    wchar_t swap_out[32];
    FormatBytes(snapshot_.swap_used_bytes, swap_out);
    const Stat stats[] = {{Str().kMemCommitted, commit, 0},
                          {Str().kMemCompressed, compressed, 0},
                          {Str().kMemSwapUsed, swap_out, 0},
                          {Str().kMemSpeed, speed, 0}};
    const int count = snapshot_.memory_speed_mts ? 3 : 2;
    y = StatsCard(x, y, w, stats, count + 1, count, true);
    y = SectionTitle(x, y, w, Str().kTopProcesses);
    return TopProcesses(x, y, w, snapshot_.top_memory.data(), snapshot_.top_memory_count, snapshot_.top_valid,
                        ProcessValue::Bytes);
}

float FlyoutWindow::DrawDiskPanel(float x, float y, float w) {
    const sampler::StorageReadings& st = snapshot_.storage;
    if (st.count == 0) {
        return Notice(x, y, w, Str().kStorageNotFound);
    }
    // 一张卡片：上面是所有盘合计的容量条（按盘分段），下面每块盘一行，四列固定宽度，数字变化时不左右跳
    constexpr float kRowHeight = 6 + 20 + 2 + 16 + 18 + 6;
    constexpr float kSwatchIndent = 16.0f;
    const auto total = sampler::TotalStorageCapacity(st);
    std::array<std::optional<sampler::StorageCapacity>, sampler::kMaxDisks> capacity{};
    int with_capacity = 0;
    bool any_temp = false;
    for (int i = 0; i < st.count; ++i) {
        const sampler::DiskReadings& d = st.disks[static_cast<size_t>(i)];
        capacity[static_cast<size_t>(i)] = sampler::DiskCapacity(d);
        with_capacity += capacity[static_cast<size_t>(i)] ? 1 : 0;
        any_temp = any_temp || d.temperature.has_value();
    }
    // 各盘在分段条里的颜色：同一个强调色，透明度从 1 递减到 0.35，色块和条上的段一一对应
    std::array<D2D1_COLOR_F, sampler::kMaxDisks> colors{};
    for (int i = 0, k = 0; i < st.count; ++i) {
        if (capacity[static_cast<size_t>(i)]) {
            colors[static_cast<size_t>(i)] = theme_.accent;
            if (with_capacity > 1) {
                colors[static_cast<size_t>(i)].a *= 1.0f - 0.65f * static_cast<float>(k) / static_cast<float>(with_capacity - 1);
            }
            ++k;
        }
    }

    const float ix = x + kCardPadX;
    const float iw = w - 2 * kCardPadX;
    const float right = ix + iw;
    const float rows_top = total ? y + kCardPadY + 20 + 8 + 8 + 8 : y + 4;
    const float card_bottom = rows_top + static_cast<float>(st.count) * kRowHeight + 4;
    const auto row_rect = [&](int i) {
        const float top = rows_top + static_cast<float>(i) * kRowHeight;
        return Rect(x + 4, top, x + w - 4, top + kRowHeight);
    };

    // 鼠标停在哪块盘的行上，容量条和数字就换成那块盘的；行的位置这一帧就能算出来，直接拿鼠标位置比
    int hovered = -1;
    if (total && mouse_in_ && !adapter_menu_ && !end_confirm_.active && mouse_.y >= panel_area_.top &&
        mouse_.y < panel_area_.bottom) {
        for (int i = 0; i < st.count; ++i) {
            if (capacity[static_cast<size_t>(i)] && Contains(row_rect(i), mouse_.x, mouse_.y)) {
                hovered = i;
            }
        }
    }

    Card(Rect(x, y, x + w, card_bottom));
    if (total) {
        const sampler::StorageCapacity& shown = hovered >= 0 ? *capacity[static_cast<size_t>(hovered)] : *total;
        float cy = y + kCardPadY;
        // “1.1 TB / 2.7 TB”：已用是主角，总量紧跟其后用次要色（和内存面板顶部一致）
        wchar_t used[24];
        wchar_t total_text[24];
        FormatBytes(static_cast<double>(shown.used_bytes), used);
        FormatBytes(static_cast<double>(shown.total_bytes), total_text);
        wchar_t of_total[32];
        _snwprintf_s(of_total, _TRUNCATE, L" / %s", total_text);
        const float used_w = TextWidth(used, 14.0f, true);
        Text(used, 14.0f, true, Rect(ix, cy, ix + used_w + 1, cy + 20), theme_.text);
        Text(of_total, 14.0f, false, Rect(ix + used_w, cy, right, cy + 20), theme_.text3);
        cy += 28;
        const D2D1_RECT_F bar = Rect(ix, cy, right, cy + 8);
        if (hovered >= 0) {
            Meter(bar, static_cast<double>(shown.used_bytes) / static_cast<double>(shown.total_bytes), theme_.accent);
        } else {
            std::array<float, sampler::kMaxDisks> fractions{};
            std::array<D2D1_COLOR_F, sampler::kMaxDisks> segment_colors{};
            int segments = 0;
            for (int i = 0; i < st.count; ++i) {
                if (const auto& c = capacity[static_cast<size_t>(i)]) {
                    fractions[static_cast<size_t>(segments)] =
                        static_cast<float>(static_cast<double>(c->used_bytes) / static_cast<double>(total->total_bytes));
                    segment_colors[static_cast<size_t>(segments)] = colors[static_cast<size_t>(i)];
                    ++segments;
                }
            }
            StackedBar(bar, fractions.data(), segment_colors.data(), segments);
        }
    }

    // 读写速率最宽（“999.9 MB/s”），温度最窄；没有任何盘报温度时只排三列
    const float indent = total ? kSwatchIndent : 0.0f;
    const float col_area = iw - indent;
    const float col_w[] = {any_temp ? col_area * 0.28f : col_area / 3, any_temp ? col_area * 0.28f : col_area / 3,
                           any_temp ? col_area * 0.25f : col_area / 3, col_area * 0.19f};
    for (int i = 0; i < st.count; ++i) {
        const sampler::DiskReadings& d = st.disks[static_cast<size_t>(i)];
        const bool has_capacity = total && capacity[static_cast<size_t>(i)];
        const D2D1_RECT_F row = row_rect(i);
        if (i > 0 || total) {
            Fill(Rect(ix, row.top, right, row.top + 1), theme_.stroke);
        }
        if (has_capacity) {
            AddPanelHit(row, HitKind::DiskRow, i);
            if (i == hovered) {
                Fill(row, theme_.hover, 4.0f);
            }
        }
        float cy = row.top + 6;

        // 第一行：色块（容量条上对应的段）、卷（没有卷时用盘号）、型号
        if (has_capacity) {
            Fill(Rect(ix, cy + 6, ix + 8, cy + 14), colors[static_cast<size_t>(i)], 2.0f);
        }
        wchar_t title[32];
        if (d.letters[0]) {
            wcscpy_s(title, d.letters);
        } else {
            _snwprintf_s(title, _TRUNCATE, Str().kDiskUnnamed, d.number);
        }
        const float tx = ix + indent;
        const float title_w = TextWidth(title, 14.0f, true);
        Text(title, 14.0f, true, Rect(tx, cy, tx + title_w + 1, cy + 20), theme_.text);
        Text(d.model, 13.0f, false, Rect(tx + title_w + 8, cy, right, cy + 20), theme_.text2,
             DWRITE_TEXT_ALIGNMENT_LEADING, true);
        cy += 22;

        // 第二行：标签在上（次要色），数值在下（主色加粗），docs/design.md rule 9
        wchar_t read[24];
        wchar_t write[24];
        wchar_t active[16];
        wchar_t temp[16] = L"";
        FormatRate(d.read_bytes_per_sec, read);
        FormatRate(d.write_bytes_per_sec, write);
        FormatPercent(d.active_percent, active);
        D2D1_COLOR_F temp_color = theme_.text;
        if (d.temperature) {
            FormatTemperature(*d.temperature, 0, temp);
            if (TemperatureTone(*d.temperature) > 0) {
                temp_color = TemperatureColor(*d.temperature);
            }
        }
        const struct {
            const wchar_t* label;
            const wchar_t* value;
            D2D1_COLOR_F color;
        } cells[] = {{Str().kColumnRead, read, theme_.text},
                     {Str().kColumnWrite, write, theme_.text},
                     {Str().kStorageActive, active, theme_.text},
                     {Str().kStorageTemp, temp, temp_color}};
        float cx = tx;
        for (int c = 0; c < (any_temp ? 4 : 3); ++c) {
            // 只有 NVMe 盘有温度；别的盘这一格空着，不写“—”冒充读不到
            if (cells[c].value[0]) {
                Text(cells[c].label, 11.5f, false, Rect(cx, cy, cx + col_w[c], cy + 16), theme_.text3);
                Text(cells[c].value, 13.0f, true, Rect(cx, cy + 16, cx + col_w[c], cy + 34), cells[c].color);
            }
            cx += col_w[c];
        }
    }
    return IoProcesses(x, card_bottom + kSectionGap, w, true);
}
float FlyoutWindow::IoProcesses(float x, float y, float w, bool disk) {
    y = SectionTitle(x, y, w, disk ? Str().kTopProcesses : Str().kNetProcesses);
    switch (snapshot_.etw_state) {
    case sampler::EtwState::NoAccess:
        return Notice(x, y, w, Str().kEtwNoAccess);
    case sampler::EtwState::NoSlot:
        return Notice(x, y, w, Str().kEtwNoSlot);
    case sampler::EtwState::Failed:
        return Notice(x, y, w, Str().kEtwFailed);
    default:
        break;
    }
    if (disk) {
        return TopProcesses(x, y, w, snapshot_.top_disk.data(), snapshot_.top_disk_count, snapshot_.top_io_valid,
                            ProcessValue::ReadWrite, Str().kIoNoDiskActivity);
    }
    return TopProcesses(x, y, w, snapshot_.top_net.data(), snapshot_.top_net_count, snapshot_.top_io_valid,
                        ProcessValue::DownUp, Str().kIoNoNetActivity);
}

float FlyoutWindow::DrawNetworkPanel(float x, float y, float w) {
    const auto& s = snapshot_;
    const bool chosen = s.net_selected >= 0;
    const sampler::NetAdapter* adapter = chosen ? &s.net_list[static_cast<size_t>(s.net_selected)] : nullptr;
    // 选定的网卡用不了时临时统计全部物理网卡（D-011），这里说明一句
    if (s.net_fallback == sampler::NetFallback::Missing) {
        y = Notice(x, y, w, Str().kNetFallbackMissing);
    } else if (s.net_fallback == sampler::NetFallback::Disconnected && adapter) {
        wchar_t notice[96];
        _snwprintf_s(notice, _TRUNCATE, Str().kNetFallbackDown, adapter->name);
        y = Notice(x, y, w, notice);
    }

    // 流量统计开着时，速率下面加一条分隔线和“今日 / 本月”两行
    constexpr float kTrafficRowH = 22.0f;
    const float traffic_h = s.traffic_on ? 9 + 2 * kTrafficRowH : 0.0f;
    const float card_h = kCardPadY + 28 + 8 + 38 + traffic_h + kCardPadY;
    Card(Rect(x, y, x + w, y + card_h));
    const float ix = x + kCardPadX;
    const float iw = w - 2 * kCardPadX;
    float cy = y + kCardPadY;

    // 网卡按钮：点开是网卡列表，和设置窗口“网络”页改的是同一项
    wchar_t label[64];
    wchar_t glyph = kGlyphGlobe;
    if (adapter) {
        wcscpy_s(label, adapter->name);
        glyph = AdapterGlyph(*adapter);
    } else {
        _snwprintf_s(label, _TRUNCATE, Str().kNetAdapterAuto, values_.net_adapter_count);
    }
    const float aw = std::min(TextWidth(label, 12.0f, false), iw - 60);
    const D2D1_RECT_F button = Rect(ix, cy, ix + 8 + 12 + 6 + aw + 6 + 10 + 8, cy + 28);
    adapter_button_ = button;
    const bool hovered = AddPanelHit(button, HitKind::AdapterButton, 0) && hovered_ == hit_count_ - 1;
    Fill(button, theme_.track, 4.0f);
    if (hovered || adapter_menu_) {
        Fill(button, theme_.hover, 4.0f);
    }
    Outline(button, theme_.card_stroke, 4.0f);
    Icon(glyph, 12.0f, Rect(ix + 8, cy, ix + 20, cy + 28), theme_.text);
    Text(label, 12.0f, false, Rect(ix + 26, cy, ix + 26 + aw, cy + 28), theme_.text, DWRITE_TEXT_ALIGNMENT_LEADING,
         true);
    Icon(kGlyphChevronDown, 10.0f, Rect(button.right - 18, cy, button.right - 8, cy + 28), theme_.text);
    cy += 36;

    wchar_t down[32];
    wchar_t up[32];
    FormatNetworkRate(values_.rx_bytes_per_sec, NetworkDirection::Download, down);
    FormatNetworkRate(values_.tx_bytes_per_sec, NetworkDirection::Upload, up);
    // 延迟没开时只有两列，不放一个永远是“—”的格子
    wchar_t latency[24];
    switch (s.ping_state) {
    case sampler::PingState::Ok:
        if (s.ping_ms && *s.ping_ms == 0) {
            wcscpy_s(latency, Str().kPingUnderOne);
        } else {
            _snwprintf_s(latency, _TRUNCATE, Str().kPingMsFormat, static_cast<unsigned long>(s.ping_ms.value_or(0)));
        }
        break;
    case sampler::PingState::Timeout:
        wcscpy_s(latency, Str().kPingTimeout);
        break;
    case sampler::PingState::Unreachable:
        wcscpy_s(latency, Str().kPingUnreachable);
        break;
    case sampler::PingState::Unresolved:
        wcscpy_s(latency, Str().kPingUnresolved);
        break;
    default:
        wcscpy_s(latency, Str().kUnavailable);
        break;
    }
    const wchar_t* labels[] = {Str().kNetDownload, Str().kNetUpload, Str().kNetLatency};
    const wchar_t* values[] = {down, up, latency};
    const int columns = config_.network.ping ? 3 : 2;
    const float col_w = iw / 3;
    for (int i = 0; i < columns; ++i) {
        const float cx = ix + static_cast<float>(i) * col_w;
        Text(labels[i], 11.5f, false, Rect(cx, cy, cx + col_w, cy + 16), theme_.text3);
        Text(values[i], 16.0f, true, Rect(cx, cy + 16, cx + col_w, cy + 38), theme_.text);
    }
    if (s.traffic_on) {
        cy += 38 + 4;
        Fill(Rect(ix, cy, ix + iw, cy + 1), theme_.stroke);
        cy += 5;
        // 行名占左边一窄列，下载、上传各占剩下的一半
        const float label_w = 64.0f;
        const float value_w = (iw - label_w) / 2;
        const struct {
            const wchar_t* label;
            uint64_t rx;
            uint64_t tx;
        } rows[] = {{Str().kTrafficToday, s.traffic_today_rx, s.traffic_today_tx},
                    {Str().kTrafficPeriod, s.traffic_period_rx, s.traffic_period_tx}};
        for (const auto& row : rows) {
            wchar_t bytes[24];
            wchar_t text[32];
            Text(row.label, 12.0f, false, Rect(ix, cy, ix + label_w, cy + kTrafficRowH), theme_.text2);
            FormatBytesDetailed(static_cast<double>(row.rx), bytes);
            _snwprintf_s(text, _TRUNCATE, Str().kDownloadLine, bytes);
            Text(text, 13.0f, true, Rect(ix + label_w, cy, ix + label_w + value_w, cy + kTrafficRowH), theme_.text);
            FormatBytesDetailed(static_cast<double>(row.tx), bytes);
            _snwprintf_s(text, _TRUNCATE, Str().kUploadLine, bytes);
            Text(text, 13.0f, true, Rect(ix + label_w + value_w, cy, ix + iw, cy + kTrafficRowH), theme_.text);
            cy += kTrafficRowH;
        }
    }
    y += card_h + kSectionGap;
    // 进程列表含所有网卡（不含回环）。速率和卡片是同一轮采样（D-060）
    return IoProcesses(x, y, w, false);
}

void FlyoutWindow::DrawAdapterMenu() {
    const auto& s = snapshot_;
    constexpr float kItemH = 44.0f;
    constexpr float kPad = 4.0f;
    menu_count_ = std::min(1 + s.net_list_count, static_cast<int>(menu_items_.size()));
    const float h = 2 * kPad + static_cast<float>(menu_count_) * kItemH;
    const float x = kSidePad;
    const float w = kWidth - 2 * kSidePad;
    // 默认在按钮下面；放不下就往上挪，菜单可以盖住磁贴
    const float top = std::max(8.0f, std::min(adapter_button_.bottom + 4, kHeight - 8 - h));
    menu_rect_ = Rect(x, top, x + w, top + h);
    Fill(menu_rect_, theme_.menu, 8.0f);
    Outline(menu_rect_, theme_.menu_stroke, 8.0f);

    for (int i = 0; i < menu_count_; ++i) {
        const float iy = top + kPad + static_cast<float>(i) * kItemH;
        const D2D1_RECT_F item = Rect(x + kPad, iy, x + w - kPad, iy + kItemH);
        menu_items_[static_cast<size_t>(i)] = item;
        const bool selected = i == 0 ? s.net_selected < 0 : s.net_selected == i - 1;
        if (i == menu_hovered_) {
            Fill(item, theme_.hover, 4.0f);
        }
        if (selected) {
            // Win11 下拉菜单的选中标记：左侧一根强调色短竖条
            Fill(Rect(item.left, iy + 14, item.left + 3, iy + kItemH - 14), theme_.accent, 1.5f);
        }
        const wchar_t* name = Str().kNetMenuAuto;
        wchar_t sub[80];
        wchar_t meta[32] = L"";
        wchar_t glyph = kGlyphGlobe;
        if (i == 0) {
            _snwprintf_s(sub, _TRUNCATE, Str().kNetMenuAutoSub, s.net_adapter_count);
        } else {
            const sampler::NetAdapter& a = s.net_list[static_cast<size_t>(i - 1)];
            name = a.name;
            glyph = AdapterGlyph(a);
            wcscpy_s(sub, a.description);
            if (!a.up) {
                wcscpy_s(meta, Str().kNetNotConnected);
            } else {
                FormatNetworkRate(a.rx_bytes_per_sec, NetworkDirection::Download, meta);
            }
        }
        const float meta_w = meta[0] ? TextWidth(meta, 12.0f, false) : 0.0f;
        const float text_l = item.left + 40;
        const float text_r = item.right - 12 - (meta_w > 0 ? meta_w + 12 : 0);
        Icon(glyph, 14.0f, Rect(item.left + 14, iy, item.left + 30, iy + kItemH), theme_.text);
        Text(name, 13.0f, selected, Rect(text_l, iy + 4, text_r, iy + 23), theme_.text, DWRITE_TEXT_ALIGNMENT_LEADING,
             true);
        Text(sub, 11.5f, false, Rect(text_l, iy + 23, text_r, iy + 40), theme_.text3, DWRITE_TEXT_ALIGNMENT_LEADING,
             true);
        if (meta[0]) {
            Text(meta, 12.0f, false, Rect(item.right - 12 - meta_w, iy, item.right - 12, iy + kItemH), theme_.text2);
        }
    }
}

int FlyoutWindow::MenuHitTest(float x, float y) const {
    for (int i = 0; i < menu_count_; ++i) {
        if (Contains(menu_items_[static_cast<size_t>(i)], x, y)) {
            return i;
        }
    }
    return -1;
}

void FlyoutWindow::OnMenuClick(int item) {
    adapter_menu_ = false;
    menu_hovered_ = -1;
    if (item == 0) {
        SendMessageW(notify_, msg::kSelectAdapter, 0, 0);
    } else if (item - 1 < snapshot_.net_list_count) {
        const GUID guid = snapshot_.net_list[static_cast<size_t>(item - 1)].guid;
        SendMessageW(notify_, msg::kSelectAdapter, 0, reinterpret_cast<LPARAM>(&guid));
    }
    Render();
}

void FlyoutWindow::DrawEndConfirm() {
    // 半透明遮罩 + 浮窗中央的卡片（D-048）。不用系统消息框：它会把浮窗挤到后台，
    // 触发“失焦收起”，而且弹在屏幕中间，和刚才点的行没有联系
    Fill(Rect(0, 0, kWidth, kHeight), D2D1::ColorF(0, 0, 0, theme_.dark ? 0.35f : 0.18f));
    constexpr float kCardW = 300.0f;
    constexpr float kCardH = 148.0f;
    constexpr float kPad = 16.0f;
    const float left = (kWidth - kCardW) / 2;
    const float top = (kHeight - kCardH) / 2;
    end_card_ = Rect(left, top, left + kCardW, top + kCardH);
    Fill(end_card_, theme_.menu, 10.0f);
    Outline(end_card_, theme_.menu_stroke, 10.0f);
    Text(Str().kEndProcess, 14.0f, true, Rect(left + kPad, top + 14, left + kCardW - kPad, top + 36), theme_.text);

    wchar_t body[256];
    if (end_confirm_.group) {
        _snwprintf_s(body, _TRUNCATE, Str().kEndConfirmGroup, end_confirm_.name, end_confirm_.expected);
    } else {
        _snwprintf_s(body, _TRUNCATE, Str().kEndConfirm, end_confirm_.name,
                     static_cast<unsigned long>(end_confirm_.first.pid));
    }
    Text(body, 13.0f, false, Rect(left + kPad, top + 44, left + kCardW - kPad, top + 100), theme_.text2);

    constexpr float kButtonH = 30.0f;
    const float by = top + kCardH - 14 - kButtonH;
    const float cancel_w = TextWidth(Str().kCancel, 13.0f, false) + 28.0f;
    const float end_w = TextWidth(Str().kEndButton, 13.0f, true) + 28.0f;
    end_confirm_rect_ = Rect(left + kCardW - kPad - end_w, by, left + kCardW - kPad, by + kButtonH);
    end_cancel_rect_ = Rect(end_confirm_rect_.left - 8 - cancel_w, by, end_confirm_rect_.left - 8, by + kButtonH);
    if (end_hover_ == 1) {
        Fill(end_cancel_rect_, theme_.hover, 4.0f);
    }
    Text(Str().kCancel, 13.0f, false, Rect(end_cancel_rect_.left, by + 5, end_cancel_rect_.right, by + 25),
         theme_.text, DWRITE_TEXT_ALIGNMENT_CENTER);
    Fill(end_confirm_rect_, theme_.accent, 4.0f);
    Text(Str().kEndButton, 13.0f, true, Rect(end_confirm_rect_.left, by + 5, end_confirm_rect_.right, by + 25),
         theme_.on_accent, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void FlyoutWindow::DrawToast() {
    if (!toast_[0]) {
        return;
    }
    const float text_w = TextWidth(toast_, 13.0f, false);
    const float w = std::min(kWidth - 2 * kSidePad, text_w + 32.0f);
    constexpr float kToastH = 40.0f;
    const float left = (kWidth - w) / 2;
    const float top = kHeight - kFooterHeight - 12 - kToastH;
    const D2D1_RECT_F card = Rect(left, top, left + w, top + kToastH);
    Fill(card, theme_.menu, 8.0f);
    Outline(card, theme_.menu_stroke, 8.0f);
    Text(toast_, 13.0f, false, Rect(left + 16, top + 11, left + w - 16, top + 31), theme_.text,
         DWRITE_TEXT_ALIGNMENT_CENTER, true);
}

void FlyoutWindow::ShowToast(const wchar_t* text) {
    wcscpy_s(toast_, text);
    if (visible_) {
        SetTimer(hwnd_, kTimerEndToast, kEndToastMs, nullptr);
        Render();
    }
}

void FlyoutWindow::ToggleSystemTheme() {
    // 目标取当前 Windows 模式的反面；壁纸按目标模式选，留空就不动壁纸
    const bool dark = SystemIsDarkTheme();
    const wchar_t* wallpaper = dark ? config_.theme_switch.light_wallpaper : config_.theme_switch.dark_wallpaper;
    ApplySystemThemeAsync(hwnd_, !dark, wallpaper);
}

void FlyoutWindow::BeginEndProcess() {
    if (!end_confirm_.active || end_running_) {
        return;
    }
    auto* request = new (std::nothrow) EndProcessRequest;
    if (!request) {
        return;
    }
    wcscpy_s(end_name_, end_confirm_.name);
    end_group_ = end_confirm_.group;
    request->hwnd = hwnd_;
    request->group = end_confirm_.group;
    request->first = end_confirm_.first;
    request->child_count = end_confirm_.group ? end_confirm_.child_count : 1;
    if (end_confirm_.group) {
        for (int i = 0; i < request->child_count; ++i) {
            request->children[static_cast<size_t>(i)] = end_confirm_.children[static_cast<size_t>(i)];
        }
    } else {
        request->children[0] = end_confirm_.first;
    }
    end_confirm_ = {};
    end_hover_ = 0;
    end_running_ = true;
    if (!QueueUserWorkItem(&EndProcessWorker, request, WT_EXECUTEDEFAULT)) {
        delete request;
        end_running_ = false;
        ShowToast(Str().kEndFailed);
        return;
    }
    Render();
}

float FlyoutWindow::DrawSensorsPanel(float x, float y, float w) {
    using sampler::SensorState;
    const auto& r = snapshot_.sensors;
    if (r.cpu_state != SensorState::Ok || r.board_state != SensorState::Ok) {
        y = Notice(x, y, w, SensorStatusText());
    }

    // 读不到的部分整节隐藏（AGENTS.md：降级而不崩溃，不显示假的 0）；用户隐藏的传感器不列，改过名的用别名（D-070）
    const int total = ListSensors(snapshot_, sensor_list_);
    static constexpr SensorGroup kGroups[] = {SensorGroup::Cpu, SensorGroup::Board, SensorGroup::Fans,
                                              SensorGroup::Gpu, SensorGroup::Storage};
    for (const SensorGroup group : kGroups) {
        SensorRow rows[kMaxSensorItems];
        int n = 0;
        for (int i = 0; i < total; ++i) {
            const SensorItem& item = sensor_list_[static_cast<size_t>(i)];
            if (item.group != group || !config::SensorVisible(config_.sensors, item.key)) {
                continue;
            }
            rows[n++] = {config::SensorName(config_.sensors, item.key, item.name),
                         item.value ? std::optional<double>(*item.value) : std::nullopt, item.fan};
        }
        if (n == 0) {
            continue;
        }
        const wchar_t* title = group == SensorGroup::Cpu     ? cpu_name_
                               : group == SensorGroup::Board ? Str().kSectionBoard
                               : group == SensorGroup::Fans  ? Str().kSectionFans
                               : group == SensorGroup::Gpu   ? snapshot_.gpu.name
                                                             : Str().kSectionStorage;
        y = SectionTitle(x, y, w, title);
        y = SensorCard(x, y, w, rows, n);
    }
    return y;
}

// ---------------------------------------------------------------- 区块

float FlyoutWindow::Notice(float x, float y, float w, const wchar_t* text) {
    Text(text, 12.0f, false, Rect(x, y, x + w, y + 18), theme_.text3);
    return y + 18 + kSectionGap;
}

float FlyoutWindow::SectionTitle(float x, float y, float w, const wchar_t* title) {
    y += 4;
    Text(title, 12.0f, true, Rect(x, y, x + w, y + 22), theme_.text2);
    return y + 22 + kSectionGap;
}

float FlyoutWindow::StatsCard(float x, float y, float w, const Stat* items, int count, int columns, bool first_full_row) {
    const int leading = first_full_row ? 1 : 0;
    const int rows = leading + (count - leading + columns - 1) / columns;
    const float card_h = 2 * kCardPadY + static_cast<float>(rows) * 37 + static_cast<float>(rows - 1) * 10;
    Card(Rect(x, y, x + w, y + card_h));
    const float col_w = (w - 2 * kCardPadX) / static_cast<float>(columns);
    for (int i = 0; i < count; ++i) {
        const bool full = first_full_row && i == 0;
        const int index = i - leading;
        const int column = full ? 0 : index % columns;
        const int row = full ? 0 : leading + index / columns;
        const float width = full ? w - 2 * kCardPadX : col_w;
        const float cx = x + kCardPadX + static_cast<float>(column) * col_w;
        const float cy = y + kCardPadY + static_cast<float>(row) * 47;
        const D2D1_COLOR_F color = items[i].tone == 2 ? theme_.hot : items[i].tone == 1 ? theme_.warm : theme_.text;
        Text(items[i].label, 11.5f, false, Rect(cx, cy, cx + width, cy + 16), theme_.text3);
        Text(items[i].value, 15.0f, true, Rect(cx, cy + 17, cx + width, cy + 37), color);
    }
    return y + card_h + kSectionGap;
}

float FlyoutWindow::SensorCard(float x, float y, float w, const SensorRow* rows, int count) {
    // 和上方的数值卡片同一种白底卡片，行在卡片里留出同样的内边距
    const float card_h = 2 * kCardPadY + static_cast<float>(count) * 28;
    Card(Rect(x, y, x + w, y + card_h));
    SensorRows(x + kCardPadX, y + kCardPadY, w - 2 * kCardPadX, rows, count);
    return y + card_h + kSectionGap;
}

float FlyoutWindow::SensorRows(float x, float y, float w, const SensorRow* rows, int count) {
    const float right = x + w;
    for (int i = 0; i < count; ++i) {
        const SensorRow& r = rows[i];
        wchar_t value[24];
        D2D1_COLOR_F bar_color = theme_.accent;
        D2D1_COLOR_F text_color = theme_.text;
        double fraction = 0;
        if (!r.value) {
            // 这个通道接过但这一轮读数无效
            wcscpy_s(value, Str().kUnavailable);
            text_color = theme_.text3;
        } else if (r.fan) {
            // 停转也照实写 0 RPM，不换成文字
            _snwprintf_s(value, _TRUNCATE, Str().kRpmFormat, std::max(0.0, *r.value));
            fraction = *r.value / 3000.0;
        } else {
            FormatTemperature(*r.value, 1, value);
            bar_color = TemperatureColor(*r.value);
            if (TemperatureTone(*r.value) > 0) {
                text_color = bar_color;
            }
            fraction = *r.value / 100.0;
        }
        Text(r.name, 13.0f, false, Rect(x, y, right - 76 - 12 - 96 - 12, y + 28), theme_.text,
             DWRITE_TEXT_ALIGNMENT_LEADING, true);
        Meter(Rect(right - 76 - 12 - 96, y + 12, right - 76 - 12, y + 16), fraction, bar_color);
        Text(value, 13.0f, true, Rect(right - 76, y, right, y + 28), text_color, DWRITE_TEXT_ALIGNMENT_TRAILING);
        y += 28;
    }
    return y + kSectionGap;
}

// ---------------------------------------------------------------- 原语

void FlyoutWindow::Fill(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float radius) {
    brush_->SetColor(color);
    if (radius > 0) {
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_.Get());
    } else {
        target_->FillRectangle(rect, brush_.Get());
    }
}

void FlyoutWindow::Outline(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float radius) {
    // 1 px 描边画在边框内侧，和 CSS border 一致
    const float half = 0.5f * USER_DEFAULT_SCREEN_DPI / static_cast<float>(dpi_);
    const D2D1_RECT_F inner = Rect(rect.left + half, rect.top + half, rect.right - half, rect.bottom - half);
    brush_->SetColor(color);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(inner, radius, radius), brush_.Get(),
                                  USER_DEFAULT_SCREEN_DPI / static_cast<float>(dpi_));
}

void FlyoutWindow::Card(const D2D1_RECT_F& rect) {
    Fill(rect, theme_.card, kCardRadius);
    Outline(rect, theme_.card_stroke, kCardRadius);
}

void FlyoutWindow::Text(const wchar_t* text, float size, bool semibold, const D2D1_RECT_F& rect,
                        const D2D1_COLOR_F& color, DWRITE_TEXT_ALIGNMENT align, bool ellipsis) {
    IDWriteTextFormat* format = Font(size, semibold);
    if (!format || !text) {
        return;
    }
    const auto length = static_cast<UINT32>(wcslen(text));
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text, length, format, std::max(0.0f, rect.right - rect.left),
                                         std::max(0.0f, rect.bottom - rect.top), &layout))) {
        return;
    }
    layout->SetTypography(typography_.Get(), {0, length});
    layout->SetTextAlignment(align);
    if (ellipsis) {
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> sign;
        if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(format, &sign))) {
            layout->SetTrimming(&trimming, sign.Get());
        }
    }
    brush_->SetColor(color);
    target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), brush_.Get(),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void FlyoutWindow::TextSpaced(const wchar_t* text, float size, bool semibold, const D2D1_RECT_F& rect,
                              const D2D1_COLOR_F& color, float spacing) {
    IDWriteTextFormat* format = Font(size, semibold);
    if (!format || !text) {
        return;
    }
    const auto length = static_cast<UINT32>(wcslen(text));
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text, length, format, std::max(0.0f, rect.right - rect.left),
                                         std::max(0.0f, rect.bottom - rect.top), &layout))) {
        return;
    }
    layout->SetTypography(typography_.Get(), {0, length});
    // 字距要 IDWriteTextLayout1（Windows 8+）；万一拿不到就按普通排版画
    ComPtr<IDWriteTextLayout1> spaced;
    if (SUCCEEDED(layout.As(&spaced))) {
        spaced->SetCharacterSpacing(0.0f, spacing, 0.0f, {0, length});
    }
    brush_->SetColor(color);
    target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), brush_.Get(),
                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float FlyoutWindow::TextWidth(const wchar_t* text, float size, bool semibold) {
    IDWriteTextFormat* format = Font(size, semibold);
    const auto length = static_cast<UINT32>(wcslen(text));
    ComPtr<IDWriteTextLayout> layout;
    if (!format || FAILED(dwrite_->CreateTextLayout(text, length, format, 1000.0f, 100.0f, &layout))) {
        return 0.0f;
    }
    layout->SetTypography(typography_.Get(), {0, length});
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return std::ceil(metrics.widthIncludingTrailingWhitespace);
}

void FlyoutWindow::Icon(wchar_t glyph, float size, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color) {
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(icon_family_, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, size, L"", &format))) {
        return;
    }
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    brush_->SetColor(color);
    const wchar_t text[2] = {glyph, 0};
    target_->DrawTextW(text, 1, format.Get(), rect, brush_.Get());
}

void FlyoutWindow::StackedBar(const D2D1_RECT_F& bar, const float* fractions, const D2D1_COLOR_F* colors, int count) {
    const float radius = (bar.bottom - bar.top) / 2;
    Fill(bar, theme_.track, radius);
    ComPtr<ID2D1RoundedRectangleGeometry> mask;
    d2d_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(bar, radius, radius), &mask);
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.Get()), nullptr);
    const float width = bar.right - bar.left;
    float sx = bar.left;
    for (int i = 0; i < count; ++i) {
        const float sw = width * std::clamp(fractions[i], 0.0f, 1.0f);
        // 段与段之间留 1 px 缝；太窄的段不留缝，免得消失
        Fill(Rect(sx, bar.top, sx + std::max(sw - 1.0f, std::min(sw, 1.0f)), bar.bottom), colors[i]);
        sx += sw;
    }
    target_->PopLayer();
}

void FlyoutWindow::Meter(const D2D1_RECT_F& track, double fraction, const D2D1_COLOR_F& color) {
    const float radius = (track.bottom - track.top) / 2;
    Fill(track, theme_.track, radius);
    const float w = (track.right - track.left) * static_cast<float>(std::clamp(fraction, 0.0, 1.0));
    if (w > 0) {
        Fill(Rect(track.left, track.top, track.left + std::max(w, 2 * radius), track.bottom), color, radius);
    }
}

void FlyoutWindow::Sparkline(const History<kHistoryCapacity>& history, const D2D1_RECT_F& rect, double min_span) {
    // 每个像素最多一个点：1 小时的 3600 个采样也只画 88 个点
    float points[128];
    const int width = static_cast<int>(rect.right - rect.left);
    const int count = Downsample(history, trends_->window, points, std::min(width, static_cast<int>(std::size(points))));
    float lo = 0;
    float hi = 0;
    bool any = false;
    for (int i = 0; i < count; ++i) {
        if (!std::isnan(points[i])) {
            lo = any ? std::min(lo, points[i]) : points[i];
            hi = any ? std::max(hi, points[i]) : points[i];
            any = true;
        }
    }
    if (!any || count < 2) {
        return;
    }
    const double span = std::max(static_cast<double>(hi - lo), min_span);
    const float step = (rect.right - rect.left - 2) / static_cast<float>(count - 1);

    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(d2d_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) {
        return;
    }
    // 最新的点在最右边；历史还没记满时曲线从右往左长
    bool open = false;
    for (int i = 0; i < count; ++i) {
        if (std::isnan(points[i])) {
            if (open) {
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                open = false;
            }
            continue;
        }
        const float px = rect.left + 1 + static_cast<float>(i) * step;
        const float py = rect.bottom - 1.5f -
                         static_cast<float>((points[i] - lo) / span) * (rect.bottom - rect.top - 3);
        if (!open) {
            sink->BeginFigure(D2D1::Point2F(px, py), D2D1_FIGURE_BEGIN_HOLLOW);
            open = true;
        } else {
            sink->AddLine(D2D1::Point2F(px, py));
        }
    }
    if (open) {
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }
    sink->Close();
    brush_->SetColor(theme_.accent);
    target_->DrawGeometry(path.Get(), brush_.Get(), 1.4f, round_stroke_.Get());
}

int FlyoutWindow::TemperatureTone(double celsius) const {
    return celsius >= config_.sensors.hot ? 2 : celsius >= config_.sensors.warn ? 1 : 0;
}

D2D1_COLOR_F FlyoutWindow::TemperatureColor(double celsius) const {
    // 颜色规则见 docs/subsystems/flyout.md，阈值默认值见 docs/configuration.md。
    if (celsius >= config_.sensors.hot) {
        return theme_.hot;
    }
    return celsius >= config_.sensors.warn ? theme_.warm : theme_.accent;
}

// ---------------------------------------------------------------- 交互

void FlyoutWindow::AddHit(const D2D1_RECT_F& rect, HitKind kind, int index) {
    if (hit_count_ < static_cast<int>(hits_.size())) {
        hits_[static_cast<size_t>(hit_count_++)] = {rect, kind, index};
    }
}

int FlyoutWindow::HitTest(float x, float y) const {
    for (int i = 0; i < hit_count_; ++i) {
        if (Contains(hits_[static_cast<size_t>(i)].rect, x, y)) {
            return i;
        }
    }
    return -1;
}

void FlyoutWindow::OnClick(const Hit& hit) {
    switch (hit.kind) {
    case HitKind::Tile:
        SelectPanel(static_cast<Panel>(hit.index));
        break;
    case HitKind::Theme:
        ToggleSystemTheme();
        break;
    case HitKind::Pin:
        pinned_ = !pinned_;
        Render();
        break;
    case HitKind::Settings:
        PostMessageW(notify_, msg::kOpenSettings, 0, 0);
        break;
    case HitKind::TaskManager:
        ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case HitKind::ProcessRow:
        ToggleExpanded(static_cast<uint32_t>(hit.index));
        Render();
        break;
    case HitKind::AdapterButton:
        adapter_menu_ = true;
        menu_hovered_ = -1;
        hovered_ = -1;
        Render();
        break;
    default:
        break;
    }
}

LRESULT CALLBACK FlyoutWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<FlyoutWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<FlyoutWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT FlyoutWindow::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_NCCALCSIZE:
        // WS_CAPTION 只是让 DWM 画系统背景的条件（D-049）；客户区铺满整个窗口，标题栏收掉
        return wparam ? 0 : DefWindowProcW(hwnd_, msg, wparam, lparam);
    case WM_NCHITTEST:
        // 没有非客户区可按；不处理的话顶部会被当作标题栏，固定和设置按钮点不到
        return HTCLIENT;
    case WM_CLOSE:
        // WS_CAPTION 带来了系统菜单，Alt+F4 会发 WM_CLOSE；收起而不是销毁窗口（D-049）
        Hide();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        Render(dc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case msg::kEndProcessResult:
        // 后台任务结束。浮窗若已收起就只清状态，下次打开不带旧提示（D-048）
        end_running_ = false;
        if (!visible_) {
            toast_[0] = L'\0';
            return 0;
        }
        {
            const int ended = static_cast<int>(wparam);
            const int attempted = static_cast<int>(lparam);
            wchar_t text[160];
            if (attempted <= 0) {
                wcscpy_s(text, Str().kEndFailed);
            } else if (ended >= attempted) {
                if (end_group_) {
                    _snwprintf_s(text, _TRUNCATE, Str().kEndDoneGroup, end_name_, ended);
                } else {
                    _snwprintf_s(text, _TRUNCATE, Str().kEndDone, end_name_);
                }
            } else {
                _snwprintf_s(text, _TRUNCATE, Str().kEndPartial, ended, attempted - ended);
            }
            ShowToast(text);
        }
        return 0;
    case msg::kThemeApplied:
        // 成功后系统广播会让浮窗自己换配色；失败才需要说话
        if (wparam == 0) {
            ShowToast(Str().kThemeSwitchFailed);
        }
        return 0;
    case WM_TIMER:
        if (wparam == kTimerEndToast) {
            KillTimer(hwnd_, kTimerEndToast);
            toast_[0] = L'\0';
            Render();
            return 0;
        }
        if (wparam == kTimerAnimation) {
            AdvanceAnimation();
            return 0;
        }
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    case WM_DPICHANGED:
        // 只在主显示器任务栏上弹出，DPI 变化时直接收起，下次打开按新 DPI 重新布局
        Hide();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && visible_ && !pinned_ && !process_menu_ && config_.flyout.auto_close) {
            wchar_t name[64] = L"";
            DWORD pid = 0;
            if (const auto other = reinterpret_cast<HWND>(lparam)) {
                GetClassNameW(other, name, static_cast<int>(std::size(name)));
                GetWindowThreadProcessId(other, &pid);
            }
            log::Write(L"flyout: deactivated by [%s] pid=%lu, hiding", name, pid);
            Hide();
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            // 确认卡片开着时 Esc 只取消确认，不收起浮窗；菜单同理
            if (end_confirm_.active) {
                end_confirm_ = {};
                end_hover_ = 0;
                Render();
            } else if (adapter_menu_) {
                adapter_menu_ = false;
                Render();
            } else {
                Hide();
            }
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            tracking_mouse_ = TrackMouseEvent(&tme) != FALSE;
        }
        const float scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(dpi_);
        const bool was_in_list = mouse_in_ && Contains(list_rect_, mouse_.x, mouse_.y);
        mouse_ = D2D1::Point2F(GET_X_LPARAM(lparam) * scale, GET_Y_LPARAM(lparam) * scale);
        mouse_in_ = true;
        if (end_confirm_.active) {
            const int hover = Contains(end_confirm_rect_, mouse_.x, mouse_.y)   ? 2
                              : Contains(end_cancel_rect_, mouse_.x, mouse_.y) ? 1
                                                                                : 0;
            if (hover != end_hover_) {
                end_hover_ = hover;
                Render();
            }
            return 0;
        }
        if (adapter_menu_) {
            const int item = MenuHitTest(mouse_.x, mouse_.y);
            if (item != menu_hovered_) {
                menu_hovered_ = item;
                Render();
            }
            return 0;
        }
        const int hit = HitTest(mouse_.x, mouse_.y);
        // 移出进程列表时立即重画，换上锁住期间攒下的新榜单
        if (hit != hovered_ || was_in_list != Contains(list_rect_, mouse_.x, mouse_.y)) {
            hovered_ = hit;
            Render();
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        tracking_mouse_ = false;
        menu_hovered_ = -1;
        const bool was_in_list = mouse_in_ && Contains(list_rect_, mouse_.x, mouse_.y);
        mouse_in_ = false;
        if (end_hover_ != 0) {
            end_hover_ = 0;
            Render();
        } else if (hovered_ != -1 || was_in_list) {
            hovered_ = -1;
            Render();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        const float scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(dpi_);
        const D2D1_POINT_2F point =
            D2D1::Point2F(GET_X_LPARAM(lparam) * scale, GET_Y_LPARAM(lparam) * scale);
        if (end_confirm_.active) {
            if (Contains(end_confirm_rect_, point.x, point.y)) {
                BeginEndProcess();
            } else if (Contains(end_cancel_rect_, point.x, point.y) || !Contains(end_card_, point.x, point.y)) {
                // 取消按钮和卡片外的单击都取消；点卡片正文不做任何事
                end_confirm_ = {};
                end_hover_ = 0;
                Render();
            }
            return 0;
        }
        if (adapter_menu_) {
            // 菜单外的单击只关菜单，不穿透到下面的内容
            const int item = MenuHitTest(GET_X_LPARAM(lparam) * scale, GET_Y_LPARAM(lparam) * scale);
            if (item >= 0) {
                OnMenuClick(item);
            } else {
                adapter_menu_ = false;
                Render();
            }
            return 0;
        }
        const int hit = HitTest(GET_X_LPARAM(lparam) * scale, GET_Y_LPARAM(lparam) * scale);
        if (hit >= 0) {
            OnClick(hits_[static_cast<size_t>(hit)]);
        }
        return 0;
    }
    case WM_CONTEXTMENU: {
        if (end_confirm_.active || end_running_) return 0;
        POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        POINT client = pt;
        ScreenToClient(hwnd_, &client);
        const float scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(dpi_);
        const int hit = HitTest(client.x * scale, client.y * scale);
        if (adapter_menu_ || hit < 0) return 0;
        const Hit& row = hits_[static_cast<size_t>(hit)];
        const auto child = row.process;
        if (!child.pid || child.pid == 4 || child.pid == GetCurrentProcessId() || !child.create_time) return 0;
        // 合并行不用先展开：右键直接结束整个程序组（D-048，和任务管理器“结束任务”一致）
        const bool group = row.group_count > 1;
        wchar_t label[64];
        if (group) {
            _snwprintf_s(label, _TRUNCATE, Str().kEndProcessGroup, row.group_count);
        } else {
            wcscpy_s(label, Str().kEndProcess);
        }
        struct MenuGuard { HMENU h; ~MenuGuard() { if (h) DestroyMenu(h); } } menu{CreatePopupMenu()};
        if (!menu.h) return 0;
        AppendMenuW(menu.h, MF_STRING, 1, label);
        process_menu_ = true;
        const auto choice = TrackPopupMenu(menu.h, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr);
        process_menu_ = false;
        // 菜单期间收到过 WM_MOUSELEAVE；鼠标还在列表上的话接着锁住，免得关掉菜单后列表马上重排
        if (POINT cursor{}; GetCursorPos(&cursor) && ScreenToClient(hwnd_, &cursor)) {
            RECT client_rect{};
            GetClientRect(hwnd_, &client_rect);
            if (PtInRect(&client_rect, cursor)) {
                mouse_ = D2D1::Point2F(cursor.x * scale, cursor.y * scale);
                mouse_in_ = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                tracking_mouse_ = TrackMouseEvent(&tme) != FALSE;
            }
        }
        if (choice != 1) return 0;
        // 在浮窗里显示二次确认，不开系统消息框（D-048）。显示名从当前画着的列表里找
        const sampler::ProcessEntry* entry = nullptr;
        for (int i = 0; i < shown_count_; ++i) {
            if (shown_[static_cast<size_t>(i)].app_id == static_cast<uint32_t>(row.index)) {
                entry = &shown_[static_cast<size_t>(i)];
                break;
            }
        }
        if (!entry) return 0;
        end_confirm_ = {};
        end_confirm_.active = true;
        end_confirm_.group = group;
        end_confirm_.expected = group ? row.group_count : 1;
        end_confirm_.first = child;
        wcscpy_s(end_confirm_.name, entry->name);
        end_confirm_.child_count = group ? std::min(entry->child_count, sampler::kMaxChildren) : 1;
        if (group) {
            for (int i = 0; i < end_confirm_.child_count; ++i) {
                end_confirm_.children[static_cast<size_t>(i)] = entry->children[static_cast<size_t>(i)];
            }
        } else {
            end_confirm_.children[0] = child;
        }
        end_hover_ = 0;
        Render();
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (end_confirm_.active) return 0;
        adapter_menu_ = false;
        scroll_ = std::clamp(scroll_ - static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA * 48.0f,
                             0.0f, max_scroll_);
        Render();
        return 0;
    case WM_NCDESTROY: {
        HWND hwnd = hwnd_;
        animating_ = false;
        StopAnimationTimer();
        ReleaseDeviceResources();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        visible_ = false;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    default:
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

}  // namespace pitot::ui
