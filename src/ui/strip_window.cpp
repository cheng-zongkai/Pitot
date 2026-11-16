#include "ui/strip_window.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <iterator>

#include "common/log.h"
#include "common/messages.h"
#include "ui/format.h"
#include "ui/strings.h"
#include "ui/strip_layout.h"
#include "ui/strip_metrics.h"

namespace pitot::ui {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kStripClass[] = L"Pitot.Strip";

// 以下尺寸都是 DIP，取自设计稿“任务栏条组件”。字号、间距、名称可在 config.ini 里调（D-022）
constexpr float kPad = kStripFramePad;  // 窗口内边距，和设置页的宽度判断用同一个数
constexpr float kButtonHalfH = 20.0f;  // 可点击区高 40
constexpr float kButtonRadius = 4.0f;
constexpr float kBarWidth = 3.0f;
static_assert(kBarWidth == kStripBarWidth);
constexpr float kBarHalfH = 13.0f;  // 竖条高 26
constexpr float kBarRadius = 1.5f;
// 行高 / 字号。设计稿：10 px 字配 13 px 行高，13 px 字配 17 px，11 px 字配 15 px
constexpr float kMeterLineRatio = 1.3f;
constexpr float kNetLineRatio = 1.36f;

float LineHeight(float size, float ratio) { return std::round(size * ratio); }

template <size_t N>
void AppendLine(wchar_t (&buf)[N], const wchar_t* line) {
    if (!line || !line[0]) {
        return;
    }
    const size_t used = wcslen(buf);
    const size_t extra = wcslen(line) + (used > 0 ? 1 : 0);
    if (used + extra >= N) {
        return;
    }
    if (used > 0) {
        wcscat_s(buf, L"\n");
    }
    wcscat_s(buf, line);
}

void FormatStripTemp(std::optional<double> celsius, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (!celsius || !std::isfinite(*celsius)) {
        wcsncpy_s(out, size, Str().kUnavailable, _TRUNCATE);
        return;
    }
    _snwprintf_s(out, size, _TRUNCATE, L"%.0f°", DisplayTemperature(*celsius));
}


constexpr UINT kCmdExit = 1;
constexpr UINT_PTR kTimerTooltip = 1;
constexpr int kTooltipGapDip = 8;

int Scale(int dip, UINT dpi) { return MulDiv(dip, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI); }

void ClassNameOf(HWND hwnd, wchar_t (&out)[64]) {
    out[0] = L'\0';
    if (hwnd) {
        GetClassNameW(hwnd, out, static_cast<int>(std::size(out)));
    }
}

struct MenuTraits {
    using pointer = HMENU;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { DestroyMenu(h); }
};

// 配置的字体不存在时退回 Segoe UI；中文由 DirectWrite 按 zh-CN 回退到微软雅黑
const wchar_t* PickFontFamily(IDWriteFactory* factory, const wchar_t* wanted) {
    ComPtr<IDWriteFontCollection> fonts;
    if (SUCCEEDED(factory->GetSystemFontCollection(&fonts, FALSE))) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (SUCCEEDED(fonts->FindFamilyName(wanted, &index, &exists)) && exists) {
            return wanted;
        }
    }
    log::Write(L"strip: font [%s] not installed, using Segoe UI", wanted);
    return L"Segoe UI";
}

HRESULT CreateFormat(IDWriteFactory* factory, const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight,
                     ComPtr<IDWriteTextFormat>& out) {
    HRESULT hr = factory->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", out.ReleaseAndGetAddressOf());
    if (SUCCEEDED(hr)) {
        out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return hr;
}

}  // namespace

StripWindow::~StripWindow() { Detach(); }

bool StripWindow::Register(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kStripClass;
    return RegisterClassExW(&wc) != 0;
}

bool StripWindow::Attach(HINSTANCE instance, const TaskbarInfo& taskbar, HWND notify) {
    Detach();
    if (!EnsureFactories()) {
        return false;
    }
    notify_ = notify;
    taskbar_ = taskbar;
    theme_ = LoadStripTheme(general_);
    ComputeLayout(taskbar);

    // 跨进程、跨完整性级别的父子关系，等价于 SetParent 到 explorer 的窗口（D-013）
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kStripClass, L"", WS_CHILD | WS_CLIPSIBLINGS, x_,
                                0, width_, height_, taskbar.tray, nullptr, instance, this);
    if (!hwnd) {
        log::Write(L"strip: CreateWindowEx(child of tray %p) failed, error=%lu", taskbar.tray, GetLastError());
        return false;
    }

    // 分层窗口要先 UpdateLayeredWindow 一次，显示出来才有内容
    Render();
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    CreateTooltip(instance);

    RECT rc{};
    GetWindowRect(hwnd_, &rc);
    log::Write(L"strip: attached hwnd=%p tray=%p screen=(%ld,%ld,%ld,%ld) dpi=%u light=%d", hwnd_, taskbar.tray,
               rc.left, rc.top, rc.right, rc.bottom, taskbar.dpi, theme_.light);
    return true;
}

void StripWindow::Detach() {
    if (hwnd_ && !IsWindow(hwnd_)) {
        log::Write(L"strip: hwnd %p vanished without WM_NCDESTROY", hwnd_);
        hwnd_ = nullptr;
        ReleaseSurface();
    }
    if (hwnd_) {
        detaching_ = true;
        DestroyWindow(hwnd_);
        detaching_ = false;
    }
}

