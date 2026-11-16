#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <optional>

#include "common/unique_gdi.h"
#include "common/unique_handle.h"
#include "config/config.h"
#include "sampler/cpu_info.h"
#include "sampler/icon_store.h"
#include "sampler/snapshot.h"
#include "ui/history.h"
#include "ui/sensor_list.h"
#include "ui/strip_window.h"
#include "ui/taskbar.h"
#include "ui/theme.h"

namespace pitot::ui {


// 点开任务栏条弹出的浮窗（M2）。布局取自设计稿“浮窗 · 深色 / 浅色”：
// 标题栏、3 × 2 磁贴（磁贴即导航，D-008）、可滚动的详情面板、状态栏。
// Win11 圆角 + Acrylic 背景，D2D 软件渲染；关闭时释放渲染资源和工厂，常驻内存留给任务栏条（D-024）。
class FlyoutWindow {
public:
    enum class Panel { Cpu, Gpu, Memory, Disk, Network, Sensors };

    FlyoutWindow() = default;
    ~FlyoutWindow();
    FlyoutWindow(const FlyoutWindow&) = delete;
    FlyoutWindow& operator=(const FlyoutWindow&) = delete;

    static bool Register(HINSTANCE instance);
    // notify 接收 msg::kOpenSettings，也是浮窗的所有者窗口（不在任务栏和 Alt+Tab 里出现）
    bool Create(HINSTANCE instance, HWND notify, const Trends* trends);
    void Destroy();

    // 任务栏条被单击。target 是点中部分对应的面板；[strip] click = flyout 时忽略它（D-044）
    // anchor_right 是任务栏条靠右时它的右边（屏幕坐标），浮窗和它右对齐；为空时浮窗靠任务栏左边（D-074）
    void Toggle(const TaskbarInfo& taskbar, Panel target, std::optional<int> anchor_right = std::nullopt);
    // target 为空时按 [flyout] open_panel 选面板
    void Show(const TaskbarInfo& taskbar, std::optional<Panel> target = std::nullopt,
              std::optional<int> anchor_right = std::nullopt);
    void Hide();
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    // snapshot 带每核占用和详细指标（浮窗打开时才有）
    void SetValues(const StripValues& values, const sampler::Snapshot& snapshot);
    void SetConfig(const config::Config& config);
    void RefreshTheme();
    // 进程图标来源（采样线程的图标库），生命周期长于浮窗
    void SetIconStore(const sampler::IconStore* icons) { icons_ = icons; }

private:
    enum class HitKind { None, Tile, Theme, Pin, Settings, TaskManager, ProcessRow, AdapterButton, DiskRow };
    struct Hit {
        D2D1_RECT_F rect{};
        HitKind kind = HitKind::None;
        int index = 0;
        sampler::ProcessChild process{};
        int group_count = 0;  // 合并行是程序组的进程数；单进程行和子进程行为 0
    };
    // 结束进程的确认目标：单个 PID 或同一程序的整组进程（D-048）
    struct EndTarget {
        bool active = false;
        bool group = false;
        int expected = 1;
        sampler::ProcessChild first{};
        std::array<sampler::ProcessChild, sampler::kMaxChildren> children{};
        int child_count = 0;
        wchar_t name[64]{};
    };
    struct FontEntry {
        float size = 0.0f;
        bool semibold = false;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT HandleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

    void ApplyWindowAttributes();
    bool EnsureFactories();
    bool EnsureDeviceResources();
    void ReleaseDeviceResources();
    void ReleaseFactories();
    IDWriteTextFormat* Font(float size, bool semibold);
    float Px(float dip) const;

    // 画一帧并拷到窗口。paint_dc 为空时自己 GetDC
    void Render(HDC paint_dc = nullptr);
    void DrawHeader();
    void DrawTiles(float top);
    void DrawPanel(const D2D1_RECT_F& area);
    void DrawFooter(float top);
    float DrawCpuPanel(float x, float y, float w);
    float DrawGpuPanel(float x, float y, float w);
    float DrawMemoryPanel(float x, float y, float w);
    float DrawDiskPanel(float x, float y, float w);
    float DrawNetworkPanel(float x, float y, float w);
    float DrawSensorsPanel(float x, float y, float w);
    // 网络面板的网卡下拉菜单，盖在其他内容上面；打开时只有它响应鼠标
    void DrawAdapterMenu();
    int MenuHitTest(float x, float y) const;
    void OnMenuClick(int item);
    // 结束进程的二次确认和结果提示都画在浮窗里，不开系统消息框（D-048）
    void DrawEndConfirm();
    void DrawToast();
    void BeginEndProcess();
    void ShowToast(const wchar_t* text);
    // 右上角的小太阳：在 Windows 浅色 / 深色之间切换，并换上对应的壁纸
    void ToggleSystemTheme();

