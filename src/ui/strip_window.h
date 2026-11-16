#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <optional>

#include "common/unique_gdi.h"
#include "ui/history.h"
#include "config/config.h"
#include "ui/strip_layout.h"
#include "ui/strip_metrics.h"
#include "ui/taskbar.h"
#include "ui/theme.h"

namespace pitot::ui {

// 任务栏条显示的数值。空表示不可用。温度下标与 config::StripTempSource 一致
struct StripValues {
    std::optional<double> cpu_percent;
    std::optional<double> memory_percent;
    std::optional<double> memory_used_bytes;
    std::optional<double> memory_total_bytes;
    std::optional<double> rx_bytes_per_sec;
    std::optional<double> tx_bytes_per_sec;
    std::optional<double> disk_read_bytes_per_sec;
    std::optional<double> disk_write_bytes_per_sec;
    std::optional<double> disk_active_percent;  // 最忙的一块盘的活动时间百分比
    std::optional<double> gpu_percent;
    std::array<std::optional<double>, config::kStripTempSourceCount> temperature{};
    int net_adapter_count = 0;
    // 正在统计的网卡名；空是“自动”（悬停提示显示物理网卡数）
    wchar_t net_adapter_name[40]{};
};

// 任务栏条上被单击的部分对应哪个浮窗面板，随 msg::kStripClicked 的 wParam 发出（D-044、D-066）；
// lParam 是这条任务栏的窗口，浮窗据此弹在同一个显示器上（D-077）
enum class StripPart { Cpu, Gpu, Memory, Storage, Network, Sensors };

// 挂在任务栏里的条形窗口（D-013）。作为 Shell_TrayWnd 的子窗口，
// 用 D2D 软件渲染 + 分层窗口逐像素 alpha 绘制（D-018），让任务栏自身的背景透出来。
// 样式 A 双行堆叠、B 迷你曲线、C 单行极简（D-075）。每一项的数值来源由 [strip] items 决定（D-066）。
// 每条任务栏一个实例（D-077）。
class StripWindow {
public:
    StripWindow() = default;
    ~StripWindow();
    StripWindow(const StripWindow&) = delete;
    StripWindow& operator=(const StripWindow&) = delete;

    static bool Register(HINSTANCE instance);

    // 创建并挂进任务栏。notify 接收 msg::kStripLost 等通知，右键菜单也以它为所有者。
    bool Attach(HINSTANCE instance, const TaskbarInfo& taskbar, HWND notify);
    // 主动销毁，不会发 msg::kStripLost
    void Detach();
    // 任务栏位置、尺寸、DPI 变化，或者被别的兄弟窗口盖住时，调整到正确状态
    void Sync(const TaskbarInfo& taskbar);

    void SetValues(const StripValues& values);
    // config.ini 的外观设置：[strip] 字体、字号、间距、名称，[general] 强调色。可以在 Attach 之前调用
    void SetConfig(const config::Config& config);
    // 系统深浅色或强调色变了
    void RefreshTheme();
    // 样式 B 的迷你曲线读这里，生命周期长于任务栏条
    void SetTrends(const Trends* trends) { trends_ = trends; }
    [[nodiscard]] HWND tray() const noexcept { return taskbar_.tray; }
    // 任务栏条在屏幕上的位置；没挂上时为空
    [[nodiscard]] RECT ScreenRect() const;

    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

    void ComputeLayout(const TaskbarInfo& taskbar);
    bool EnsureFactories();
    bool BuildTextResources();
    float ContentWidth() const;  // 可点击区宽度，DIP
    // 横坐标（DIP，从窗口左边算起）落在哪一项：间距以中线为界
    StripPart PartAt(float x) const;
    bool EnsureSurface();
    void ReleaseSurface();
    void Render();
    float DrawMeter(float x, float cy, const wchar_t* label, std::optional<double> percent, float text_width,
                    const D2D1_COLOR_F& fill, const D2D1_COLOR_F& value_color);
    void DrawStat(float x, float cy, const wchar_t* label, const wchar_t* value, float width,
                  const D2D1_COLOR_F& value_color);
    void DrawLine(const wchar_t* text, IDWriteTextFormat* format, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color,
                  DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
    // 样式 B / C 的一项（D-075）
    struct CompactValue {
        wchar_t text[24];
        D2D1_COLOR_F color;  // 数值的颜色
        D2D1_COLOR_F line;   // 曲线的颜色
        const History<kHistoryCapacity>* history;
        double min_span;     // 曲线的最小纵向跨度，平稳时不至于放大成锯齿
    };
    CompactValue CompactValueOf(const StripSpan& span) const;
    void DrawSparkItem(const StripSpan& span, float x, float cy);
    void DrawMinimalItem(const StripSpan& span, float x, float cy);
    void DrawSparkline(const History<kHistoryCapacity>& history, const D2D1_RECT_F& rect, double min_span,
                       const D2D1_COLOR_F& color);
    D2D1_COLOR_F TempColor(std::optional<double> celsius) const;
    // 一行网速：箭头、数字（固定宽度的列里右对齐）、单位（固定位置左对齐），单位不随位数移动
    void DrawRate(const wchar_t* arrow, std::optional<double> bytes_per_sec, float x, float top, float height,
                  const D2D1_COLOR_F& color, config::NetUnit net_unit = config::NetUnit::Auto);
    void ShowContextMenu();
    void CreateTooltip(HINSTANCE instance);
    void UpdateTooltip();
    void ShowTooltip();
    void HideTooltip();

    struct WindowTraits {
        using pointer = HWND;
        static pointer invalid() noexcept { return nullptr; }
        static void close(pointer h) noexcept { DestroyWindow(h); }
    };

    HWND hwnd_ = nullptr;
    HWND notify_ = nullptr;
    bool detaching_ = false;

    // 系统悬停提示。所有者是隐藏主窗口：若用任务栏条作所有者，实际所有者会变成它的顶层祖先，即 explorer 的任务栏
    unique_handle_t<WindowTraits> tooltip_;
    wchar_t tooltip_text_[512]{};  // 最多 8 项，网速、磁盘各占两行
    bool tooltip_visible_ = false;

    TaskbarInfo taskbar_{};
    int x_ = 0;
    int width_ = 0;
    int height_ = 0;

    StripValues values_{};
    StripTheme theme_{};
    config::StripStyle style_{};
    config::GeneralConfig general_{};
    bool hovered_ = false;

    // 设备无关资源：字体、排版，跨挂载保留
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> label_format_;  // “CPU”“内存”
    Microsoft::WRL::ComPtr<IDWriteTextFormat> value_format_;  // “23%”
    Microsoft::WRL::ComPtr<IDWriteTextFormat> net_format_;    // 网速两行
    Microsoft::WRL::ComPtr<IDWriteTextFormat> compact_label_format_;  // 样式 C 的名称
    Microsoft::WRL::ComPtr<IDWriteTextFormat> compact_value_format_;  // 样式 B / C 的数值
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> round_stroke_;            // 样式 B 的曲线
    const Trends* trends_ = nullptr;
    Microsoft::WRL::ComPtr<IDWriteTypography> typography_;  // 等宽数字，数值变化时不左右跳
    StripLayout layout_{};
    StripMeasured measured_{};
    int warn_ = 60;
    int hot_ = 80;

    // 析构顺序与声明相反：先放开画刷和渲染目标，再删 DC，最后删位图
    unique_bitmap dib_;
    unique_memory_dc memory_dc_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    int surface_width_ = 0;
    int surface_height_ = 0;
    bool logged_first_render_ = false;
};

}  // namespace pitot::ui