void StripWindow::Sync(const TaskbarInfo& taskbar) {
    if (!hwnd_) {
        return;
    }
    if (!SameGeometry(taskbar, taskbar_)) {
        log::Write(L"strip: taskbar changed rect=(%ld,%ld,%ld,%ld) dpi=%u start.left=%ld", taskbar.rect.left,
                   taskbar.rect.top, taskbar.rect.right, taskbar.rect.bottom, taskbar.dpi, taskbar.start.left);
        taskbar_ = taskbar;
        ComputeLayout(taskbar);
        SetWindowPos(hwnd_, nullptr, x_, 0, width_, height_, SWP_NOZORDER | SWP_NOACTIVATE);
        Render();
    }
    if (HWND above = GetWindow(hwnd_, GW_HWNDPREV)) {
        wchar_t name[64];
        ClassNameOf(above, name);
        log::Write(L"strip: covered by sibling %p [%s], raising", above, name);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void StripWindow::SetValues(const StripValues& values) {
    values_ = values;
    Render();
    UpdateTooltip();
}

void StripWindow::CreateTooltip(HINSTANCE instance) {
    tooltip_.reset(CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, notify_, nullptr,
                                   instance, nullptr));
    if (!tooltip_) {
        log::Write(L"strip: tooltip creation failed, error=%lu", GetLastError());
        return;
    }
    UpdateTooltip();
    // 用跟踪模式自己控制显示时机和位置：默认模式会把提示弹在光标下面，
    // 任务栏条随即收到 WM_MOUSELEAVE，提示马上又被收起（父窗口在别的进程，鼠标穿不过提示窗口）
    TTTOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    tool.hwnd = notify_;
    tool.uId = reinterpret_cast<UINT_PTR>(hwnd_);
    tool.lpszText = tooltip_text_;
    if (!SendMessageW(tooltip_.get(), TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool))) {
        log::Write(L"strip: TTM_ADDTOOL failed");
    }
    // 设了最大宽度，提示文字里的 \n 才会换行
    SendMessageW(tooltip_.get(), TTM_SETMAXTIPWIDTH, 0, 2000);
}