    // 通用区块，返回区块底边的 y
    float Notice(float x, float y, float w, const wchar_t* text);
    float SectionTitle(float x, float y, float w, const wchar_t* title);
    struct Stat {
        const wchar_t* label;
        const wchar_t* value;
        int tone;  // 0 正常，1 偏热，2 过热
    };
    float StatsCard(float x, float y, float w, const Stat* items, int count, int columns = 3, bool first_full_row = false);
    // 进程列表的数值列：百分比（CPU）、字节数（内存）；两列：GPU 利用率 + 显存、读取 + 写入、下载 + 上传
    enum class ProcessValue { Percent, Bytes, PercentAndBytes, ReadWrite, DownUp };
    // 采样结果里的进程列表。valid 为假时显示“正在统计”；idle 不为空时，空列表显示这句（统计在跑但没有活动）
    float TopProcesses(float x, float y, float w, const sampler::ProcessEntry* entries, int count, bool valid,
                       ProcessValue kind, const wchar_t* idle = nullptr);
    // 锁住的行：从新榜单里取同一程序（和同一 PID）的数值，顺序不动
    static void RefreshHeldEntry(sampler::ProcessEntry& held, const sampler::ProcessEntry* latest, int count);
    // 存储 / 网络面板底部的进程列表（ETW），会话不可用时显示原因
    float IoProcesses(float x, float y, float w, bool disk);
    float DrawCoreGrid(float x, float y, float w);
    void DrawLoadRing(float cx, float cy, float diameter, float load);
    void DrawProcessIcon(int slot, uint32_t version, const D2D1_RECT_F& rect);
    // 面板里的可点击区域：裁到面板可视范围内，完全滚出去时不登记，返回是否登记了
    bool AddPanelHit(const D2D1_RECT_F& rect, HitKind kind, int index);
    bool IsExpanded(uint32_t app_id) const;
    void ToggleExpanded(uint32_t app_id);
    // 状态栏和面板提示用的传感器状态说明
    const wchar_t* SensorStatusText() const;
    struct SensorRow {
        const wchar_t* name;
        std::optional<double> value;  // 为空显示“—”
        bool fan;                     // 风扇转速；否则是温度
    };
    float SensorRows(float x, float y, float w, const SensorRow* rows, int count);
    // 同上，外面套一张白底卡片，和数值卡片一致（传感器面板各分节，D-038）
    float SensorCard(float x, float y, float w, const SensorRow* rows, int count);

    // 绘制原语，坐标都是 DIP
    void Fill(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float radius = 0.0f);
    void Outline(const D2D1_RECT_F& rect, const D2D1_COLOR_F& color, float radius);
    void Card(const D2D1_RECT_F& rect);
    void Text(const wchar_t* text, float size, bool semibold, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color,
              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool ellipsis = false);
    // 加了字距的文字（标题栏的字标用，D-048）
    void TextSpaced(const wchar_t* text, float size, bool semibold, const D2D1_RECT_F& rect,
                    const D2D1_COLOR_F& color, float spacing);
    void Icon(wchar_t glyph, float size, const D2D1_RECT_F& rect, const D2D1_COLOR_F& color);
    float TextWidth(const wchar_t* text, float size, bool semibold);
    void Meter(const D2D1_RECT_F& track, double fraction, const D2D1_COLOR_F& color);
    // 分段进度条（内存构成、各盘容量），fractions 是各段占整条的比例
    void StackedBar(const D2D1_RECT_F& bar, const float* fractions, const D2D1_COLOR_F* colors, int count);
    // 最近 trends_->window 个采样压成一条曲线，点数按矩形宽度（D-076）
    void Sparkline(const History<kHistoryCapacity>& history, const D2D1_RECT_F& rect, double min_span);
    D2D1_COLOR_F TemperatureColor(double celsius) const;
    // 0 正常，1 偏热，2 过热（[sensors] warn / hot）
    int TemperatureTone(double celsius) const;

    void AddHit(const D2D1_RECT_F& rect, HitKind kind, int index = 0);
    int HitTest(float x, float y) const;
    void OnClick(const Hit& hit);
    void SelectPanel(Panel panel);
    // [flyout] tiles（D-072）：这个面板的磁贴开着没有；关着时换成第一个开着的
    bool TileShown(Panel panel) const;
    Panel ShownPanel(Panel wanted) const;
    // 磁贴区的高度：开着的磁贴排几行就多高
    float TilesHeight() const;
    double AnimationProgress() const;
    // 开始 / 停止展开动画的定时器，连同定时器精度一起调整（D-089）
    bool StartAnimationTimer();
    void StopAnimationTimer();
    // 动画的一帧：推进进度并重画，动画结束时停掉定时器（D-089）
    void AdvanceAnimation();