void StripWindow::UpdateTooltip() {
    tooltip_text_[0] = L'\0';
    wchar_t line[96];
    // 顺序和任务栏上从左到右一致
    for (const config::StripItemConfig& item : style_.items) {
        if (item.source == config::StripSource::Empty) {
            continue;
        }
        // 名称留空时提示里仍显示默认名称，否则一行只剩数字看不懂
        const wchar_t* label = StripItemLabel(item)[0] ? StripItemLabel(item) : StripItemDefaultLabel(item);
        switch (item.source) {
        case config::StripSource::Cpu: {
            wchar_t cpu[16];
            FormatPercentDetailed(values_.cpu_percent, cpu);
            _snwprintf_s(line, _TRUNCATE, Str().kTooltipNamed, label, cpu);
            AppendLine(tooltip_text_, line);
            break;
        }
        case config::StripSource::Memory: {
            wchar_t memory[16];
            wchar_t used[24];
            wchar_t total[24];
            FormatPercentDetailed(values_.memory_percent, memory);
            FormatBytes(values_.memory_used_bytes, used);
            FormatBytes(values_.memory_total_bytes, total);
            _snwprintf_s(line, _TRUNCATE, Str().kTooltipMemory, label, memory, used, total);
            AppendLine(tooltip_text_, line);
            break;
        }
        case config::StripSource::Gpu: {
            wchar_t gpu[16];
            FormatPercentDetailed(values_.gpu_percent, gpu);
            _snwprintf_s(line, _TRUNCATE, Str().kTooltipNamed, label, gpu);
            AppendLine(tooltip_text_, line);
            break;
        }
        case config::StripSource::Temp: {
            wchar_t temp[16];
            const auto index = static_cast<size_t>(item.temp);
            const std::optional<double> celsius =
                index < values_.temperature.size() ? values_.temperature[index] : std::nullopt;
            if (celsius && std::isfinite(*celsius)) {
                FormatTemperature(*celsius, 1, temp);
            } else {
                wcscpy_s(temp, Str().kUnavailable);
            }
            _snwprintf_s(line, _TRUNCATE, Str().kTooltipNamed, label, temp);
            AppendLine(tooltip_text_, line);
            break;
        }
        case config::StripSource::DiskActivity: {
            wchar_t active[16];
            FormatPercentDetailed(values_.disk_active_percent, active);
            _snwprintf_s(line, _TRUNCATE, Str().kTooltipNamed, label, active);
            AppendLine(tooltip_text_, line);
            break;
        }
        case config::StripSource::Disk: {
            wchar_t read[24];
            wchar_t write[24];
            FormatRateDetailed(values_.disk_read_bytes_per_sec, read);
            FormatRateDetailed(values_.disk_write_bytes_per_sec, write);
            _snwprintf_s(line, _TRUNCATE, Str().kTooltipWrite, write);
            AppendLine(tooltip_text_, line);
            _snwprintf_s(line, _TRUNCATE, Str().kTooltipRead, read);
            AppendLine(tooltip_text_, line);
            break;
        }
        case config::StripSource::Net: {
            wchar_t rx[24];
            wchar_t tx[24];
            wchar_t scope[64];
            FormatRateDetailed(values_.rx_bytes_per_sec, rx);
            FormatRateDetailed(values_.tx_bytes_per_sec, tx);
            _snwprintf_s(line, _TRUNCATE, Str().kTooltipUp, tx);
            AppendLine(tooltip_text_, line);
            _snwprintf_s(line, _TRUNCATE, Str().kTooltipDown, rx);
            AppendLine(tooltip_text_, line);
            if (values_.net_adapter_name[0]) {
                _snwprintf_s(scope, _TRUNCATE, Str().kTooltipNetAdapter, values_.net_adapter_name);
            } else {
                _snwprintf_s(scope, _TRUNCATE, Str().kTooltipNetAuto, values_.net_adapter_count);
            }
            AppendLine(tooltip_text_, scope);
            break;
        }
        case config::StripSource::Empty:
            break;
        }
    }

    if (tooltip_ && hwnd_) {
        TTTOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = notify_;
        tool.uId = reinterpret_cast<UINT_PTR>(hwnd_);
        tool.lpszText = tooltip_text_;
        SendMessageW(tooltip_.get(), TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
        if (tooltip_visible_) {
            ShowTooltip();  // 文字变长变短后重新对齐
        }
    }
}

void StripWindow::ShowTooltip() {
    if (!tooltip_ || !hwnd_) {
        return;
    }
    TTTOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.hwnd = notify_;
    tool.uId = reinterpret_cast<UINT_PTR>(hwnd_);
    RECT strip{};
    GetWindowRect(hwnd_, &strip);
    const int x = strip.left + Scale(static_cast<int>(kPad), taskbar_.dpi);
    const int gap = Scale(kTooltipGapDip, taskbar_.dpi);

    if (!tooltip_visible_) {
        // 显示之前拿不到准确尺寸（TTM_GETBUBBLESIZE 此时返回很小的值），先在远离光标的地方显示出来再量
        SendMessageW(tooltip_.get(), TTM_TRACKPOSITION, 0,
                     MAKELPARAM(x, std::max(0, static_cast<int>(taskbar_.rect.top) - Scale(400, taskbar_.dpi))));
        SendMessageW(tooltip_.get(), TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
        tooltip_visible_ = true;
    }
    // 左边和任务栏条对齐，底边在任务栏上方留一点空隙，绝不盖住任务栏条（盖住光标会触发 WM_MOUSELEAVE）
    RECT tip{};
    GetWindowRect(tooltip_.get(), &tip);
    const int y = taskbar_.rect.top - (tip.bottom - tip.top) - gap;
    SendMessageW(tooltip_.get(), TTM_TRACKPOSITION, 0, MAKELPARAM(x, std::max(0, y)));
}

void StripWindow::HideTooltip() {
    KillTimer(hwnd_, kTimerTooltip);
    if (tooltip_ && tooltip_visible_) {
        TTTOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = notify_;
        tool.uId = reinterpret_cast<UINT_PTR>(hwnd_);
        SendMessageW(tooltip_.get(), TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    }
    tooltip_visible_ = false;
}

void StripWindow::SetConfig(const config::Config& config) {
    style_ = config.strip;
    general_ = config.general;
    warn_ = config.sensors.warn;
    hot_ = std::max(config.sensors.hot, warn_);
    theme_ = LoadStripTheme(general_);
    // 已经建好的文字资源要按新字体字号重建；还没建的等 Attach 时再建。
    // 窗口宽度跟着内容走（D-068），显示项或字号变了要重新摆
    if (dwrite_) {
        BuildTextResources();
        if (hwnd_) {
            ComputeLayout(taskbar_);
            SetWindowPos(hwnd_, nullptr, x_, 0, width_, height_, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    Render();
    UpdateTooltip();
}

void StripWindow::RefreshTheme() {
    const StripTheme theme = LoadStripTheme(general_);
    if (theme.light != theme_.light) {
        log::Write(L"strip: theme changed light=%d", theme.light);
    }
    theme_ = theme;
    Render();
}

void StripWindow::ComputeLayout(const TaskbarInfo& taskbar) {
    height_ = taskbar.client.bottom - taskbar.client.top;
    const float scale = static_cast<float>(taskbar.dpi) / USER_DEFAULT_SCREEN_DPI;
    // 宽度按内容走（D-068）。靠左时最宽到开始按钮前，图标靠左时开始按钮在最左边，可用宽度是 0；
    // 靠右时最宽到图标区右边，窗口贴着通知区（D-074）
    const StripSpace space = StripSpaceFor(taskbar, style_.position);
    const float want = std::min(StripWindowWidth(ContentWidth()), space.room());
    x_ = static_cast<int>(std::lround(space.WindowLeft(want) * scale));
    width_ = std::max(0, static_cast<int>(std::ceil(want * scale)));
}

bool StripWindow::EnsureFactories() {
    if (typography_ && net_format_) {
        return true;
    }
    HRESULT hr = S_OK;
    if (!typography_) {
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.ReleaseAndGetAddressOf());
        if (SUCCEEDED(hr)) {
            hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(dwrite_.ReleaseAndGetAddressOf()));
        }
        ComPtr<IDWriteTypography> typography;
        if (SUCCEEDED(hr)) {
            hr = dwrite_->CreateTypography(&typography);
        }
        if (SUCCEEDED(hr)) {
            hr = typography->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
        }
        if (SUCCEEDED(hr)) {
            const auto props = D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                           D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
            hr = d2d_->CreateStrokeStyle(props, nullptr, 0, round_stroke_.ReleaseAndGetAddressOf());
        }
        if (FAILED(hr)) {
            log::Write(L"strip: factory init failed hr=0x%08lX", hr);
            return false;
        }
        typography_ = typography;
    }
    return BuildTextResources();
}

bool StripWindow::BuildTextResources() {
    const wchar_t* family = PickFontFamily(dwrite_.Get(), style_.font);
    const auto weight = static_cast<DWRITE_FONT_WEIGHT>(style_.value_weight);
    HRESULT hr = CreateFormat(dwrite_.Get(), family, style_.label_size, DWRITE_FONT_WEIGHT_NORMAL, label_format_);
    if (SUCCEEDED(hr)) {
        hr = CreateFormat(dwrite_.Get(), family, style_.value_size, weight, value_format_);
    }
    if (SUCCEEDED(hr)) {
        hr = CreateFormat(dwrite_.Get(), family, style_.net_size, DWRITE_FONT_WEIGHT_NORMAL, net_format_);
    }
    if (SUCCEEDED(hr)) {
        hr = CreateFormat(dwrite_.Get(), family, CompactSize(style_), DWRITE_FONT_WEIGHT_NORMAL, compact_label_format_);
    }
    if (SUCCEEDED(hr)) {
        hr = CreateFormat(dwrite_.Get(), family, CompactSize(style_), weight, compact_value_format_);
    }
    if (FAILED(hr)) {
        log::Write(L"strip: text formats failed hr=0x%08lX", hr);
        label_format_.Reset();
        value_format_.Reset();
        net_format_.Reset();
        compact_label_format_.Reset();
        compact_value_format_.Reset();
        return false;
    }

    // 列宽按最宽的可能内容固定下来，数值变化时整条不伸缩。和设置页用同一套量法（D-057、D-068）
    measured_ = MeasureStripStyle(dwrite_.Get(), style_);
    layout_ = LayoutStrip(style_.items, ColumnsOf(style_, measured_));
    log::Write(L"strip: style font=[%s] sizes=%.1f/%.1f/%.1f weight=%d gaps=%.1f/%.1f/%.1f/%.1f content=%.1f DIP",
               family, style_.label_size, style_.value_size, style_.net_size, style_.value_weight, style_.padding,
               style_.item_gap, style_.net_gap, style_.bar_text_gap, kPad + ContentWidth());
    return true;
}

StripPart StripWindow::PartAt(float x) const {
    const int index = ItemAt(layout_, x - kPad);
    if (index < 0) {
        return StripPart::Cpu;
    }
    const StripSpan& span = layout_.spans[index];
    switch (span.source) {
    case config::StripSource::Memory:
        return StripPart::Memory;
    case config::StripSource::Net:
        return StripPart::Network;
    case config::StripSource::Gpu:
        return StripPart::Gpu;
    case config::StripSource::Disk:
    case config::StripSource::DiskActivity:
        return StripPart::Storage;
    case config::StripSource::Temp:
        switch (span.temp) {
        case config::StripTempSource::Cpu:
            return StripPart::Cpu;
        case config::StripTempSource::Gpu:
            return StripPart::Gpu;
        case config::StripTempSource::Storage:
            return StripPart::Storage;
        case config::StripTempSource::Board:
            return StripPart::Sensors;
        }
        break;
    case config::StripSource::Cpu:
    case config::StripSource::Empty:
        break;
    }
    return StripPart::Cpu;
}

float StripWindow::ContentWidth() const { return layout_.content_width; }

bool StripWindow::EnsureSurface() {
    if (target_ && surface_width_ == width_ && surface_height_ == height_) {
        return true;
    }
    ReleaseSurface();
    if (width_ <= 0 || height_ <= 0) {
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width_;
    bmi.bmiHeader.biHeight = -height_;  // 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_.reset(CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0));
    memory_dc_.reset(CreateCompatibleDC(nullptr));
    if (!dib_ || !memory_dc_) {
        log::Write(L"strip: DIB/DC creation failed, error=%lu", GetLastError());
        ReleaseSurface();
        return false;
    }
    SelectObject(memory_dc_.get(), dib_.get());

    // 软件渲染（D-018）：任务栏条很小，CPU 画几乎没有开销，也省掉 D3D 设备的几 MB 常驻内存
    const auto dpi = static_cast<float>(taskbar_.dpi);
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    HRESULT hr = d2d_->CreateDCRenderTarget(&props, target_.ReleaseAndGetAddressOf());
    RECT rc{0, 0, width_, height_};
    if (SUCCEEDED(hr)) {
        hr = target_->BindDC(memory_dc_.get(), &rc);
    }
    if (SUCCEEDED(hr)) {
        hr = target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), brush_.ReleaseAndGetAddressOf());
    }
    if (FAILED(hr)) {
        log::Write(L"strip: render target creation failed hr=0x%08lX", hr);
        ReleaseSurface();
        return false;
    }
    // 逐像素 alpha 下 ClearType 会出彩边，只能用灰度抗锯齿
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    surface_width_ = width_;
    surface_height_ = height_;
    return true;
}

void StripWindow::ReleaseSurface() {
    brush_.Reset();
    target_.Reset();
    memory_dc_.reset();
    dib_.reset();
    surface_width_ = 0;
    surface_height_ = 0;
}

void StripWindow::Render() {
    if (!hwnd_ || !net_format_ || !EnsureSurface()) {
        return;
    }
    const float scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(taskbar_.dpi);
    const float h = height_ * scale;
    const float cy = h / 2.0f;
    const float content_width = ContentWidth();

    target_->BeginDraw();
    // alpha 为 0 的像素在分层窗口上会被点击穿透，底色用 1/255 让整条都能接收鼠标，肉眼看不出来
    target_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f / 255.0f));

    if (hovered_) {
        brush_->SetColor(theme_.hover);
        target_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(kPad, cy - kButtonHalfH, kPad + content_width, cy + kButtonHalfH),
                              kButtonRadius, kButtonRadius),
            brush_.Get());
    }

    const float net_line = LineHeight(style_.net_size, kNetLineRatio);
    const auto temp_color = [&](std::optional<double> celsius) { return TempColor(celsius); };
    for (int i = 0; i < layout_.count; ++i) {
        const StripSpan& span = layout_.spans[i];
        const float x = kPad + span.left;
        if (style_.variant == config::StripVariant::Spark) {
            DrawSparkItem(span, x, cy);
            continue;
        }
        if (style_.variant == config::StripVariant::Minimal) {
            // 单行极简：项之间在间距正中画一个小圆点分隔（设计稿“任务栏条 · 三种样式”）
            constexpr float kSeparatorRadius = 1.2f;
            if (i > 0 && span.left - layout_.spans[i - 1].right >= 2 * kSeparatorRadius) {
                const float mid = kPad + (layout_.spans[i - 1].right + span.left) / 2.0f;
                brush_->SetColor(theme_.text2);
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(mid, cy), kSeparatorRadius, kSeparatorRadius),
                                     brush_.Get());
            }
            DrawMinimalItem(span, x, cy);
            continue;
        }
        const config::StripItemConfig& item = style_.items[span.item];
        const float text_width = measured_.text[span.item];
        const wchar_t* label = StripItemLabel(item);
        // 百分比项：带竖条时画成占用条，不带时是名称 + 数值。内存和磁盘活动到 80% / 90% 变黄 / 变红（D-069）
        const auto percent_item = [&](std::optional<double> percent) {
            const UsageTone tone = UsageToneSource(span.source) ? UsageToneOf(percent) : UsageTone::Normal;
            const D2D1_COLOR_F value_color =
                tone == UsageTone::Hot ? theme_.hot : tone == UsageTone::Warn ? theme_.warm : theme_.text;
            const D2D1_COLOR_F fill =
                tone == UsageTone::Hot ? theme_.hot : tone == UsageTone::Warn ? theme_.warm : theme_.accent;
            if (HasBar(item)) {
                DrawMeter(x, cy, label, percent, text_width, fill, value_color);
                return;
            }
            wchar_t value[16];
            FormatPercent(percent, value);
            DrawStat(x, cy, label, value, text_width, value_color);
        };
        switch (span.source) {
        case config::StripSource::Cpu:
            percent_item(values_.cpu_percent);
            break;
        case config::StripSource::Memory:
            percent_item(values_.memory_percent);
            break;
        case config::StripSource::Gpu:
            percent_item(values_.gpu_percent);
            break;
        case config::StripSource::DiskActivity:
            percent_item(values_.disk_active_percent);
            break;
        case config::StripSource::Temp: {
            const std::optional<double> celsius = values_.temperature[static_cast<size_t>(span.temp)];
            wchar_t temp[16];
            FormatStripTemp(celsius, temp, std::size(temp));
            DrawStat(x, cy, label, temp, text_width, temp_color(celsius));
            break;
        }
        case config::StripSource::Disk:
            DrawRate(Str().kDiskWriteShort, values_.disk_write_bytes_per_sec, x, cy - net_line, net_line,
                     theme_.text2);
            DrawRate(Str().kDiskReadShort, values_.disk_read_bytes_per_sec, x, cy, net_line, theme_.text);
            break;
        case config::StripSource::Net:
            DrawRate(Str().kArrowUp, values_.tx_bytes_per_sec, x, cy - net_line, net_line, theme_.text2,
                     style_.net_unit);
            DrawRate(Str().kArrowDown, values_.rx_bytes_per_sec, x, cy, net_line, theme_.text, style_.net_unit);
            break;
        case config::StripSource::Empty:
            break;
        }
    }

    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        ReleaseSurface();
        return;
    }

    POINT src{0, 0};
    SIZE size{width_, height_};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const BOOL ok = UpdateLayeredWindow(hwnd_, nullptr, nullptr, &size, memory_dc_.get(), &src, 0, &blend, ULW_ALPHA);
    if (!ok || !logged_first_render_) {
        log::Write(L"strip: render EndDraw=0x%08lX UpdateLayeredWindow=%d error=%lu size=%dx%d", hr, ok,
                   ok ? 0UL : GetLastError(), width_, height_);
        logged_first_render_ = true;
    }
}

float StripWindow::DrawMeter(float x, float cy, const wchar_t* label, std::optional<double> percent, float text_width,
                              const D2D1_COLOR_F& fill_color, const D2D1_COLOR_F& value_color) {
    const D2D1_RECT_F bar = D2D1::RectF(x, cy - kBarHalfH, x + kBarWidth, cy + kBarHalfH);
    brush_->SetColor(theme_.track);
    target_->FillRoundedRectangle(D2D1::RoundedRect(bar, kBarRadius, kBarRadius), brush_.Get());
    if (percent) {
        const float fill = 2.0f * kBarHalfH * static_cast<float>(std::clamp(*percent, 0.0, 100.0) / 100.0);
        if (fill > 0.0f) {
            brush_->SetColor(fill_color);
            target_->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(bar.left, bar.bottom - fill, bar.right, bar.bottom), kBarRadius,
                                  kBarRadius),
                brush_.Get());
        }
    }

    // 标签和数值两行作为一个整体垂直居中
    const float label_line = LineHeight(style_.label_size, kMeterLineRatio);
    const float value_line = LineHeight(style_.value_size, kMeterLineRatio);
    const float tx = x + kBarWidth + style_.bar_text_gap;
    const float top = cy - (label_line + value_line) / 2.0f;
    DrawLine(label, label_format_.Get(), D2D1::RectF(tx, top, tx + text_width, top + label_line), theme_.text2);
    wchar_t value[16];
    FormatPercent(percent, value);
    DrawLine(value, value_format_.Get(),
             D2D1::RectF(tx, top + label_line, tx + text_width, top + label_line + value_line), value_color);
    return tx + text_width;
}