    HWND hwnd_ = nullptr;
    HWND notify_ = nullptr;
    const Trends* trends_ = nullptr;
    bool visible_ = false;
    bool pinned_ = false;
    ULONGLONG hidden_at_ms_ = 0;
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;

    Panel panel_ = Panel::Cpu;
    float scroll_ = 0.0f;
    float max_scroll_ = 0.0f;
    LARGE_INTEGER anim_start_{};
    bool animating_ = false;
    int anim_frames_ = 0;
    // 动画期间把系统定时器精度提到 1 ms，停止动画时还原（D-089）
    bool timer_resolution_raised_ = false;
    // 打开浮窗各阶段的耗时（毫秒），动画结束时写一行日志，排查“打开后要等一会儿”（D-089）
    struct OpenTiming {
        LARGE_INTEGER start{};
        LARGE_INTEGER last_frame{};
        double setup_ms = 0.0;        // 开始到窗口显示之前：工厂、窗口属性、定位
        double show_ms = 0.0;         // ShowWindow 和激活
        double first_frame_ms = 0.0;  // 第一帧画完
        double longest_gap_ms = 0.0;  // 动画里两帧之间最长的间隔
        double detail_ms = -1.0;      // 第一份带详细指标的快照到达（相对打开），还没到时为负
    };
    OpenTiming open_timing_{};

    StripValues values_{};
    sampler::Snapshot snapshot_{};
    config::Config config_{};
    FlyoutTheme theme_{};
    wchar_t cpu_name_[64]{};
    // 传感器面板和传感器磁贴每帧重排一次，放成员里免得占栈
    SensorList sensor_list_{};
    sampler::CpuTopology topology_{};

    std::array<Hit, 128> hits_{};
    D2D1_RECT_F panel_area_{};
    // 进程列表里展开的程序（app_id），浮窗关闭时清空
    std::array<uint32_t, 16> expanded_{};
    int expanded_count_ = 0;
    // 当前画着的进程列表，鼠标停在上面时锁住不重排（D-044）。list_rect_ 是上一帧列表占的可视区域
    std::array<sampler::ProcessEntry, sampler::kMaxTopProcesses> shown_{};
    int shown_count_ = 0;
    Panel shown_panel_ = Panel::Cpu;
    D2D1_RECT_F list_rect_{};
    // 鼠标在浮窗里的位置（DIP），离开窗口时 mouse_in_ 为假
    D2D1_POINT_2F mouse_{};
    bool mouse_in_ = false;
    const sampler::IconStore* icons_ = nullptr;
    int hit_count_ = 0;
    int hovered_ = -1;
    // 网卡菜单：第 0 项“自动”，第 i + 1 项是 snapshot_.net_list[i]
    bool adapter_menu_ = false;
    D2D1_RECT_F adapter_button_{};
    std::array<D2D1_RECT_F, sampler::kMaxNetAdapters + 1> menu_items_{};
    D2D1_RECT_F menu_rect_{};
    int menu_count_ = 0;
    int menu_hovered_ = -1;
    bool tracking_mouse_ = false;
    bool process_menu_ = false;
    // 结束进程的二次确认（D-048）：卡片画在浮窗里，打开时只有两个按钮响应
    EndTarget end_confirm_;
    D2D1_RECT_F end_card_{};
    D2D1_RECT_F end_cancel_rect_{};
    D2D1_RECT_F end_confirm_rect_{};
    int end_hover_ = 0;  // 0 无，1 取消，2 结束进程
    bool end_running_ = false;
    wchar_t end_name_[64]{};
    bool end_group_ = false;
    // 操作结果提示，3 秒后消失
    wchar_t toast_[160]{};

    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IDWriteTypography> typography_;
    std::array<FontEntry, 16> fonts_{};
    const wchar_t* font_family_ = L"Segoe UI";
    const wchar_t* icon_family_ = L"Segoe MDL2 Assets";

    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> round_stroke_;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> logo_;  // 标题栏的单色标志（D-082）

    // 设备资源：只在浮窗显示期间存在。D2D 软件渲染到 32 位 DIB，再整块拷到窗口（D-024）。
    // 析构顺序与声明相反：先放开画刷和渲染目标，再删 DC，最后删位图
    unique_bitmap dib_;
    unique_memory_dc memory_dc_;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    std::array<Microsoft::WRL::ComPtr<ID2D1Bitmap>, sampler::IconStore::kSlots> icon_bitmaps_{};
    std::array<uint32_t, sampler::IconStore::kSlots> icon_versions_{};
    int surface_width_ = 0;
    int surface_height_ = 0;
};

}  // namespace pitot::ui