void StripWindow::DrawStat(float x, float cy, const wchar_t* label, const wchar_t* value, float width,
                           const D2D1_COLOR_F& value_color) {
    const float label_line = LineHeight(style_.label_size, kMeterLineRatio);
    const float value_line = LineHeight(style_.value_size, kMeterLineRatio);
    const float top = cy - (label_line + value_line) / 2.0f;
    DrawLine(label, label_format_.Get(), D2D1::RectF(x, top, x + width, top + label_line), theme_.text2);
    DrawLine(value, value_format_.Get(), D2D1::RectF(x, top + label_line, x + width, top + label_line + value_line),
             value_color);
}

void StripWindow::DrawRate(const wchar_t* arrow, std::optional<double> bytes_per_sec, float x, float top,
                           float height, const D2D1_COLOR_F& color, config::NetUnit net_unit) {
    wchar_t number[16];
    const wchar_t* unit = L"";
    FormatNetRateParts(bytes_per_sec, net_unit, number, unit);
    const float number_left = x + measured_.rate_prefix;
    const float unit_left = number_left + measured_.rate_number + measured_.rate_space;
    DrawLine(arrow, net_format_.Get(), D2D1::RectF(x, top, number_left, top + height), color);
    DrawLine(number, net_format_.Get(),
             D2D1::RectF(number_left, top, number_left + measured_.rate_number, top + height), color,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
    DrawLine(unit, net_format_.Get(), D2D1::RectF(unit_left, top, unit_left + measured_.rate_unit, top + height), color);
}

RECT StripWindow::ScreenRect() const {
    RECT rc{};
    if (hwnd_) {
        GetWindowRect(hwnd_, &rc);
    }
    return rc;
}

D2D1_COLOR_F StripWindow::TempColor(std::optional<double> celsius) const {
    // 温度按阈值着色；没有读数时用主文字颜色
    if (!celsius || !std::isfinite(*celsius)) {
        return theme_.text;
    }
    return *celsius >= hot_ ? theme_.hot : *celsius >= warn_ ? theme_.warm : theme_.accent;
}

StripWindow::CompactValue StripWindow::CompactValueOf(const StripSpan& span) const {
    CompactValue v{};
    v.color = theme_.text;
    v.line = theme_.accent;
    v.min_span = 5.0;
    const auto percent = [&](std::optional<double> value, const History<kHistoryCapacity>* history) {
        FormatPercent(value, v.text);
        const UsageTone tone = UsageToneSource(span.source) ? UsageToneOf(value) : UsageTone::Normal;
        if (tone != UsageTone::Normal) {
            v.color = v.line = tone == UsageTone::Hot ? theme_.hot : theme_.warm;
        }
        v.history = history;
    };
    const Trends* t = trends_;
    switch (span.source) {
    case config::StripSource::Cpu:
        percent(values_.cpu_percent, t ? &t->cpu : nullptr);
        break;
    case config::StripSource::Memory:
        percent(values_.memory_percent, t ? &t->memory : nullptr);
        v.min_span = 2.0;
        break;
    case config::StripSource::Gpu:
        percent(values_.gpu_percent, t ? &t->gpu : nullptr);
        break;
    case config::StripSource::DiskActivity:
        percent(values_.disk_active_percent, t ? &t->disk_active : nullptr);
        break;
    case config::StripSource::Temp: {
        const std::optional<double> celsius = values_.temperature[static_cast<size_t>(span.temp)];
        if (celsius && std::isfinite(*celsius)) {
            _snwprintf_s(v.text, _TRUNCATE, L"%.0f°", DisplayTemperature(*celsius));
            // 和样式 A 一样，温度的数值也按阈值着色
            v.color = v.line = TempColor(celsius);
        } else {
            wcscpy_s(v.text, Str().kUnavailable);
        }
        static constexpr const History<kHistoryCapacity> Trends::*kTemps[] = {
            &Trends::temp_cpu, &Trends::temp_gpu, &Trends::temp_storage, &Trends::temp_board};
        v.history = t ? &(t->*kTemps[static_cast<size_t>(span.temp)]) : nullptr;
        v.min_span = 2.0;
        break;
    }
    case config::StripSource::Disk: {
        std::optional<double> total;
        if (values_.disk_read_bytes_per_sec || values_.disk_write_bytes_per_sec) {
            total = values_.disk_read_bytes_per_sec.value_or(0.0) + values_.disk_write_bytes_per_sec.value_or(0.0);
        }
        FormatRateShort(total, v.text);
        v.history = t ? &t->storage : nullptr;
        v.min_span = 1024.0 * 1024;
        break;
    }
    case config::StripSource::Net:
        FormatNetRateShort(values_.rx_bytes_per_sec, style_.net_unit, v.text);
        v.history = t ? &t->rx : nullptr;
        v.min_span = 16.0 * 1024;
        break;
    case config::StripSource::Empty:
        break;
    }
    return v;
}

void StripWindow::DrawSparkItem(const StripSpan& span, float x, float cy) {
    // 样式 B：第一行名称 + 数值，下面一条迷你曲线，整块垂直居中（D-075）
    constexpr float kSparkHeight = 12.0f;
    constexpr float kSparkGap = 2.0f;
    constexpr float kNameGap = 4.0f;
    const float line = LineHeight(CompactSize(style_), kMeterLineRatio);
    const float top = cy - (line + kSparkGap + kSparkHeight) / 2.0f;
    const float width = span.right - span.left;
    const config::StripItemConfig& item = style_.items[span.item];
    const CompactValue v = CompactValueOf(span);
    const wchar_t* label = span.source == config::StripSource::Net    ? Str().kArrowDown
                           : span.source == config::StripSource::Disk ? Str().kLabelDisk
                                                                      : StripItemLabel(item);
    const float label_w = measured_.label[span.item];
    DrawLine(label, label_format_.Get(), D2D1::RectF(x, top, x + label_w, top + line), theme_.text2);
    const float vx = x + label_w + (label_w > 0.0f ? kNameGap : 0.0f);
    DrawLine(v.text, compact_value_format_.Get(), D2D1::RectF(vx, top, x + width, top + line), v.color);
    if (v.history) {
        const float sy = top + line + kSparkGap;
        DrawSparkline(*v.history, D2D1::RectF(x, sy, x + width, sy + kSparkHeight), v.min_span, v.line);
    }
}

void StripWindow::DrawMinimalItem(const StripSpan& span, float x, float cy) {
    // 样式 C：一行“名称 数值”，速率项是“↓2.4M ↑180K”（D-075）
    const float line = LineHeight(CompactSize(style_), kMeterLineRatio);
    const float top = cy - line / 2.0f;
    const float right = x + (span.right - span.left);
    if (IsRate(span.source)) {
        const bool net = span.source == config::StripSource::Net;
        wchar_t first[32];
        wchar_t second[32];
        wchar_t a[24];
        wchar_t b[24];
        if (net) {
            FormatNetRateShort(values_.rx_bytes_per_sec, style_.net_unit, a);
            FormatNetRateShort(values_.tx_bytes_per_sec, style_.net_unit, b);
        } else {
            FormatRateShort(values_.disk_read_bytes_per_sec, a);
            FormatRateShort(values_.disk_write_bytes_per_sec, b);
        }
        _snwprintf_s(first, _TRUNCATE, L"%s%s", net ? Str().kArrowDown : Str().kDiskReadShort, a);
        _snwprintf_s(second, _TRUNCATE, L"%s%s", net ? Str().kArrowUp : Str().kDiskWriteShort, b);
        const float half = x + measured_.compact_rate_part + measured_.compact_space;
        DrawLine(first, compact_value_format_.Get(), D2D1::RectF(x, top, half, top + line), theme_.text);
        DrawLine(second, compact_label_format_.Get(), D2D1::RectF(half, top, right, top + line), theme_.text2);
        return;
    }
    const CompactValue v = CompactValueOf(span);
    const float label_w = measured_.label[span.item];
    DrawLine(StripItemLabel(style_.items[span.item]), compact_label_format_.Get(),
             D2D1::RectF(x, top, x + label_w, top + line), theme_.text2);
    // 数值靠右：列宽按“100%”留，短的数值靠左的话分隔点会离它很远
    const float vx = x + label_w + (label_w > 0.0f ? measured_.compact_space : 0.0f);
    DrawLine(v.text, compact_value_format_.Get(), D2D1::RectF(vx, top, right, top + line), v.color,
             DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void StripWindow::DrawSparkline(const History<kHistoryCapacity>& history, const D2D1_RECT_F& rect, double min_span,
                                const D2D1_COLOR_F& color) {
    float points[128];
    const int width = std::max(2, static_cast<int>(rect.right - rect.left));
    const size_t window = trends_ ? trends_->window : 60;
    const int count = Downsample(history, window, points, std::min(width, static_cast<int>(std::size(points))));
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
        const float py =
            rect.bottom - 1 - static_cast<float>((points[i] - lo) / span) * (rect.bottom - rect.top - 2);
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
    brush_->SetColor(color);
    target_->DrawGeometry(path.Get(), brush_.Get(), 1.4f, round_stroke_.Get());
}

void StripWindow::DrawLine(const wchar_t* text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
                           const D2D1_COLOR_F& color, DWRITE_TEXT_ALIGNMENT align) {
    const auto length = static_cast<UINT32>(wcslen(text));
    if (length == 0) {
        return;
    }
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text, length, format, std::max(0.0f, rect.right - rect.left),
                                         rect.bottom - rect.top, &layout))) {
        return;
    }
    layout->SetTypography(typography_.Get(), {0, length});
    if (align != DWRITE_TEXT_ALIGNMENT_LEADING) {
        layout->SetTextAlignment(align);
    }
    brush_->SetColor(color);
    target_->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), brush_.Get());
}

void StripWindow::ShowContextMenu() {
    unique_handle_t<MenuTraits> menu(CreatePopupMenu());
    if (!menu) {
        return;
    }
    AppendMenuW(menu.get(), MF_STRING, kCmdExit, Str().kMenuExit);
    const int edge = GetSystemMetricsForDpi(SM_CXMENUCHECK, GetDpiForWindow(hwnd_));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = edge;
    info.bmiHeader.biHeight = -edge;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    unique_bitmap exit_icon(CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0));
    unique_memory_dc icon_dc(CreateCompatibleDC(nullptr));
    if (exit_icon && icon_dc && bits) {
        auto* pixels = static_cast<DWORD*>(bits);
        std::fill_n(pixels, edge * edge, 0);
        HGDIOBJ old_bitmap = SelectObject(icon_dc.get(), exit_icon.get());
        HGDIOBJ old_pen = SelectObject(icon_dc.get(), GetStockObject(DC_PEN));
        SetDCPenColor(icon_dc.get(), RGB(255, 255, 255));
        const auto point = [edge](int x, int y) { return POINT{MulDiv(x, edge, 16), MulDiv(y, edge, 16)}; };
        const POINT door[] = {point(7, 3), point(3, 3), point(3, 13), point(7, 13)};
        const POINT arrow[] = {point(10, 4), point(14, 8), point(10, 12)};
        const POINT shaft[] = {point(6, 8), point(14, 8)};
        Polyline(icon_dc.get(), door, static_cast<int>(std::size(door)));
        Polyline(icon_dc.get(), arrow, static_cast<int>(std::size(arrow)));
        Polyline(icon_dc.get(), shaft, static_cast<int>(std::size(shaft)));
        SelectObject(icon_dc.get(), old_pen);
        SelectObject(icon_dc.get(), old_bitmap);
        GdiFlush();
        const COLORREF color = GetSysColor(COLOR_MENUTEXT);
        const DWORD opaque = 0xFF000000u | (static_cast<DWORD>(GetRValue(color)) << 16) |
                             (static_cast<DWORD>(GetGValue(color)) << 8) | GetBValue(color);
        for (int i = 0; i < edge * edge; ++i) {
            pixels[i] = (pixels[i] & 0x00FFFFFFu) ? opaque : 0;
        }
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_BITMAP;
        item.hbmpItem = exit_icon.get();
        SetMenuItemInfoW(menu.get(), kCmdExit, FALSE, &item);
    }
    POINT pt{};
    GetCursorPos(&pt);
    // 菜单所有者用本线程的隐藏主窗口；先设为前台，点菜单外面时菜单才会关闭
    SetForegroundWindow(notify_);
    const UINT cmd = static_cast<UINT>(TrackPopupMenu(
        menu.get(), TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, notify_,
        nullptr));
    PostMessageW(notify_, WM_NULL, 0, 0);
    if (cmd == kCmdExit) {
        log::Write(L"strip: exit from context menu");
        PostMessageW(notify_, WM_CLOSE, 0, 0);
    }
}

LRESULT CALLBACK StripWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<StripWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<StripWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT StripWindow::HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
        if (!hovered_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            hovered_ = true;
            // 和系统提示一样，停留一会儿再弹出
            SetTimer(hwnd_, kTimerTooltip, GetDoubleClickTime(), nullptr);
            Render();
        }
        return 0;
    case WM_MOUSELEAVE:
        hovered_ = false;
        HideTooltip();
        Render();
        return 0;
    case WM_TIMER:
        if (wparam == kTimerTooltip) {
            KillTimer(hwnd_, kTimerTooltip);
            ShowTooltip();
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        HideTooltip();
        return 0;
    case WM_LBUTTONUP: {
        const float x = static_cast<float>(GET_X_LPARAM(lparam)) * USER_DEFAULT_SCREEN_DPI /
                        static_cast<float>(taskbar_.dpi);
        PostMessageW(notify_, msg::kStripClicked, static_cast<WPARAM>(PartAt(x)),
                     reinterpret_cast<LPARAM>(taskbar_.tray));
        return 0;
    }
    case WM_CONTEXTMENU:
        // 必须自己处理：子窗口交给 DefWindowProc 的话会转给父窗口，弹出的就是 explorer 的任务栏菜单
        ShowContextMenu();
        return 0;
    case WM_DPICHANGED_AFTERPARENT:
        log::Write(L"strip: WM_DPICHANGED_AFTERPARENT dpi=%u", GetDpiForWindow(hwnd_));
        PostMessageW(notify_, msg::kTaskbarChanged, 0, 0);
        return 0;
    case WM_NCDESTROY: {
        // explorer 退出时系统只发 WM_NCDESTROY，不发 WM_DESTROY（D-013）
        HWND hwnd = hwnd_;
        log::Write(L"strip: WM_NCDESTROY hwnd=%p intentional=%d", hwnd, detaching_);
        if (!detaching_ && notify_) {
            PostMessageW(notify_, msg::kStripLost, 0, 0);
        }
        tooltip_.reset();
        tooltip_visible_ = false;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        hovered_ = false;
        ReleaseSurface();
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    default:
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

}  // namespace pitot::ui
