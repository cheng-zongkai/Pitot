#include "ui/settings_window.h"

#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <exception>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "app/autostart.h"
#include "app/pawnio_install.h"
#include "app/resource.h"
#include "common/log.h"
#include "common/names.h"
#include "common/unique_gdi.h"
#include "common/unique_handle.h"
#include "config/config.h"
#include "config/paths.h"
#include "sampler/net.h"
#include "ui/format.h"
#include "ui/logo.h"
#include "ui/sensor_share.h"
#include "ui/settings_controls.h"
#include "ui/strings.h"
#include "ui/strip_layout.h"
#include "ui/strip_metrics.h"
#include "ui/taskbar.h"
#include "ui/theme.h"

namespace pitot::ui {
namespace {

using Microsoft::WRL::ComPtr;

// 尺寸都是 DIP，取自原设计稿“设置窗口”
constexpr float kWindowWidth = 1000.0f;
constexpr float kWindowHeight = 720.0f;
constexpr float kNavWidth = 260.0f;
constexpr float kNavItemHeight = 36.0f;
constexpr float kPadX = 36.0f;
constexpr float kPadTop = 28.0f;
constexpr float kMaxCardWidth = 1000.0f;
constexpr float kCardHeight = 64.0f;
constexpr float kCardGap = 4.0f;
// 显示项那一张卡片（D-068）：一排可拖动的小卡片，下面一条宽度条
constexpr float kOrderPad = 16.0f;
constexpr float kChipH = 60.0f;
constexpr float kChipMaxW = 112.0f;
constexpr float kOrderGap = 8.0f;
constexpr float kMeterGap = 16.0f;  // 小卡片到宽度条
constexpr float kMeterH = 6.0f;
constexpr float kMeterTextH = 18.0f;
constexpr float kOrderHintH = 20.0f;  // 拒绝或被裁掉时的一行说明
constexpr float kOptionHeight = 48.0f;
constexpr float kControlHeight = 32.0f;
constexpr float kComboItemHeight = 36.0f;
constexpr int kMaxComboRows = 8;
constexpr UINT_PTR kTimerAdapters = 1;
constexpr UINT kAdapterRefreshMs = 2000;
constexpr UINT_PTR kEditSubclassId = 1;

constexpr std::array<uint32_t, 48> kAccentColors = {
    0xFFB900, 0xFF8C00, 0xF7630C, 0xCA5010, 0xDA3B01, 0xEF6950, 0xD13438, 0xFF4343,
    0xE74856, 0xE81123, 0xEA005E, 0xC30052, 0xE3008C, 0xBF0077, 0xC239B3, 0x9A0089,
    0x0078D4, 0x0063B1, 0x8E8CD8, 0x6B69D6, 0x8764B8, 0x744DA9, 0xB146C2, 0x881798,
    0x0099BC, 0x2D7D9A, 0x00B7C3, 0x038387, 0x00B294, 0x018574, 0x00CC6A, 0x10893E,
    0x7A7574, 0x5D5A58, 0x68768A, 0x515C6B, 0x567C73, 0x486860, 0x498205, 0x107C10,
    0x767676, 0x4C4A48, 0x69797E, 0x4A5459, 0x647C64, 0x525E54, 0x847545, 0x7E735F,
};

// Segoe Fluent Icons 字形
constexpr wchar_t kGlyphGeneral = 0xE713;
constexpr wchar_t kGlyphStrip = 0xE7F4;
constexpr wchar_t kGlyphFlyout = 0xE80A;
constexpr wchar_t kGlyphSensors = 0xE9D9;
constexpr wchar_t kGlyphNetwork = 0xE968;
constexpr wchar_t kGlyphInfo = 0xE946;
constexpr wchar_t kGlyphTheme = 0xE790;
constexpr wchar_t kGlyphLanguage = 0xE774;
constexpr wchar_t kGlyphRefresh = 0xE72C;
constexpr wchar_t kGlyphFont = 0xE8D2;
constexpr wchar_t kGlyphFontSize = 0xE8E9;
constexpr wchar_t kGlyphBold = 0xE8DD;
constexpr wchar_t kGlyphTag = 0xE8EC;
constexpr wchar_t kGlyphSpacing = 0xE740;
constexpr wchar_t kGlyphTiles = 0xE80A;
constexpr wchar_t kGlyphCursor = 0xE7C9;
constexpr wchar_t kGlyphList = 0xE8FD;
constexpr wchar_t kGlyphShield = 0xEA18;
constexpr wchar_t kGlyphAdapter = 0xE839;
constexpr wchar_t kGlyphWifi = 0xE701;
constexpr wchar_t kGlyphVpn = 0xE705;
constexpr wchar_t kGlyphChart = 0xE9D2;
constexpr wchar_t kGlyphCalendar = 0xE787;
constexpr wchar_t kGlyphStopwatch = 0xE916;
constexpr wchar_t kGlyphPin = 0xE707;
constexpr wchar_t kGlyphCode = 0xE943;
constexpr wchar_t kGlyphDocument = 0xE8A5;
constexpr wchar_t kGlyphChevronDown = 0xE70D;
constexpr wchar_t kGlyphPower = 0xE7E8;
constexpr wchar_t kGlyphSun = 0xE706;
constexpr wchar_t kGlyphAdd = 0xE710;
constexpr wchar_t kGlyphRemove = 0xE711;
constexpr wchar_t kGlyphBar = 0xE9E9;
constexpr wchar_t kGlyphCheck = 0xE73E;
constexpr wchar_t kGlyphBell = 0xEA8F;

D2D1_RECT_F Rect(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
bool Contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

D2D1_COLOR_F Rgba(uint32_t rgb, float a = 1.0f) {
    return D2D1::ColorF(static_cast<float>(rgb >> 16 & 0xFF) / 255.0f, static_cast<float>(rgb >> 8 & 0xFF) / 255.0f,
                        static_cast<float>(rgb & 0xFF) / 255.0f, a);
}

COLORREF ToColorRef(uint32_t rgb) { return RGB(rgb >> 16 & 0xFF, rgb >> 8 & 0xFF, rgb & 0xFF); }

struct icon_traits {
    using pointer = HICON;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { ::DestroyIcon(h); }
};
using unique_icon = unique_handle_t<icon_traits>;

// 标题栏的小图标是单色标志，颜色跟着文字色走（D-082）。先用白色画出覆盖率，再统一填成目标颜色：
// 预乘的白色里 alpha 就是覆盖率，图标要的是不预乘的颜色
unique_icon MakeLogoIcon(ID2D1Factory* factory, ID2D1Geometry* logo, int size, D2D1_COLOR_F color) {
    if (size <= 0 || !factory || !logo) {
        return {};
    }
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    unique_bitmap dib(CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0));
    unique_memory_dc dc(CreateCompatibleDC(nullptr));
    if (!dib || !dc || !bits) {
        return {};
    }
    memset(bits, 0, static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    HGDIOBJ old_bitmap = SelectObject(dc.get(), dib.get());
    {
        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f);
        ComPtr<ID2D1DCRenderTarget> target;
        ComPtr<ID2D1SolidColorBrush> white;
        const RECT bounds{0, 0, size, size};
        HRESULT hr = factory->CreateDCRenderTarget(&props, &target);
        if (SUCCEEDED(hr)) {
            hr = target->BindDC(dc.get(), &bounds);
        }
        if (SUCCEEDED(hr)) {
            hr = target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &white);
        }
        if (SUCCEEDED(hr)) {
            const auto edge = static_cast<float>(size);
            target->BeginDraw();
            target->Clear(D2D1::ColorF(0, 0, 0, 0));
            FillLogo(target.Get(), logo, white.Get(), D2D1::RectF(0, 0, edge, edge));
            hr = target->EndDraw();
        }
        if (FAILED(hr)) {
            SelectObject(dc.get(), old_bitmap);
            return {};
        }
    }
    SelectObject(dc.get(), old_bitmap);

    const auto channel = [](float v) {
        return static_cast<BYTE>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    const BYTE cr = channel(color.r);
    const BYTE cg = channel(color.g);
    const BYTE cb = channel(color.b);
    auto* pixels = static_cast<BYTE*>(bits);
    for (int i = 0; i < size * size; ++i) {
        BYTE* px = pixels + static_cast<size_t>(i) * 4;
        const BYTE coverage = px[3];
        px[0] = cb;
        px[1] = cg;
        px[2] = cr;
        px[3] = coverage;
    }

    const int mask_stride = ((size + 15) / 16) * 2;
    std::vector<BYTE> mask_bits(static_cast<size_t>(mask_stride) * static_cast<size_t>(size), 0);
    unique_bitmap mask(CreateBitmap(size, size, 1, 1, mask_bits.data()));
    if (!mask) {
        return {};
    }
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask.get();
    info.hbmColor = dib.get();
    return unique_icon(CreateIconIndirect(&info));
}

// 设计稿“设置窗口”的两套配色；强调色和深浅色判断沿用浮窗的（D-023、D-024）
struct Palette {
    bool dark = false;
    D2D1_COLOR_F win{}, content{}, stroke{}, card{}, card_stroke{}, text{}, text2{}, accent{}, on_accent{};
    D2D1_COLOR_F ctrl{}, ctrl_stroke{}, ctrl_bottom{}, nav_sel{}, off_ring{}, hover{}, menu{}, menu_stroke{};
    D2D1_COLOR_F ok_bg{}, ok{}, ok_text{}, bad_bg{}, bad{}, bad_text{};
    uint32_t edit_bg = 0;     // 输入框底色（不透明，GDI 画）
    uint32_t edit_text = 0;
};

Palette MakePalette(const config::GeneralConfig& general) {
    const FlyoutTheme f = LoadFlyoutTheme(general);
    Palette p;
    p.dark = f.dark;
    p.accent = f.accent;
    p.on_accent = f.on_accent;
    if (p.dark) {
        p.win = Rgba(0x202020);
        // Mica 上的内容层和卡片层使用 Windows 的主题资源数值（D-053）。
        p.content = Rgba(0x3A3A3A, 76.0f / 255.0f);
        p.stroke = Rgba(0xFFFFFF, 0.07f);
        p.card = Rgba(0xFFFFFF, 13.0f / 255.0f);
        p.card_stroke = Rgba(0x000000, 25.0f / 255.0f);
        p.text = Rgba(0xFFFFFF);
        p.text2 = Rgba(0xFFFFFF, 0.72f);
        p.ctrl = Rgba(0x3A3A3A);
        p.ctrl_stroke = Rgba(0xFFFFFF, 0.09f);
        p.ctrl_bottom = Rgba(0xFFFFFF, 0.54f);
        p.nav_sel = Rgba(0xFFFFFF, 0.06f);
        p.off_ring = Rgba(0xFFFFFF, 0.62f);
        p.hover = Rgba(0xFFFFFF, 0.05f);
        p.menu = Rgba(0x2C2C2C);
        p.menu_stroke = Rgba(0x000000, 0.4f);
        p.ok_bg = Rgba(0x6CCB5F, 0.14f);
        p.ok = Rgba(0x6CCB5F);
        p.ok_text = Rgba(0x9FE095);
        p.bad_bg = Rgba(0xFF99A4, 0.14f);
        p.bad = Rgba(0xFF99A4);
        p.bad_text = Rgba(0xFFC2C8);
        p.edit_bg = 0x3A3A3A;
        p.edit_text = 0xFFFFFF;
    } else {
        p.win = Rgba(0xF3F3F3);
        p.content = Rgba(0xFFFFFF, 128.0f / 255.0f);
        p.stroke = Rgba(0x000000, 0.06f);
        p.card = Rgba(0xFFFFFF, 179.0f / 255.0f);
        p.card_stroke = Rgba(0x000000, 15.0f / 255.0f);
        p.text = Rgba(0x1A1A1A);
        p.text2 = Rgba(0x000000, 0.62f);
        p.ctrl = Rgba(0xFBFBFB);
        p.ctrl_stroke = Rgba(0x000000, 0.08f);
        p.ctrl_bottom = Rgba(0x000000, 0.45f);
        p.nav_sel = Rgba(0x000000, 0.04f);
        p.off_ring = Rgba(0x000000, 0.6f);
        p.hover = Rgba(0x000000, 0.035f);
        p.menu = Rgba(0xF9F9F9);
        p.menu_stroke = Rgba(0x000000, 0.12f);
        p.ok_bg = Rgba(0x0F7B0F, 0.1f);
        p.ok = Rgba(0x0F7B0F);
        p.ok_text = Rgba(0x0E6B0E);
        p.bad_bg = Rgba(0xC42B1C, 0.1f);
        p.bad = Rgba(0xC42B1C);
        p.bad_text = Rgba(0xA4262C);
        p.edit_bg = 0xFBFBFB;
        p.edit_text = 0x1A1A1A;
    }
    return p;
}

void ApplyOpaqueFallback(Palette& p) {
    p.content = p.dark ? Rgba(0x282828) : Rgba(0xF9F9F9);
}

enum class Kind { Header, Toggle, Combo, Input, Slider, Color, Font, Radio, Status, Button, Reorder, Undo };

struct Option {
    std::wstring label;
    std::wstring sub;
    std::wstring meta;
    wchar_t glyph = 0;
};

// 页面上的一项：分节标题或一张设置卡片。取值和写入都通过回调，回调直接读写 config_
struct Item {
    Kind kind = Kind::Header;
    wchar_t glyph = 0;
    std::wstring title;
    std::wstring desc;
    // Toggle
    std::function<bool()> get_on;
    std::function<void(bool)> set_on;
    // Combo / Radio
    std::vector<Option> options;
    std::function<int()> get_index;
    std::function<void(int)> set_index;
    // Input：set_text 返回 false 表示不接受，输入框恢复原值
    std::function<std::wstring()> get_text;
    std::function<bool(const std::wstring&)> set_text;
    float input_width = 72.0f;
    std::wstring unit;
    int minimum = 0;
    int maximum = 0;
    std::function<float()> get_number;
    std::function<void(int)> set_number;
    // 输入框左边带一个勾选框（传感器列表，D-070）：get_on / set_on 管勾选，输入框管别名
    bool check = false;
    D2D1_RECT_F check_rect{};
    // Status / Button
    std::wstring value;
    bool good = true;
    std::function<void()> action;

    // 布局结果（DIP，已减去滚动量）
    D2D1_RECT_F card{};
    D2D1_RECT_F control{};
    D2D1_RECT_F edit_control{};
    D2D1_RECT_F track{};
    bool stacked = false;
    std::vector<D2D1_RECT_F> option_rects;
    HWND edit = nullptr;
};

enum class Page { General, Strip, Flyout, Sensors, Network, About };
constexpr int kPageCount = 6;

struct SettingChange {
    std::wstring section;
    std::wstring key;
    std::optional<std::wstring> original;
};

class SettingsWindow {
public:
    int Run(HINSTANCE instance);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);
    LRESULT Handle(UINT msg, WPARAM wparam, LPARAM lparam);

    void Reload();
    void ApplyThemeAndLanguage();
    void UpdateWindowIcon();
    void Build();
    void BuildGeneral();
    void BuildStrip();
    StripMeasured MeasureStrip(const config::StripStyle& style) const;
    bool StripChoiceFits(const config::StripStyle& style) const;
    void BuildFlyout();
    void BuildSensors();
    void BuildNetwork();
    void BuildAbout();
    void LoadFonts(IDWriteFontCollection* collection);
    void RefreshAdapters();
    // 传感器页：从共享内存重读传感器列表，刷新每一行的当前读数；列表变了就重排
    void RefreshSensors();
    // 阈值输入框按当前单位显示，INI 里始终写 ℃（D-071）
    float ToDisplayTemp(int celsius) const;
    int FromDisplayTemp(int value) const;
    std::vector<Option> AdapterOptions() const;
    int AdapterIndex() const;

    // 写一个键并重读配置；主进程靠目录变更通知自己生效
    void Write(const wchar_t* section, const wchar_t* key, const std::wstring& value);
    void WriteInt(const wchar_t* section, const wchar_t* key, int value) { Write(section, key, std::to_wstring(value)); }
    void WriteKey(const wchar_t* section, const wchar_t* key, const wchar_t* value);
    std::optional<std::wstring> ReadKey(const wchar_t* section, const wchar_t* key) const;
    void RecordChange(const wchar_t* section, const wchar_t* key, std::optional<std::wstring> original);
    bool WriteSensor(const wchar_t* key, bool visible, const wchar_t* alias);
    bool CanUndo() const;
    void UndoChanges();

    Item& Header(const wchar_t* title);
    Item& Toggle(wchar_t glyph, const wchar_t* title, const wchar_t* desc, std::function<bool()> get,
                 std::function<void(bool)> set);
    Item& Combo(wchar_t glyph, const wchar_t* title, const wchar_t* desc, std::vector<Option> options,
                std::function<int()> get, std::function<void(int)> set);
    // 数值下拉：当前值不在 values 里时（手改过 INI）也列出来，免得显示成空的
    Item& NumberCombo(wchar_t glyph, const wchar_t* title, const wchar_t* desc, std::vector<int> values,
                      const wchar_t* format, std::function<int()> get, std::function<void(int)> set);
    // 刷新间隔下拉：选项和配置都按毫秒，INI 里按秒写（可带小数，如 1.5）
    Item& SecondsCombo(wchar_t glyph, const wchar_t* title, const wchar_t* desc, std::vector<int> values_ms,
                       const wchar_t* key, int config::GeneralConfig::*field);
    Item& Input(wchar_t glyph, const wchar_t* title, const wchar_t* desc, float width, const wchar_t* unit,
                std::function<std::wstring()> get, std::function<bool(const std::wstring&)> set);
    // 桌面主题切换用的壁纸选择（D-065）
    Item& Wallpaper(wchar_t glyph, const wchar_t* title, const wchar_t* key, const wchar_t* path);
    Item& NumberInput(wchar_t glyph, const wchar_t* title, const wchar_t* desc, int min, int max, const wchar_t* unit,
                      std::function<float()> get, std::function<void(int)> set);

    void Layout();
    void PositionEdits();
    void DestroyEdits();
    void CommitEdit(HWND edit);
    void FocusNextItem(int current, bool backwards, bool from_edit);
    void SetSliderFromPosition(int index, float x);
    void OpenCombo(int index);
    void LayoutCombo();
    void OpenColor(int index);
    void Render(HDC dc);
    void OnClick(float x, float y);
    int HitNav(float x, float y) const;
    float Px(float dip) const { return dip * static_cast<float>(dpi_) / USER_DEFAULT_SCREEN_DPI; }
    float Dip(int px) const { return static_cast<float>(px) * USER_DEFAULT_SCREEN_DPI / static_cast<float>(dpi_); }

    bool EnsureTarget();
    void ReleaseTarget();
    IDWriteTextFormat* Font(float size, DWRITE_FONT_WEIGHT weight, bool icon = false);
    void Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0.0f);
    void Outline(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius = 0.0f);
    void Text(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight, const D2D1_RECT_F& r,
              const D2D1_COLOR_F& c, DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
    void Glyph(wchar_t glyph, float size, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    float TextWidth(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight);
    void DrawItem(const Item& item, int index);
    void DrawCombo();
    void DrawColor();
    void DrawStripOrder(const Item& item, int index);
    void DrawOrderCard(const D2D1_RECT_F& card, const config::StripItemConfig& item, bool selected, bool hover,
                       bool floating);
    void DrawAddCard(const D2D1_RECT_F& card, bool hover);
    void DrawStripMeter(const Item& item);
    int StripGapAt(float x, float y) const;
    void SetStripGap(int gap_index, float pointer_x);
    void SetStripSource(config::StripSource source);
    void SetStripTempSource(config::StripTempSource temp);
    bool SetStripLabel(const std::wstring& text);
    void AddStripItem();
    void RemoveStripItemAt(int index);
    // 检查放不放得下，放得下就写进 config.ini；放不下时 strip_note_ 写明原因，返回 false
    bool CommitStripItems(config::StripStyle next, bool rebuild = true);
    // 现在有没有一行红字：刚被拒绝，或者已经比开始按钮前的空白宽
    bool StripNoteShown() const;
    void WriteStripItems(const config::StripItemConfig (&items)[kStripItemCount]);
    bool OrderCardAt(float x, float y, int& item, int& card) const;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
    wchar_t config_path_[MAX_PATH]{};
    wchar_t log_path_[MAX_PATH]{};
    config::Config config_{};
    Palette palette_{};
    Page page_ = Page::General;
    std::array<std::vector<SettingChange>, static_cast<size_t>(Page::About)> changes_;
    std::vector<Item> items_;
    float scroll_ = 0.0f;
    float max_scroll_ = 0.0f;
    float content_height_ = 0.0f;
    int hovered_item_ = -1;  // 悬停在哪张卡片的控件上
    int hovered_option_ = -1;
    int hovered_nav_ = -1;
    int open_combo_ = -1;  // 展开的下拉框是哪一项
    int combo_first_ = 0;
    int combo_cursor_ = -1;
    D2D1_RECT_F combo_rect_{};
    std::vector<D2D1_RECT_F> combo_rects_;
    int combo_hovered_ = -1;
    int open_color_ = -1;
    D2D1_RECT_F color_rect_{};
    std::vector<D2D1_RECT_F> color_rects_;
    int color_hovered_ = -1;
    int focused_slider_ = -1;
    int focused_item_ = -1;
    int dragging_slider_ = -1;
    // 显示项卡片的拖动与选中。order_item_ 是 items_ 里那一行，-1 表示没在拖
    int order_item_ = -1;
    int order_from_ = 0;
    int order_to_ = 0;  // 拖动时被拖的卡片当前落在哪一格
    float order_press_x_ = 0;
    float order_pointer_x_ = 0;
    bool order_dragging_ = false;
    config::StripItemConfig order_base_[kStripItemCount]{};
    config::StripItemConfig order_preview_[kStripItemCount]{};
    int strip_selected_ = 0;  // 正在改的那一项
    int strip_count_ = 0;     // 现有几项；排在它们后面的是“添加”卡片
    float strip_room_ = kStripFallbackRoomDip;  // 开始按钮前能给任务栏条用的宽度
    StripLayout strip_layout_{};                // 按当前配置排好的任务栏条，宽度条照着画
    D2D1_RECT_F strip_remove_rect_{};           // 选中卡片右上角的 ×，没有时为空
    bool strip_remove_hover_ = false;
    int strip_gap_ = -1;        // 正在拖的间距，-1 表示没在拖
    int strip_gap_hover_ = -1;  // 指针下的间距
    float strip_gap_pointer_offset_ = 0.0f;
    std::wstring strip_note_;
    HWND focused_edit_ = nullptr;
    bool driver_ok_ = false;
    SensorList sensors_{};
    int sensor_count_ = 0;
    bool sensors_shared_ = false;
    bool backdrop_available_ = false;
    // 开机自启的状态缓存：查任务计划要走 COM，不能每次绘制都查，只在启动和改动后查
    wchar_t exe_path_[MAX_PATH]{};
    app::AutostartState autostart_ = app::AutostartState::Unknown;
    std::wstring autostart_path_;
    std::optional<std::wstring> original_autostart_;  // 空字符串表示自启原先关闭
    bool autostart_failed_ = false;

    std::unique_ptr<sampler::NetSampler> net_;
    std::unique_ptr<sampler::Snapshot> snapshot_;

    ComPtr<ID2D1Factory> d2d_;
    ComPtr<ID2D1PathGeometry> logo_;
    ComPtr<IDWriteFactory> dwrite_;
    unique_bitmap dib_;
    unique_memory_dc memory_dc_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    int surface_width_ = 0;
    int surface_height_ = 0;
    struct FontEntry {
        float size = 0;
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        bool icon = false;
        ComPtr<IDWriteTextFormat> format;
    };
    std::vector<FontEntry> fonts_;
    std::vector<std::wstring> font_names_;
    const wchar_t* font_family_ = L"Segoe UI";
    const wchar_t* icon_family_ = L"Segoe MDL2 Assets";
    unique_font edit_font_;
    unique_brush edit_brush_;
    // 标题栏图标用浮窗“设置”按钮同一个齿轮字形，深浅色跟随主题
    unique_icon icon_small_;
    unique_icon icon_big_;
};

// ---------------------------------------------------------------- 启动与消息

int SettingsWindow::Run(HINSTANCE instance) {
    instance_ = instance;
    wchar_t dir[MAX_PATH];
    if (!config::GetConfigPaths(dir, config_path_)) {
        return 1;
    }
    if (!config::GetLocalDataPath(names::kLogFile, log_path_)) {
        log_path_[0] = L'\0';
    }
    // 主进程平时会先建好配置文件；这里也做一遍，单独运行设置窗口时不出错
    const bool english = !SystemUsesChinese();
    if (!config::WriteDefaultConfig(config_path_, english)) {
        config::AppendMissingSections(config_path_, english);
    }
    Reload();
    SetLanguage(ResolveLanguage(config_.general.language));
    palette_ = MakePalette(config_.general);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())))) {
        return 1;
    }
    ComPtr<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(&fonts, FALSE))) {
        if (SUCCEEDED(fonts->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists) {
            font_family_ = L"Segoe UI Variable Text";
        }
        if (SUCCEEDED(fonts->FindFamilyName(L"Segoe Fluent Icons", &index, &exists)) && exists) {
            icon_family_ = L"Segoe Fluent Icons";
        }
        LoadFonts(fonts.Get());
    }

    // 网卡列表和每块网卡当前的速率（“网络”页），2 秒刷新一次
    net_ = std::make_unique<sampler::NetSampler>();
    snapshot_ = std::make_unique<sampler::Snapshot>();
    net_->Init();
    net_->Configure(config_.network.adapter, config_.network.show_virtual);
    net_->Sample(*snapshot_);
    // 只看驱动在不在：打开设备再关掉，不发任何请求
    driver_ok_ = static_cast<bool>(unique_file_handle(CreateFileW(names::kPawnIoDevice, GENERIC_READ,
                                                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                                                  OPEN_EXISTING, 0, nullptr)));
    if (GetModuleFileNameW(nullptr, exe_path_, MAX_PATH) != 0) {
        autostart_ = app::QueryAutostart(exe_path_, &autostart_path_);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = names::kSettingsWindowClass;
    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    // 按主显示器的 DPI 定初始大小，居中放在工作区里
    POINT origin{0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    const UINT dpi = GetDpiForSystem();
    RECT frame{0, 0, MulDiv(static_cast<int>(kWindowWidth), static_cast<int>(dpi), 96),
               MulDiv(static_cast<int>(kWindowHeight), static_cast<int>(dpi), 96)};
    AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    const int w = frame.right - frame.left;
    const int h = frame.bottom - frame.top;
    const int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    const int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    hwnd_ = CreateWindowExW(0, names::kSettingsWindowClass, Str().kSetTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            x, std::max(static_cast<int>(mi.rcWork.top), y), w, h, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        return 1;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    ApplyThemeAndLanguage();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetTimer(hwnd_, kTimerAdapters, kAdapterRefreshMs, nullptr);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void SettingsWindow::LoadFonts(IDWriteFontCollection* collection) {
    wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
    GetUserDefaultLocaleName(locale, static_cast<int>(std::size(locale)));
    for (UINT32 i = 0; i < collection->GetFontFamilyCount(); ++i) {
        ComPtr<IDWriteFontFamily> family;
        ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(collection->GetFontFamily(i, &family)) || FAILED(family->GetFamilyNames(&names))) {
            continue;
        }
        UINT32 name_index = 0;
        BOOL found = FALSE;
        names->FindLocaleName(locale, &name_index, &found);
        if (!found) {
            names->FindLocaleName(L"en-us", &name_index, &found);
        }
        if (!found) {
            name_index = 0;
        }
        UINT32 length = 0;
        if (FAILED(names->GetStringLength(name_index, &length)) || length == 0 || length >= 64) {
            continue;
        }
        std::wstring name(static_cast<size_t>(length) + 1, L'\0');
        if (SUCCEEDED(names->GetString(name_index, name.data(), length + 1))) {
            name.resize(length);
            font_names_.push_back(std::move(name));
        }
    }
    std::sort(font_names_.begin(), font_names_.end(),
              [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    font_names_.erase(std::unique(font_names_.begin(), font_names_.end(),
                                  [](const std::wstring& a, const std::wstring& b) {
                                      return _wcsicmp(a.c_str(), b.c_str()) == 0;
                                  }),
                      font_names_.end());
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<SettingsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    try {
        return self ? self->Handle(msg, wparam, lparam) : DefWindowProcW(hwnd, msg, wparam, lparam);
    } catch (const std::exception& e) {
        log::Write(L"settings: window message failed: %S", e.what());
    } catch (...) {
        log::Write(L"settings: window message failed");
    }
    return msg == WM_NCCREATE ? FALSE : msg == WM_CREATE ? -1 : 0;
}

LRESULT CALLBACK SettingsWindow::EditProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data) {
    auto* self = reinterpret_cast<SettingsWindow*>(data);
    try {
        if (msg == WM_KEYDOWN && wparam == VK_TAB) {
            for (size_t i = 0; i < self->items_.size(); ++i) {
                if (self->items_[i].edit == hwnd) {
                    self->FocusNextItem(static_cast<int>(i), (GetKeyState(VK_SHIFT) & 0x8000) != 0, true);
                    return 0;
                }
            }
        }
        if (msg == WM_KEYDOWN && wparam == VK_DOWN) {
            for (size_t i = 0; i < self->items_.size(); ++i) {
                if (self->items_[i].edit == hwnd && self->items_[i].kind == Kind::Font) {
                    SetFocus(self->hwnd_);
                    self->OpenCombo(static_cast<int>(i));
                    return 0;
                }
            }
        }
        if (msg == WM_KEYDOWN && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
            if (wparam == VK_RETURN) {
                self->CommitEdit(hwnd);
            } else {
                // Esc：放弃这次修改
                for (const Item& item : self->items_) {
                    if (item.edit == hwnd && item.get_text) {
                        SetWindowTextW(hwnd, item.get_text().c_str());
                    }
                }
            }
            SetFocus(self->hwnd_);
            return 0;
        }
        if (msg == WM_CHAR && (wparam == L'\r' || wparam == L'\t' || wparam == 0x1B)) {
            return 0;  // 不让单行编辑框“叮”一声
        }
        return DefSubclassProc(hwnd, msg, wparam, lparam);
    } catch (const std::exception& e) {
        log::Write(L"settings: edit message failed: %S", e.what());
    } catch (...) {
        log::Write(L"settings: edit message failed");
    }
    return 0;
}

LRESULT SettingsWindow::Handle(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        const HDC dc = BeginPaint(hwnd_, &ps);
        Render(dc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        ReleaseTarget();
        Layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = MulDiv(720, static_cast<int>(dpi_), 96);
        info->ptMinTrackSize.y = MulDiv(480, static_cast<int>(dpi_), 96);
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const auto* r = reinterpret_cast<const RECT*>(lparam);
        if (target_) {
            target_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));
        }
        ApplyThemeAndLanguage();
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SETTINGCHANGE:
        // 系统深浅色或强调色变了，跟着变
        if (lparam && wcscmp(reinterpret_cast<const wchar_t*>(lparam), L"ImmersiveColorSet") == 0) {
            ApplyThemeAndLanguage();
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wparam) == WA_INACTIVE && (open_combo_ >= 0 || open_color_ >= 0)) {
            open_combo_ = -1;
            open_color_ = -1;
            PositionEdits();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    case WM_TIMER:
        if (wparam == kTimerAdapters) {
            RefreshAdapters();
            RefreshSensors();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            int row = -1;
            int card = -1;
            if (strip_gap_ >= 0 || StripGapAt(Dip(pt.x), Dip(pt.y)) >= 0) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            if (Contains(strip_remove_rect_, Dip(pt.x), Dip(pt.y))) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            if (order_dragging_ || OrderCardAt(Dip(pt.x), Dip(pt.y), row, card)) {
                // 只有已有的项能拖；末尾的“添加”是个按钮
                SetCursor(LoadCursorW(nullptr, order_dragging_ || card < strip_count_ ? IDC_SIZEALL : IDC_HAND));
                return TRUE;
            }
        }
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    case WM_MOUSEMOVE: {
        const float x = Dip(GET_X_LPARAM(lparam));
        const float y = Dip(GET_Y_LPARAM(lparam));
        if (dragging_slider_ >= 0 && (wparam & MK_LBUTTON)) {
            SetSliderFromPosition(dragging_slider_, x);
            return 0;
        }
        if (strip_gap_ >= 0 && (wparam & MK_LBUTTON)) {
            SetStripGap(strip_gap_, x);
            return 0;
        }
        if (order_item_ >= 0 && (wparam & MK_LBUTTON)) {
            if (!order_dragging_ && order_from_ < strip_count_ && strip_count_ > 1 &&
                std::fabs(x - order_press_x_) >= 4.0f) {
                order_dragging_ = true;
            }
            if (order_dragging_ && static_cast<size_t>(order_item_) < items_.size() &&
                items_[static_cast<size_t>(order_item_)].option_rects.size() >= 2) {
                const auto& rects = items_[static_cast<size_t>(order_item_)].option_rects;
                const float pitch = rects[1].left - rects[0].left;
                const int target = StripCardIndexAt(x, rects[0].left, pitch, strip_count_);
                std::copy(std::begin(order_base_), std::end(order_base_), std::begin(order_preview_));
                MoveStripItem(order_preview_, order_from_, target);
                order_to_ = target;
            }
            order_pointer_x_ = x;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        int nav = HitNav(x, y);
        int item = -1;
        int option = -1;
        int combo = -1;
        int color = -1;
        if (open_color_ >= 0) {
            for (size_t i = 0; i < color_rects_.size(); ++i) {
                if (Contains(color_rects_[i], x, y)) {
                    color = static_cast<int>(i);
                }
            }
            nav = -1;
        } else if (open_combo_ >= 0) {
            for (size_t i = 0; i < combo_rects_.size(); ++i) {
                if (Contains(combo_rects_[i], x, y)) {
                    combo = combo_first_ + static_cast<int>(i);
                }
            }
            nav = -1;
        } else {
            for (size_t i = 0; i < items_.size(); ++i) {
                const Item& it = items_[i];
                if (Contains(it.control, x, y)) {
                    item = static_cast<int>(i);
                }
                for (size_t o = 0; o < it.option_rects.size(); ++o) {
                    if (Contains(it.option_rects[o], x, y)) {
                        item = static_cast<int>(i);
                        option = static_cast<int>(o);
                    }
                }
            }
        }
        const bool remove_hover = open_combo_ < 0 && open_color_ < 0 && Contains(strip_remove_rect_, x, y);
        const int gap_hover = open_combo_ < 0 && open_color_ < 0 && strip_gap_ < 0 ? StripGapAt(x, y) : -1;
        if (nav != hovered_nav_ || item != hovered_item_ || option != hovered_option_ || combo != combo_hovered_ ||
            color != color_hovered_ || remove_hover != strip_remove_hover_ || gap_hover != strip_gap_hover_) {
            strip_remove_hover_ = remove_hover;
            strip_gap_hover_ = gap_hover;
            hovered_nav_ = nav;
            hovered_item_ = item;
            hovered_option_ = option;
            combo_hovered_ = combo;
            color_hovered_ = color;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        hovered_nav_ = hovered_item_ = hovered_option_ = combo_hovered_ = color_hovered_ = -1;
        strip_gap_hover_ = -1;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        // 点到输入框以外：让输入框失去焦点，它的内容随之生效
        SetFocus(hwnd_);
        focused_slider_ = -1;
        focused_item_ = -1;
        if (open_combo_ < 0 && open_color_ < 0) {
            const float x = Dip(GET_X_LPARAM(lparam));
            const float y = Dip(GET_Y_LPARAM(lparam));
            const int gap = StripGapAt(x, y);
            if (gap >= 0) {
                strip_gap_ = gap;
                strip_gap_hover_ = gap;
                for (const Item& item : items_) {
                    if (item.kind == Kind::Reorder) {
                        const float total = std::max({strip_room_, StripWindowWidth(strip_layout_.content_width), 1.0f});
                        const float scale = (item.track.right - item.track.left) / total;
                        strip_gap_pointer_offset_ = item.track.left +
                            (kStripFramePad + strip_layout_.spans[gap + 1].left) * scale - x;
                        break;
                    }
                }
                SetCapture(hwnd_);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (Contains(strip_remove_rect_, x, y)) {
                RemoveStripItemAt(strip_selected_);
                return 0;
            }
            int order_row = -1;
            int order_card = -1;
            if (OrderCardAt(x, y, order_row, order_card)) {
                order_item_ = order_row;
                order_from_ = order_card;
                order_to_ = order_card;
                order_press_x_ = order_pointer_x_ = x;
                order_dragging_ = false;
                std::copy(std::begin(config_.strip.items), std::end(config_.strip.items), std::begin(order_base_));
                std::copy(std::begin(order_base_), std::end(order_base_), std::begin(order_preview_));
                SetCapture(hwnd_);
                return 0;
            }
            for (size_t i = 0; i < items_.size(); ++i) {
                const Item& item = items_[i];
                if (item.kind == Kind::Slider && Contains(item.track, x, y)) {
                    focused_slider_ = dragging_slider_ = static_cast<int>(i);
                    focused_item_ = static_cast<int>(i);
                    SetCapture(hwnd_);
                    SetSliderFromPosition(dragging_slider_, x);
                    break;
                }
            }
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (strip_gap_ >= 0) {
            SetStripGap(strip_gap_, Dip(GET_X_LPARAM(lparam)));
            strip_gap_ = -1;
            ReleaseCapture();
            return 0;
        }
        if (dragging_slider_ >= 0) {
            SetSliderFromPosition(dragging_slider_, Dip(GET_X_LPARAM(lparam)));
            dragging_slider_ = -1;
            ReleaseCapture();
            return 0;
        }
        if (order_item_ >= 0) {
            const bool dragging = order_dragging_;
            const int from = order_from_;
            const int row = order_item_;
            config::StripItemConfig preview[kStripItemCount];
            std::copy(std::begin(order_preview_), std::end(order_preview_), std::begin(preview));
            order_item_ = -1;
            order_dragging_ = false;
            ReleaseCapture();
            if (dragging) {
                // 顺序不影响总宽，不用再判断放不放得下
                if (!std::equal(std::begin(preview), std::end(preview), std::begin(config_.strip.items))) {
                    config::CompactStripItems(preview);
                    strip_note_.clear();
                    strip_selected_ = std::min(order_to_, kStripItemCount - 1);
                    WriteStripItems(preview);
                    Build();
                }
            } else if (row >= 0 && from >= strip_count_) {
                AddStripItem();
            } else if (row >= 0) {
                // 点卡片：选中它，下面的来源、名称、竖条跟着换
                strip_selected_ = from;
                strip_note_.clear();
                Build();
            }
            return 0;
        }
        OnClick(Dip(GET_X_LPARAM(lparam)), Dip(GET_Y_LPARAM(lparam)));
        return 0;
    case WM_CAPTURECHANGED:
        dragging_slider_ = -1;
        order_item_ = -1;
        order_dragging_ = false;
        strip_gap_ = -1;
        return 0;
    case WM_MOUSEWHEEL: {
        if (open_combo_ >= 0) {
            const int steps = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            combo_first_ -= steps * 3;
            combo_hovered_ = -1;
            LayoutCombo();
            if (!combo_rects_.empty()) {
                combo_cursor_ = std::clamp(combo_cursor_, combo_first_,
                                           combo_first_ + static_cast<int>(combo_rects_.size()) - 1);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        open_color_ = -1;
        const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA * 64.0f;
        scroll_ = std::clamp(scroll_ - delta, 0.0f, max_scroll_);
        Layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == VK_TAB) {
            open_combo_ = -1;
            open_color_ = -1;
            PositionEdits();
            FocusNextItem(focused_item_, (GetKeyState(VK_SHIFT) & 0x8000) != 0, false);
            return 0;
        }
        if (wparam == VK_ESCAPE && (open_combo_ >= 0 || open_color_ >= 0)) {
            open_combo_ = -1;
            open_color_ = -1;
            PositionEdits();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        if (open_combo_ >= 0) {
            const Item& item = items_[static_cast<size_t>(open_combo_)];
            const int count = static_cast<int>(item.options.size());
            if (wparam == VK_RETURN || wparam == VK_SPACE) {
                const int index = combo_cursor_;
                const auto select = item.set_index;
                const int selected = item.get_index();
                open_combo_ = -1;
                if (index >= 0 && index < count && index != selected) {
                    select(index);
                }
                PositionEdits();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            const int delta = wparam == VK_UP ? -1 : wparam == VK_DOWN ? 1 : wparam == VK_PRIOR ? -kMaxComboRows
                                                              : wparam == VK_NEXT ? kMaxComboRows : 0;
            if (delta || wparam == VK_HOME || wparam == VK_END) {
                combo_cursor_ = wparam == VK_HOME ? 0 : wparam == VK_END ? count - 1
                                              : std::clamp(combo_cursor_ + delta, 0, count - 1);
                combo_first_ = ShowOption(combo_first_, combo_cursor_, count, static_cast<int>(combo_rects_.size()));
                LayoutCombo();
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
        }
        if (open_color_ >= 0) {
            if (wparam == VK_RETURN || wparam == VK_SPACE) {
                if (color_hovered_ >= 0) {
                    const D2D1_RECT_F& r = color_rects_[static_cast<size_t>(color_hovered_)];
                    OnClick((r.left + r.right) / 2, (r.top + r.bottom) / 2);
                }
                return 0;
            }
            const int delta = wparam == VK_LEFT ? -1 : wparam == VK_RIGHT ? 1 : wparam == VK_UP ? -8
                                                            : wparam == VK_DOWN ? 8 : 0;
            if (delta) {
                color_hovered_ = std::clamp(std::max(0, color_hovered_) + delta, 0,
                                             static_cast<int>(kAccentColors.size()) - 1);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
        }
        if (focused_slider_ >= 0 && static_cast<size_t>(focused_slider_) < items_.size()) {
            Item& item = items_[static_cast<size_t>(focused_slider_)];
            const int delta = wparam == VK_LEFT || wparam == VK_DOWN ? -1 : wparam == VK_RIGHT || wparam == VK_UP ? 1
                                                        : wparam == VK_PRIOR ? 5 : wparam == VK_NEXT ? -5 : 0;
            if (delta || wparam == VK_HOME || wparam == VK_END) {
                const int current = static_cast<int>(std::lround(item.get_number()));
                const int value = wparam == VK_HOME ? item.minimum : wparam == VK_END ? item.maximum
                                                    : std::clamp(current + delta, item.minimum, item.maximum);
                if (value != current) {
                    item.set_number(value);
                }
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
        }
        if (focused_item_ >= 0 && static_cast<size_t>(focused_item_) < items_.size()) {
            const Item& item = items_[static_cast<size_t>(focused_item_)];
            const int delta = wparam == VK_UP || wparam == VK_LEFT ? -1 :
                              wparam == VK_DOWN || wparam == VK_RIGHT ? 1 : 0;
            if (item.kind == Kind::Radio && delta && !item.options.empty()) {
                const int selected = std::clamp(item.get_index() + delta, 0,
                                                static_cast<int>(item.options.size()) - 1);
                const auto set = item.set_index;
                set(selected);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
        }
        if ((wparam == VK_RETURN || wparam == VK_SPACE) && focused_item_ >= 0 &&
            static_cast<size_t>(focused_item_) < items_.size()) {
            const Item& item = items_[static_cast<size_t>(focused_item_)];
            if (item.kind == Kind::Color) {
                OpenColor(focused_item_);
            } else if (item.kind == Kind::Combo) {
                OpenCombo(focused_item_);
            } else if (item.kind == Kind::Toggle || item.kind == Kind::Button || item.kind == Kind::Undo) {
                OnClick((item.control.left + item.control.right) / 2, (item.control.top + item.control.bottom) / 2);
            }
            return 0;
        }
        return 0;
    case WM_COMMAND:
        if (HIWORD(wparam) == EN_KILLFOCUS) {
            CommitEdit(reinterpret_cast<HWND>(lparam));
            focused_edit_ = nullptr;
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (HIWORD(wparam) == EN_SETFOCUS) {
            focused_edit_ = reinterpret_cast<HWND>(lparam);
            if (open_combo_ >= 0 || open_color_ >= 0) {
                open_combo_ = -1;
                open_color_ = -1;
                PositionEdits();
            }
            for (size_t i = 0; i < items_.size(); ++i) {
                if (items_[i].edit == focused_edit_) {
                    focused_item_ = static_cast<int>(i);
                    focused_slider_ = -1;
                    break;
                }
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, ToColorRef(palette_.edit_text));
        SetBkColor(dc, ToColorRef(palette_.edit_bg));
        return reinterpret_cast<LRESULT>(edit_brush_.get());
    }
    case WM_DESTROY:
        KillTimer(hwnd_, kTimerAdapters);
        DestroyEdits();
        edit_font_.reset();
        edit_brush_.reset();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

// ---------------------------------------------------------------- 配置

void SettingsWindow::Reload() { config_ = config::LoadConfig(config_path_); }

std::optional<std::wstring> SettingsWindow::ReadKey(const wchar_t* section, const wchar_t* key) const {
    std::wstring value(config::kStripItemsTextChars, L'\0');
    const wchar_t missing[] = L"\x01";
    DWORD length = 0;
    for (;;) {
        const DWORD capacity = static_cast<DWORD>(value.size());
        length = GetPrivateProfileStringW(section, key, missing, value.data(), capacity, config_path_);
        if (length < capacity - 1) {
            break;
        }
        value.resize(value.size() * 2);
    }
    value.resize(length);
    return value == missing ? std::nullopt : std::optional(std::move(value));
}

void SettingsWindow::RecordChange(const wchar_t* section, const wchar_t* key,
                                  std::optional<std::wstring> original) {
    auto& changes = changes_[static_cast<size_t>(page_)];
    const auto existing = std::find_if(changes.begin(), changes.end(), [&](const SettingChange& change) {
        return change.section == section && change.key == key;
    });
    const auto current = ReadKey(section, key);
    if (existing != changes.end()) {
        if (existing->original == current) {
            changes.erase(existing);
        }
    } else if (original != current) {
        changes.push_back({section, key, std::move(original)});
    }
}

void SettingsWindow::WriteKey(const wchar_t* section, const wchar_t* key, const wchar_t* value) {
    auto original = ReadKey(section, key);
    if ((!value && !original) || (value && original && *original == value)) {
        return;
    }
    if (config::WriteValue(config_path_, section, key, value)) {
        RecordChange(section, key, std::move(original));
    } else {
        log::Write(L"settings: cannot write [%s] %s", section, key);
    }
}

bool SettingsWindow::WriteSensor(const wchar_t* key, bool visible, const wchar_t* alias) {
    const std::wstring section = std::wstring(L"sensor.") + key;
    auto original_visible = ReadKey(section.c_str(), L"visible");
    auto original_alias = ReadKey(section.c_str(), L"alias");
    const bool saved = config::WriteSensorSetting(config_path_, key, visible, alias);
    RecordChange(section.c_str(), L"visible", std::move(original_visible));
    RecordChange(section.c_str(), L"alias", std::move(original_alias));
    if (!saved) {
        log::Write(L"settings: cannot write [%s]", section.c_str());
    }
    Reload();
    return saved;
}

bool SettingsWindow::CanUndo() const {
    return page_ != Page::About && (!changes_[static_cast<size_t>(page_)].empty() ||
                                    (page_ == Page::General && original_autostart_.has_value()));
}

void SettingsWindow::UndoChanges() {
    auto& changes = changes_[static_cast<size_t>(page_)];
    std::erase_if(changes, [this](const SettingChange& change) {
        const wchar_t* value = change.original ? change.original->c_str() : nullptr;
        if (config::WriteValue(config_path_, change.section.c_str(), change.key.c_str(), value)) {
            return true;
        }
        log::Write(L"settings: cannot restore [%s] %s", change.section.c_str(), change.key.c_str());
        return false;
    });
    if (page_ == Page::General && original_autostart_) {
        autostart_failed_ = !app::SetAutostart(!original_autostart_->empty(), original_autostart_->c_str());
        autostart_ = app::QueryAutostart(exe_path_, &autostart_path_);
        if (!autostart_failed_) {
            original_autostart_.reset();
        }
    }
    Reload();
    if (page_ == Page::Network) {
        net_->Configure(config_.network.adapter, config_.network.show_virtual);
        net_->Sample(*snapshot_);
    }
    strip_note_.clear();
    if (page_ == Page::General) {
        ApplyThemeAndLanguage();
    } else {
        Build();
    }
    if (CanUndo()) {
        MessageBoxW(hwnd_, Str().kSetUndoFailed, Str().kSetUndo, MB_OK | MB_ICONWARNING);
    }
}

void SettingsWindow::Write(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WriteKey(section, key, value.c_str());
    Reload();
    for (const Item& item : items_) {
        if (item.edit && item.edit != focused_edit_ && item.get_text) {
            SetWindowTextW(item.edit, item.get_text().c_str());
        }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::UpdateWindowIcon() {
    if (!hwnd_) {
        return;
    }
    const UINT dpi = dpi_ != 0 ? dpi_ : GetDpiForWindow(hwnd_);
    if (!logo_ && d2d_) {
        CreateLogoGeometry(d2d_.Get(), logo_.ReleaseAndGetAddressOf());
    }
    // 标题栏用单色标志；任务栏和 Alt+Tab 用的大图标是彩色应用图标（D-082）
    unique_icon small_icon =
        MakeLogoIcon(d2d_.Get(), logo_.Get(), GetSystemMetricsForDpi(SM_CXSMICON, dpi), palette_.text);
    const int big_size = GetSystemMetricsForDpi(SM_CXICON, dpi);
    unique_icon big_icon(static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_PITOT), IMAGE_ICON, big_size, big_size, 0)));
    if (small_icon) {
        SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon.get()));
        icon_small_ = std::move(small_icon);
    }
    if (big_icon) {
        SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon.get()));
        icon_big_ = std::move(big_icon);
    }
}

void SettingsWindow::ApplyThemeAndLanguage() {
    SetLanguage(ResolveLanguage(config_.general.language));
    palette_ = MakePalette(config_.general);
    SetWindowTextW(hwnd_, Str().kSetTitle);
    UpdateWindowIcon();
    const BOOL dark = palette_.dark;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
    const HRESULT backdrop_result = DwmSetWindowAttribute(hwnd_, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    const MARGINS margins{-1, -1, -1, -1};
    const HRESULT frame_result = DwmExtendFrameIntoClientArea(hwnd_, &margins);
    backdrop_available_ = SUCCEEDED(backdrop_result) && SUCCEEDED(frame_result);
    if (!backdrop_available_) {
        ApplyOpaqueFallback(palette_);
    }
    DestroyEdits();
    edit_brush_.reset(CreateSolidBrush(ToColorRef(palette_.edit_bg)));
    edit_font_.reset(CreateFontW(-MulDiv(14, static_cast<int>(dpi_), 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, font_family_));
    fonts_.clear();
    Build();
}

// ---------------------------------------------------------------- 页面内容

Item& SettingsWindow::Header(const wchar_t* title) {
    Item& item = items_.emplace_back();
    item.kind = Kind::Header;
    item.title = title;
    return item;
}

Item& SettingsWindow::Toggle(wchar_t glyph, const wchar_t* title, const wchar_t* desc, std::function<bool()> get,
                             std::function<void(bool)> set) {
    Item& item = items_.emplace_back();
    item.kind = Kind::Toggle;
    item.glyph = glyph;
    item.title = title;
    item.desc = desc ? desc : L"";
    item.get_on = std::move(get);
    item.set_on = std::move(set);
    return item;
}

Item& SettingsWindow::Combo(wchar_t glyph, const wchar_t* title, const wchar_t* desc, std::vector<Option> options,
                            std::function<int()> get, std::function<void(int)> set) {
    Item& item = items_.emplace_back();
    item.kind = Kind::Combo;
    item.glyph = glyph;
    item.title = title;
    item.desc = desc ? desc : L"";
    item.options = std::move(options);
    item.get_index = std::move(get);
    item.set_index = std::move(set);
    return item;
}

Item& SettingsWindow::NumberCombo(wchar_t glyph, const wchar_t* title, const wchar_t* desc, std::vector<int> values,
                                  const wchar_t* format, std::function<int()> get, std::function<void(int)> set) {
    const int current = get();
    if (std::find(values.begin(), values.end(), current) == values.end()) {
        values.push_back(current);
        std::sort(values.begin(), values.end());
    }
    std::vector<Option> options;
    for (int v : values) {
        wchar_t text[32];
        _snwprintf_s(text, _TRUNCATE, format, v);
        options.push_back({text, L"", L"", 0});
    }
    return Combo(
        glyph, title, desc, std::move(options),
        [values, get] {
            const auto it = std::find(values.begin(), values.end(), get());
            return it == values.end() ? -1 : static_cast<int>(it - values.begin());
        },
        [values, set](int i) { set(values[static_cast<size_t>(i)]); });
}

Item& SettingsWindow::SecondsCombo(wchar_t glyph, const wchar_t* title, const wchar_t* desc,
                                   std::vector<int> values_ms, const wchar_t* key,
                                   int config::GeneralConfig::*field) {
    const auto seconds = [](int ms) { return static_cast<double>(ms) / 1000.0; };
    const int current = config_.general.*field;
    if (std::find(values_ms.begin(), values_ms.end(), current) == values_ms.end()) {
        values_ms.push_back(current);
        std::sort(values_ms.begin(), values_ms.end());
    }
    std::vector<Option> options;
    for (int ms : values_ms) {
        wchar_t text[32];
        _snwprintf_s(text, _TRUNCATE, Str().kSetSecondsFormat, seconds(ms));
        options.push_back({text, L"", L"", 0});
    }
    return Combo(
        glyph, title, desc, std::move(options),
        [this, values_ms, field] {
            const auto it = std::find(values_ms.begin(), values_ms.end(), config_.general.*field);
            return it == values_ms.end() ? -1 : static_cast<int>(it - values_ms.begin());
        },
        [this, values_ms, key, seconds](int i) {
            wchar_t text[16];
            _snwprintf_s(text, _TRUNCATE, L"%g", seconds(values_ms[static_cast<size_t>(i)]));
            Write(L"general", key, text);
        });
}

Item& SettingsWindow::Input(wchar_t glyph, const wchar_t* title, const wchar_t* desc, float width, const wchar_t* unit,
                            std::function<std::wstring()> get, std::function<bool(const std::wstring&)> set) {
    Item& item = items_.emplace_back();
    item.kind = Kind::Input;
    item.glyph = glyph;
    item.title = title;
    item.desc = desc ? desc : L"";
    item.input_width = width;
    item.unit = unit ? unit : L"";
    item.get_text = std::move(get);
    item.set_text = std::move(set);
    return item;
}

Item& SettingsWindow::NumberInput(wchar_t glyph, const wchar_t* title, const wchar_t* desc, int min, int max,
                                  const wchar_t* unit, std::function<float()> get, std::function<void(int)> set) {
    Item& item = Input(
        glyph, title, desc, 72.0f, unit,
        [get] {
            wchar_t text[16];
            _snwprintf_s(text, _TRUNCATE, L"%g", static_cast<double>(get()));
            return std::wstring(text);
        },
        [min, max, set](const std::wstring& text) {
            wchar_t* end = nullptr;
            const long v = wcstol(text.c_str(), &end, 10);
            if (end == text.c_str() || *end != L'\0') {
                return false;
            }
            set(static_cast<int>(std::clamp(v, static_cast<long>(min), static_cast<long>(max))));
            return true;
        });
    item.kind = Kind::Slider;
    item.minimum = min;
    item.maximum = max;
    item.get_number = std::move(get);
    item.set_number = std::move(set);
    return item;
}

Item& SettingsWindow::Wallpaper(wchar_t glyph, const wchar_t* title, const wchar_t* key, const wchar_t* path) {
    Item& item = items_.emplace_back();
    item.kind = Kind::Button;
    item.glyph = glyph;
    item.title = title;
    item.desc = path[0] ? path : Str().kSetWallpaperNone;
    item.value = Str().kSetChooseWallpaper;
    item.action = [this, title, key] {
        wchar_t file[MAX_PATH] = L"";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd_;
        dialog.lpstrFilter = Str().kSetWallpaperFilter;
        dialog.lpstrFile = file;
        dialog.nMaxFile = static_cast<DWORD>(std::size(file));
        dialog.lpstrTitle = title;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
        if (GetOpenFileNameW(&dialog)) {
            Write(L"theme_switch", key, file);
            Build();
        }
    };
    return item;
}

void SettingsWindow::Build() {
    DestroyEdits();
    items_.clear();
    open_combo_ = -1;
    open_color_ = -1;
    focused_slider_ = -1;
    focused_item_ = -1;
    dragging_slider_ = -1;
    hovered_item_ = hovered_option_ = -1;
    strip_remove_rect_ = {};
    strip_remove_hover_ = false;
    strip_gap_hover_ = -1;
    if (page_ != Page::About) {
        Item& undo = items_.emplace_back();
        undo.kind = Kind::Undo;
        undo.value = Str().kSetUndo;
    }
    switch (page_) {
    case Page::General:
        BuildGeneral();
        break;
    case Page::Strip:
        BuildStrip();
        break;
    case Page::Flyout:
        BuildFlyout();
        break;
    case Page::Sensors:
        BuildSensors();
        break;
    case Page::Network:
        BuildNetwork();
        break;
    case Page::About:
        BuildAbout();
        break;
    }
    // 输入框用系统的 EDIT 控件（要输入法和光标），外框自己画
    for (Item& item : items_) {
        if (item.kind != Kind::Input && item.kind != Kind::Slider && item.kind != Kind::Color &&
            item.kind != Kind::Font) {
            continue;
        }
        // 玻璃客户区上的 GDI 控件需要独立的不透明合成表面，避免文字的 alpha 被当成全透明。
        item.edit = CreateWindowExW(WS_EX_LAYERED, L"EDIT", item.get_text().c_str(), WS_CHILD | ES_AUTOHSCROLL,
                                    0, 0, 0, 0, hwnd_, nullptr, instance_, nullptr);
        if (item.edit) {
            SetLayeredWindowAttributes(item.edit, 0, 255, LWA_ALPHA);
            SendMessageW(item.edit, WM_SETFONT, reinterpret_cast<WPARAM>(edit_font_.get()), FALSE);
            if (item.kind == Kind::Font) {
                SendMessageW(item.edit, EM_SETLIMITTEXT, 63, 0);
            }
            SetWindowSubclass(item.edit, EditProc, kEditSubclassId, reinterpret_cast<DWORD_PTR>(this));
        }
    }
    Layout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::BuildGeneral() {
    const Strings& s = Str();
    Header(s.kSetSecAppearance);
    Combo(
        kGlyphTheme, s.kSetTheme, s.kSetThemeDesc, {{s.kSetFollowSystem}, {s.kSetLight}, {s.kSetDark}},
        [this] { return static_cast<int>(config_.general.theme); },
        [this](int i) {
            const wchar_t* values[] = {L"system", L"light", L"dark"};
            Write(L"general", L"theme", values[i]);
            ApplyThemeAndLanguage();
        });
    Combo(
        kGlyphTheme, s.kSetAccent, s.kSetAccentDesc, {{s.kSetFollowSystem}, {s.kSetCustom}},
        [this] { return config_.general.accent ? 1 : 0; },
        [this](int i) {
            if (i == 0) {
                Write(L"general", L"accent", L"system");
            } else if (!config_.general.accent) {
                // 从当前颜色开始改，比空着好
                const D2D1_COLOR_F c = palette_.accent;
                wchar_t hex[16];
                _snwprintf_s(hex, _TRUNCATE, L"#%02X%02X%02X", static_cast<int>(c.r * 255 + 0.5f),
                             static_cast<int>(c.g * 255 + 0.5f), static_cast<int>(c.b * 255 + 0.5f));
                Write(L"general", L"accent", hex);
            }
            ApplyThemeAndLanguage();
        });
    if (config_.general.accent) {
        Item& color = Input(
            kGlyphTheme, s.kSetAccentColor, s.kSetAccentColorDesc, 120.0f, nullptr,
            [this] {
                wchar_t hex[16];
                _snwprintf_s(hex, _TRUNCATE, L"#%06X", config_.general.accent.value_or(0));
                return std::wstring(hex);
            },
            [this](const std::wstring& text) {
                const auto color = config::ParseColor(text.c_str());
                if (!color) {
                    return false;
                }
                wchar_t hex[16];
                _snwprintf_s(hex, _TRUNCATE, L"#%06X", *color);
                Write(L"general", L"accent", hex);
                palette_ = MakePalette(config_.general);
                if (!backdrop_available_) {
                    ApplyOpaqueFallback(palette_);
                }
                return true;
            });
        color.kind = Kind::Color;
    }
    Combo(
        kGlyphLanguage, s.kSetLanguage, nullptr, {{s.kSetFollowWindows}, {s.kSetLangZh}, {s.kSetLangEn}},
        [this] { return static_cast<int>(config_.general.language); },
        [this](int i) {
            const wchar_t* values[] = {L"auto", L"zh-CN", L"en-US"};
            Write(L"general", L"language", values[i]);
            ApplyThemeAndLanguage();
        });

    // 浮窗右上角小太阳按这里选的两张壁纸切换（D-065）
    Header(s.kSetSecDesktopTheme);
    Wallpaper(kGlyphSun, s.kSetLightWallpaper, L"light_wallpaper", config_.theme_switch.light_wallpaper);
    Wallpaper(kGlyphSun, s.kSetDarkWallpaper, L"dark_wallpaper", config_.theme_switch.dark_wallpaper);

    Header(s.kSetSecStartup);
    {
        wchar_t other[MAX_PATH + 160];
        const wchar_t* desc = s.kSetAutostartDesc;
        if (autostart_failed_) {
            desc = s.kSetAutostartFailed;
        } else if (autostart_ == app::AutostartState::OtherPath) {
            _snwprintf_s(other, _TRUNCATE, s.kSetAutostartOtherPath, autostart_path_.c_str());
            desc = other;
        }
        // 取不到任务状态（任务计划服务不可用）时开关也能点：失败了会在说明里提示
        Toggle(
            kGlyphPower, s.kSetAutostart, desc, [this] { return autostart_ == app::AutostartState::On; },
            [this](bool on) {
                const app::AutostartState previous = autostart_;
                const std::wstring previous_path = autostart_path_;
                autostart_failed_ = !app::SetAutostart(on, exe_path_);
                autostart_ = app::QueryAutostart(exe_path_, &autostart_path_);
                if (!autostart_failed_ && !original_autostart_ && previous != app::AutostartState::Unknown &&
                    (previous == app::AutostartState::Off || !previous_path.empty())) {
                    original_autostart_ = previous_path;
                }
                if (original_autostart_ && autostart_ != app::AutostartState::Unknown &&
                    _wcsicmp(original_autostart_->c_str(), autostart_path_.c_str()) == 0) {
                    original_autostart_.reset();
                }
                Build();
            });
    }

    Header(s.kSetSecRefresh);
    SecondsCombo(kGlyphRefresh, s.kSetInterval, s.kSetIntervalDesc, {1000, 1500, 2000, 5000}, L"interval",
                 &config::GeneralConfig::interval_ms);
    SecondsCombo(kGlyphRefresh, s.kSetSensorInterval, s.kSetSensorIntervalDesc, {2000, 5000, 10000},
                 L"sensor_interval", &config::GeneralConfig::sensor_interval_ms);
}

StripMeasured SettingsWindow::MeasureStrip(const config::StripStyle& style) const {
    return MeasureStripStyle(dwrite_.Get(), style);
}

bool SettingsWindow::StripChoiceFits(const config::StripStyle& style) const {
    const StripLayout layout = LayoutStrip(style.items, ColumnsOf(style, MeasureStrip(style)));
    return StripFits(layout.content_width, strip_room_);
}

void SettingsWindow::BuildStrip() {
    const Strings& s = Str();
    // 开始按钮前还有多宽。每次重建都重新量：固定的图标多了少了，居中的开始按钮会左右移动
    const std::optional<TaskbarInfo> taskbar = FindTaskbar();
    strip_room_ = taskbar ? StripSpaceFor(*taskbar, config_.strip.position).room() : kStripFallbackRoomDip;
    strip_count_ = config::CountStripItems(config_.strip.items);
    strip_layout_ = LayoutStrip(config_.strip.items, ColumnsOf(config_.strip, MeasureStrip(config_.strip)));
    if (strip_selected_ < 0 || strip_selected_ >= strip_count_) {
        strip_selected_ = std::max(0, strip_count_ - 1);
    }

    // 样式（D-075）：A 双行堆叠、B 迷你曲线、C 单行极简。换样式会改变宽度，放不下时照样换，宽度条上标红
    Combo(
        kGlyphStrip, s.kSetStripStyle, nullptr, {{s.kSetStyleStack}, {s.kSetStyleSpark}, {s.kSetStyleMinimal}},
        [this] { return static_cast<int>(config_.strip.variant); },
        [this](int i) {
            const wchar_t* values[] = {L"a", L"b", L"c"};
            Write(L"strip", L"style", values[i]);
            Build();
        });

    Header(s.kSetSecItems);
    items_.emplace_back().kind = Kind::Reorder;

    // 选中的那一项：数值来源、温度来源、名称、竖条。移除在卡片右上角的 ×
    const config::StripItemConfig& selected = config_.strip.items[strip_selected_];
    std::vector<Option> sources = {{s.kSetSourceCpu},  {s.kSetSourceMemory},       {s.kSetSourceGpu},
                                   {s.kSetSourceTemp}, {s.kSetSourceDiskActivity}, {s.kSetSourceDisk},
                                   {s.kSetSourceNet}};
    static constexpr config::StripSource kSources[] = {
        config::StripSource::Cpu,          config::StripSource::Memory, config::StripSource::Gpu,
        config::StripSource::Temp,         config::StripSource::DiskActivity,
        config::StripSource::Disk,         config::StripSource::Net};
    Combo(kGlyphStrip, s.kSetStripSource, nullptr, std::move(sources), [this] {
        const config::StripSource source = config_.strip.items[strip_selected_].source;
        const auto it = std::find(std::begin(kSources), std::end(kSources), source);
        return it == std::end(kSources) ? -1 : static_cast<int>(it - std::begin(kSources));
    }, [this](int i) {
        if (i >= 0 && i < static_cast<int>(std::size(kSources))) {
            SetStripSource(kSources[i]);
        }
    });
    if (selected.source == config::StripSource::Temp) {
        Combo(kGlyphSensors, s.kSetStripTempSource, nullptr,
              {{s.kLabelCpu}, {s.kLabelGpu}, {s.kLabelSsd}, {s.kLabelBoard}},
              [this] { return static_cast<int>(config_.strip.items[strip_selected_].temp); },
              [this](int i) {
                  if (i >= 0 && i < config::kStripTempSourceCount) {
                      SetStripTempSource(static_cast<config::StripTempSource>(i));
                  }
              });
    }
    if (config::StripSourceHasLabel(selected.source)) {
        Input(
            kGlyphTag, s.kSetStripName, s.kSetLabelDesc, 120.0f, nullptr,
            [this] { return std::wstring(StripItemLabel(config_.strip.items[strip_selected_])); },
            [this](const std::wstring& text) { return SetStripLabel(text); });
    }
    if (config::StripSourceHasBar(selected.source)) {
        Toggle(
            kGlyphBar, s.kSetStripBar, nullptr, [this] { return config_.strip.items[strip_selected_].bar; },
            [this](bool on) {
                config::StripStyle next = config_.strip;
                next.items[strip_selected_].bar = on;
                CommitStripItems(next);
            });
    }

    Header(s.kSetSecFont);
    Item& font = Input(
        kGlyphFont, s.kSetFont, s.kSetFontDesc, 220.0f, nullptr, [this] { return std::wstring(config_.strip.font); },
        [this](const std::wstring& text) {
            if (text.empty()) {
                return false;
            }
            Write(L"strip", L"font", text);
            return true;
        });
    font.kind = Kind::Font;
    for (const std::wstring& name : font_names_) {
        font.options.push_back({name});
    }
    font.get_index = [this] {
        const auto it = std::find_if(font_names_.begin(), font_names_.end(),
                                     [this](const std::wstring& name) {
                                         return _wcsicmp(name.c_str(), config_.strip.font) == 0;
                                     });
        return it == font_names_.end() ? -1 : static_cast<int>(it - font_names_.begin());
    };
    font.set_index = [this](int i) {
        if (i >= 0 && static_cast<size_t>(i) < font_names_.size()) {
            Write(L"strip", L"font", font_names_[static_cast<size_t>(i)]);
        }
    };
    const auto size_input = [this](const wchar_t* title, const wchar_t* key, float config::StripStyle::*field) {
        NumberInput(
            kGlyphFontSize, title, nullptr, 6, 32, Str().kSetPx, [this, field] { return config_.strip.*field; },
            [this, key](int v) { WriteInt(L"strip", key, v); });
    };
    size_input(s.kSetLabelSize, L"label_size", &config::StripStyle::label_size);
    size_input(s.kSetValueSize, L"value_size", &config::StripStyle::value_size);
    size_input(s.kSetNetSize, L"net_size", &config::StripStyle::net_size);
    Combo(
        kGlyphBold, s.kSetValueWeight, nullptr, {{s.kSetWeightRegular}, {s.kSetWeightSemibold}, {s.kSetWeightBold}},
        [this] {
            const int w = config_.strip.value_weight;
            return w < 500 ? 0 : w < 650 ? 1 : 2;
        },
        [this](int i) {
            const int weights[] = {400, 600, 700};
            WriteInt(L"strip", L"value_weight", weights[i]);
        });

    Header(s.kSetSecSpacing);
    const auto gap_input = [this](const wchar_t* title, const wchar_t* key, float config::StripStyle::*field) {
        Input(
            kGlyphSpacing, title, nullptr, 96.0f, Str().kSetPx,
            [this, field] {
                wchar_t text[24];
                _snwprintf_s(text, _TRUNCATE, L"%g", static_cast<double>(config_.strip.*field));
                return std::wstring(text);
            },
            [this, key](const std::wstring& text) {
                wchar_t* end = nullptr;
                const double value = wcstod(text.c_str(), &end);
                if (end == text.c_str() || *end != L'\0' || !std::isfinite(value)) {
                    return false;
                }
                const double spacing = std::clamp(value, 0.0, static_cast<double>(config::kMaxStripSpacing));
                wchar_t formatted[24];
                _snwprintf_s(formatted, _TRUNCATE, L"%.*g", std::numeric_limits<float>::max_digits10, spacing);
                Write(L"strip", key, formatted);
                strip_note_.clear();
                strip_layout_ = LayoutStrip(config_.strip.items, ColumnsOf(config_.strip, MeasureStrip(config_.strip)));
                Layout();
                return true;
            });
    };
    gap_input(s.kSetPadding, L"padding", &config::StripStyle::padding);
    // 各项间距改在上面的宽度条上拖（D-069），这里不再单列一项
    gap_input(s.kSetNetGap, L"net_gap", &config::StripStyle::net_gap);
    gap_input(s.kSetBarGap, L"bar_text_gap", &config::StripStyle::bar_text_gap);

    Header(s.kSetSecBehavior);
    Combo(
        kGlyphCursor, s.kSetStripClick, nullptr, {{s.kSetClickItem}, {s.kSetClickFlyout}},
        [this] { return config_.strip.click_item ? 0 : 1; },
        [this](int i) { Write(L"strip", L"click", i == 0 ? L"item" : L"flyout"); });
    // 位置（D-074）、所有任务栏（D-077）、网速单位（D-073）
    Combo(
        kGlyphStrip, s.kSetStripPosition, s.kSetStripPositionDesc, {{s.kSetPositionLeft}, {s.kSetPositionRight}},
        [this] { return static_cast<int>(config_.strip.position); },
        [this](int i) {
            Write(L"strip", L"position", i == 1 ? L"right" : L"left");
            Build();
        });
    Toggle(
        kGlyphStrip, s.kSetAllTaskbars, s.kSetAllTaskbarsDesc, [this] { return config_.strip.all_taskbars; },
        [this](bool on) { WriteInt(L"strip", L"all_taskbars", on ? 1 : 0); });
    Combo(
        kGlyphNetwork, s.kSetNetUnit, nullptr,
        {{s.kSetNetUnitAuto}, {s.kSetNetUnitBits}, {s.kSetNetUnitKilo}, {s.kSetNetUnitMega}},
        [this] { return static_cast<int>(config_.strip.net_unit); },
        [this](int i) {
            const wchar_t* values[] = {L"auto", L"bits", L"kb", L"mb"};
            Write(L"strip", L"net_unit", values[i]);
            Build();
        });
}

void SettingsWindow::BuildFlyout() {
    const Strings& s = Str();
    Header(s.kSetSecBehavior);
    Combo(
        kGlyphTiles, s.kSetOpenPanel, nullptr,
        {{s.kSetLastPanel},
         {s.kTileCpu},
         {s.kTileGpu},
         {s.kTileMemory},
         {s.kTileStorage},
         {s.kTileNetwork},
         {s.kTileSensors}},
        [this] { return config_.flyout.open_panel + 1; },
        [this](int i) { Write(L"flyout", L"open_panel", i == 0 ? L"last" : config::kPanelKeys[i - 1]); });
    Toggle(
        kGlyphCursor, s.kSetAutoClose, s.kSetAutoCloseDesc, [this] { return config_.flyout.auto_close; },
        [this](bool on) { WriteInt(L"flyout", L"auto_close", on ? 1 : 0); });
    NumberCombo(
        kGlyphList, s.kSetProcessRows, nullptr, {3, 4, 5, 6, 7, 8, 9, 10}, L"%d",
        [this] { return config_.flyout.process_rows; }, [this](int v) { WriteInt(L"flyout", L"process_rows", v); });
    // 趋势线时长（D-076）：磁贴和任务栏条迷你曲线覆盖多长时间
    static constexpr int kHistorySeconds[] = {60, 300, 900, 3600};
    Combo(
        kGlyphChart, s.kSetHistory, s.kSetHistoryDesc,
        {{s.kSetHistory1m}, {s.kSetHistory5m}, {s.kSetHistory15m}, {s.kSetHistory1h}},
        [this] {
            const auto it = std::find(std::begin(kHistorySeconds), std::end(kHistorySeconds),
                                      config_.flyout.history_seconds);
            return it == std::end(kHistorySeconds) ? -1 : static_cast<int>(it - std::begin(kHistorySeconds));
        },
        [this](int i) { WriteInt(L"flyout", L"history", kHistorySeconds[i]); });

    // 磁贴开关（D-072）：关掉的磁贴连同面板一起隐藏，最后一个关不掉
    Header(s.kSetSecTiles);
    const wchar_t* tiles[] = {s.kTileCpu, s.kTileGpu, s.kTileMemory, s.kTileStorage, s.kTileNetwork, s.kTileSensors};
    for (int i = 0; i < 6; ++i) {
        Toggle(
            kGlyphTiles, tiles[i], nullptr, [this, i] { return (config_.flyout.tiles & (1u << i)) != 0; },
            [this, i](bool on) {
                const unsigned next = on ? config_.flyout.tiles | (1u << i) : config_.flyout.tiles & ~(1u << i);
                if (next == 0) {
                    return;
                }
                wchar_t text[96];
                config::FormatTiles(static_cast<uint8_t>(next), text, std::size(text));
                Write(L"flyout", L"tiles", text);
            });
    }
}

float SettingsWindow::ToDisplayTemp(int celsius) const {
    return config_.sensors.fahrenheit ? std::round(static_cast<float>(celsius) * 9.0f / 5.0f + 32.0f)
                                      : static_cast<float>(celsius);
}

int SettingsWindow::FromDisplayTemp(int value) const {
    return config_.sensors.fahrenheit ? static_cast<int>(std::lround((value - 32) * 5.0 / 9.0)) : value;
}

void SettingsWindow::BuildSensors() {
    const Strings& s = Str();
    Item& driver = items_.emplace_back();
    driver.kind = Kind::Status;
    driver.glyph = kGlyphShield;
    driver.title = s.kSetPawnIo;
    driver.desc = s.kSetPawnIoDesc;
    driver.value = driver_ok_ ? s.kSetDriverOk : s.kSetDriverMissing;
    driver.good = driver_ok_;

    if (!driver_ok_) {
        driver.kind = Kind::Button;
        driver.value = s.kInstallPawnIo;
        driver.action = [this] {
            if (!app::LaunchPawnIoInstall(false)) {
                MessageBoxW(hwnd_, Str().kPawnIoFailed, Str().kSetPawnIo, MB_OK | MB_ICONWARNING);
            }
        };
    }

    Header(s.kSetSecTemperature);
    Combo(
        kGlyphSensors, s.kSetTempUnit, nullptr, {{s.kSetUnitCelsius}, {s.kSetUnitFahrenheit}},
        [this] { return config_.sensors.fahrenheit ? 1 : 0; },
        [this](int i) {
            Write(L"sensors", L"unit", i == 1 ? L"f" : L"c");
            Build();
        });
    // 阈值的范围是 30–120 ℃，华氏度下换算成 86–248 °F
    const bool f = config_.sensors.fahrenheit;
    const int low = static_cast<int>(ToDisplayTemp(30));
    const int high = static_cast<int>(ToDisplayTemp(120));
    const wchar_t* unit = f ? s.kSetFahrenheit : s.kSetCelsius;
    NumberInput(
        kGlyphSensors, s.kSetWarn, s.kSetWarnDesc, low, high, unit,
        [this] { return ToDisplayTemp(config_.sensors.warn); },
        [this](int v) { WriteInt(L"sensors", L"warn", FromDisplayTemp(v)); });
    NumberInput(
        kGlyphSensors, s.kSetHot, s.kSetHotDesc, low, high, unit,
        [this] { return ToDisplayTemp(config_.sensors.hot); },
        [this](int v) { WriteInt(L"sensors", L"hot", FromDisplayTemp(v)); });
    Toggle(
        kGlyphBell, s.kSetHotNotify, s.kSetHotNotifyDesc, [this] { return config_.sensors.notify; },
        [this](bool on) { WriteInt(L"sensors", L"notify", on ? 1 : 0); });

    // 显示与重命名（D-070）：每个传感器一行，勾选框管显示，输入框管别名
    sensors_shared_ = ReadSharedSensors(sensors_, sensor_count_);
    Item& list = items_.emplace_back();
    list.kind = Kind::Status;
    list.glyph = kGlyphList;
    list.title = s.kSetSecSensorList;
    list.desc = !sensors_shared_ ? s.kSetSensorsNotRunning : sensor_count_ == 0 ? s.kSetSensorsEmpty
                                                                                : s.kSetSensorListDesc;
    for (int i = 0; i < sensor_count_; ++i) {
        const SensorItem& sensor = sensors_[static_cast<size_t>(i)];
        const std::wstring key = sensor.key;
        Item& row = Input(
            0, sensor.name, nullptr, 180.0f, nullptr,
            [this, key] {
                const config::SensorSetting* setting = config::FindSensorSetting(config_.sensors, key.c_str());
                return std::wstring(setting ? setting->alias : L"");
            },
            [this, key](const std::wstring& text) {
                return WriteSensor(key.c_str(), config::SensorVisible(config_.sensors, key.c_str()), text.c_str());
            });
        row.check = true;
        row.get_on = [this, key] { return config::SensorVisible(config_.sensors, key.c_str()); };
        row.set_on = [this, key](bool on) {
            const config::SensorSetting* setting = config::FindSensorSetting(config_.sensors, key.c_str());
            const std::wstring alias = setting ? setting->alias : L"";
            WriteSensor(key.c_str(), on, alias.c_str());
        };
        wchar_t value[32];
        if (!sensor.value) {
            wcscpy_s(value, s.kUnavailable);
        } else if (sensor.fan) {
            _snwprintf_s(value, _TRUNCATE, s.kRpmFormat, static_cast<double>(*sensor.value));
        } else {
            FormatTemperature(*sensor.value, 1, value);
        }
        row.desc = value;
    }
}

void SettingsWindow::RefreshSensors() {
    if (page_ != Page::Sensors || open_combo_ >= 0) {
        return;
    }
    SensorList latest{};
    int count = 0;
    const bool shared = ReadSharedSensors(latest, count);
    // 传感器多了少了（比如浮窗打开后才有 ADLX 的几项）就重排；只是读数变了就只改说明文字
    bool same = shared == sensors_shared_ && count == sensor_count_;
    for (int i = 0; same && i < count; ++i) {
        same = wcscmp(latest[static_cast<size_t>(i)].key, sensors_[static_cast<size_t>(i)].key) == 0;
    }
    if (!same) {
        // 正在输入别名时不重排，免得输入框被换掉
        if (!focused_edit_) {
            Build();
        }
        return;
    }
    sensors_ = latest;
    int row = 0;
    for (Item& item : items_) {
        if (!item.check || row >= count) {
            continue;
        }
        const SensorItem& sensor = sensors_[static_cast<size_t>(row++)];
        wchar_t value[32];
        if (!sensor.value) {
            wcscpy_s(value, Str().kUnavailable);
        } else if (sensor.fan) {
            _snwprintf_s(value, _TRUNCATE, Str().kRpmFormat, static_cast<double>(*sensor.value));
        } else {
            FormatTemperature(*sensor.value, 1, value);
        }
        item.desc = value;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::vector<Option> SettingsWindow::AdapterOptions() const {
    const Strings& s = Str();
    std::vector<Option> options;
    wchar_t sub[64];
    _snwprintf_s(sub, _TRUNCATE, s.kNetMenuAutoSub, snapshot_->net_adapter_count);
    options.push_back({s.kNetMenuAuto, sub, L"", kGlyphLanguage});
    for (int i = 0; i < snapshot_->net_list_count; ++i) {
        const sampler::NetAdapter& a = snapshot_->net_list[static_cast<size_t>(i)];
        Option o{a.name, a.description, L"", !a.physical ? kGlyphVpn : a.wireless ? kGlyphWifi : kGlyphAdapter};
        if (!a.up) {
            o.meta = s.kNetNotConnected;
        } else {
            wchar_t rate[24];
            wchar_t meta[32];
            FormatRate(a.rx_bytes_per_sec, rate);
            _snwprintf_s(meta, _TRUNCATE, s.kDownloadLine, rate);
            o.meta = meta;
        }
        options.push_back(std::move(o));
    }
    return options;
}

int SettingsWindow::AdapterIndex() const {
    if (!config_.network.adapter) {
        return 0;
    }
    // 选中的网卡不在列表里（已拔掉）时哪一项都不选
    return snapshot_->net_selected >= 0 ? snapshot_->net_selected + 1 : -1;
}

void SettingsWindow::BuildNetwork() {
    const Strings& s = Str();
    Header(s.kSetSecAdapter);
    Item& radio = items_.emplace_back();
    radio.kind = Kind::Radio;
    radio.glyph = kGlyphNetwork;
    radio.title = s.kSetAdapter;
    radio.desc = s.kSetAdapterDesc;
    radio.options = AdapterOptions();
    radio.get_index = [this] { return AdapterIndex(); };
    radio.set_index = [this](int i) {
        wchar_t value[40] = L"";
        if (i > 0 && i - 1 < snapshot_->net_list_count) {
            config::FormatGuid(snapshot_->net_list[static_cast<size_t>(i - 1)].guid, value, std::size(value));
        }
        Write(L"network", L"adapter", value);
        net_->Configure(config_.network.adapter, config_.network.show_virtual);
        net_->Sample(*snapshot_);
    };
    Toggle(
        kGlyphVpn, s.kSetShowVirtual, s.kSetShowVirtualDesc, [this] { return config_.network.show_virtual; },
        [this](bool on) {
            WriteInt(L"network", L"show_virtual", on ? 1 : 0);
            net_->Configure(config_.network.adapter, config_.network.show_virtual);
            net_->Sample(*snapshot_);
            Build();
        });

    Header(s.kSetSecUsage);
    Toggle(
        kGlyphChart, s.kSetUsage, s.kSetUsageDesc, [this] { return config_.network.usage; },
        [this](bool on) { WriteInt(L"network", L"usage", on ? 1 : 0); });
    std::vector<int> days;
    for (int d = 1; d <= config::kMaxResetDay; ++d) {
        days.push_back(d);
    }
    NumberCombo(
        kGlyphCalendar, s.kSetResetDay, nullptr, days, s.kSetDayFormat, [this] { return config_.network.reset_day; },
        [this](int v) { WriteInt(L"network", L"reset_day", v); });

    Header(s.kSetSecLatency);
    Toggle(
        kGlyphStopwatch, s.kSetPing, s.kSetPingDesc, [this] { return config_.network.ping; },
        [this](bool on) { WriteInt(L"network", L"ping", on ? 1 : 0); });
    Input(
        kGlyphPin, s.kSetPingTarget, s.kSetPingTargetDesc, 200.0f, nullptr,
        [this] { return std::wstring(config_.network.ping_target); },
        [this](const std::wstring& text) {
            if (text.empty() || text.find(L' ') != std::wstring::npos) {
                return false;
            }
            Write(L"network", L"ping_target", text);
            return true;
        });
}

void SettingsWindow::BuildAbout() {
    const Strings& s = Str();
    wchar_t version[48];
    _snwprintf_s(version, _TRUNCATE, s.kSetVersionFormat, L"" PITOT_VERSION);
    Item& about = items_.emplace_back();
    about.kind = Kind::Status;
    about.glyph = kGlyphInfo;
    about.title = L"Pitot";
    about.desc = version;
    Item& license = items_.emplace_back();
    license.kind = Kind::Status;
    license.glyph = kGlyphDocument;
    license.title = s.kSetLicense;
    license.desc = s.kSetLicenseDesc;
    Item& third = items_.emplace_back();
    third.kind = Kind::Status;
    third.glyph = kGlyphCode;
    third.title = s.kSetThirdParty;
    third.desc = s.kSetThirdPartyDesc;
    Item& file = items_.emplace_back();
    file.kind = Kind::Button;
    file.glyph = kGlyphDocument;
    file.title = s.kSetLog;
    file.value = s.kSetViewLog;
    file.action = [this] {
        const std::wstring path = std::wstring(L"\"") + log_path_ + L"\"";
        if (!log_path_[0] || GetFileAttributesW(log_path_) == INVALID_FILE_ATTRIBUTES ||
            reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open", L"notepad.exe", path.c_str(), nullptr,
                                                    SW_SHOWNORMAL)) <= 32) {
            MessageBoxW(hwnd_, Str().kSetLogOpenFailed, Str().kSetLog, MB_OK | MB_ICONWARNING);
        }
    };
}

void SettingsWindow::RefreshAdapters() {
    net_->Sample(*snapshot_);
    if (page_ != Page::Network || open_combo_ >= 0) {
        return;
    }
    for (Item& item : items_) {
        if (item.kind != Kind::Radio) {
            continue;
        }
        std::vector<Option> options = AdapterOptions();
        if (options.size() != item.options.size()) {
            Build();  // 网卡多了或少了，重新排版
            return;
        }
        item.options = std::move(options);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------- 布局

void SettingsWindow::Layout() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float width = Dip(rc.right);
    const float height = Dip(rc.bottom);
    const float left = kNavWidth + kPadX;
    const float right = std::min(width - kPadX, left + kMaxCardWidth);
    float y = kPadTop + 36 + 20 - scroll_;
    for (size_t i = 0; i < items_.size(); ++i) {
        Item& item = items_[i];
        item.option_rects.clear();
        item.edit_control = {};
        item.track = {};
        item.stacked = (item.kind == Kind::Slider || item.kind == Kind::Color || item.kind == Kind::Font) &&
                       right - left < 520.0f;
        if (item.kind == Kind::Header) {
            y += i <= 1 ? 0 : 16;
            item.card = Rect(left, y, right, y + 20);
            item.control = {};
            y += 20 + 8;
            continue;
        }
        float h = kCardHeight;
        if (item.kind == Kind::Reorder) {
            h = kOrderPad + kChipH + kMeterGap + kMeterH + 6 + kMeterTextH + 10;
            if (StripNoteShown()) {
                h += kOrderHintH;
            }
        }
        if (item.stacked) {
            h += 40;
        }
        if (item.kind == Kind::Radio) {
            h += 1 + 6 + static_cast<float>(item.options.size()) * kOptionHeight + 8;
        }
        item.card = Rect(left, y, right, y + h);
        const float cy = y + (item.stacked ? 80.0f : kCardHeight / 2);
        const float cr = right - 16;
        switch (item.kind) {
        case Kind::Toggle: {
            const float text_w = std::max(TextWidth(Str().kSetOn, 14, DWRITE_FONT_WEIGHT_NORMAL),
                                          TextWidth(Str().kSetOff, 14, DWRITE_FONT_WEIGHT_NORMAL));
            item.control = Rect(cr - 40 - 12 - text_w, cy - 16, cr, cy + 16);
            break;
        }
        case Kind::Combo: {
            float w = 160;
            for (const Option& o : item.options) {
                w = std::max(w, TextWidth(o.label, 14, DWRITE_FONT_WEIGHT_NORMAL) + 12 + 10 + 12 + 12);
            }
            item.control = Rect(cr - w, cy - kControlHeight / 2, cr, cy + kControlHeight / 2);
            break;
        }
        case Kind::Input: {
            const float unit_w = item.unit.empty() ? 0 : TextWidth(item.unit, 14, DWRITE_FONT_WEIGHT_NORMAL) + 8;
            item.control = Rect(cr - unit_w - item.input_width, cy - kControlHeight / 2, cr - unit_w,
                                cy + kControlHeight / 2);
            item.edit_control = item.control;
            item.check_rect = item.check ? Rect(left + 20, cy - 10, left + 40, cy + 10) : D2D1_RECT_F{};
            break;
        }
        case Kind::Slider: {
            const float unit_w = item.unit.empty() ? 0 : TextWidth(item.unit, 14, DWRITE_FONT_WEIGHT_NORMAL) + 8;
            item.edit_control = Rect(cr - unit_w - 60, cy - kControlHeight / 2, cr - unit_w,
                                     cy + kControlHeight / 2);
            item.track = Rect(item.edit_control.left - 144, cy - 16, item.edit_control.left - 16, cy + 16);
            item.control = Rect(item.track.left, cy - 16, cr, cy + 16);
            break;
        }
        case Kind::Color: {
            item.edit_control = Rect(cr - 110, cy - 16, cr, cy + 16);
            item.control = Rect(item.edit_control.left - 44, cy - 16, cr, cy + 16);
            break;
        }
        case Kind::Font: {
            item.control = Rect(cr - item.input_width, cy - 16, cr, cy + 16);
            item.edit_control = Rect(item.control.left, item.control.top, item.control.right - 30, item.control.bottom);
            break;
        }
        case Kind::Button: {
            const float w = TextWidth(item.value, 14, DWRITE_FONT_WEIGHT_NORMAL) + 32;
            item.control = Rect(cr - w, cy - kControlHeight / 2, cr, cy + kControlHeight / 2);
            break;
        }
        case Kind::Undo: {
            const float top = kPadTop - scroll_;
            const float w = TextWidth(item.value, 14, DWRITE_FONT_WEIGHT_NORMAL) + 32;
            item.card = item.control = Rect(right - w, top + 2, right, top + 2 + kControlHeight);
            continue;
        }
        case Kind::Reorder: {
            // 现有的每一项一张小卡片，没满时末尾再加一张“添加”。卡片等宽，放不下时一起变窄
            item.control = {};
            const float row_left = left + kOrderPad;
            const float row_right = right - kOrderPad;
            const int slots = strip_count_ + (strip_count_ < kStripItemCount ? 1 : 0);
            const float fit =
                (row_right - row_left - static_cast<float>(std::max(0, slots - 1)) * kOrderGap) /
                static_cast<float>(std::max(1, slots));
            const float chip_w = std::min(kChipMaxW, fit);
            const float top = y + kOrderPad;
            for (int card = 0; card < slots; ++card) {
                const float x0 = row_left + static_cast<float>(card) * (chip_w + kOrderGap);
                item.option_rects.push_back(Rect(x0, top, x0 + chip_w, top + kChipH));
            }
            strip_remove_rect_ = {};
            if (strip_count_ > 1 && strip_selected_ < strip_count_) {
                const D2D1_RECT_F& chip = item.option_rects[static_cast<size_t>(strip_selected_)];
                strip_remove_rect_ = Rect(chip.right - 26, chip.top + 2, chip.right - 2, chip.top + 26);
            }
            const float meter_top = top + kChipH + kMeterGap;
            item.track = Rect(row_left, meter_top, row_right, meter_top + kMeterH);
            break;
        }
        case Kind::Radio: {
            item.control = {};
            float oy = y + kCardHeight + 1 + 6;
            for (size_t o = 0; o < item.options.size(); ++o) {
                item.option_rects.push_back(Rect(left + 56, oy, right - 16, oy + kOptionHeight));
                oy += kOptionHeight;
            }
            break;
        }
        default:
            item.control = {};
            break;
        }
        y += h + kCardGap;
    }
    content_height_ = y + scroll_ + kPadX;
    max_scroll_ = std::max(0.0f, content_height_ - height);
    if (scroll_ > max_scroll_) {
        scroll_ = max_scroll_;
        Layout();
        return;
    }
    PositionEdits();
}

void SettingsWindow::PositionEdits() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    for (const Item& item : items_) {
        if (!item.edit) {
            continue;
        }
        // 编辑框在外框里垂直居中；字的高度按 14 DIP 字号估，上下留白
        const D2D1_RECT_F& b = item.edit_control;
        const int x = static_cast<int>(Px(b.left + 10));
        const int w = static_cast<int>(Px(b.right - b.left - 20));
        const int h = static_cast<int>(Px(20));
        const int y = static_cast<int>(Px((b.top + b.bottom) / 2 - 10));
        // 下拉框展开时盖住的输入框先藏起来，否则子窗口会画在下拉框上面
        bool covered = false;
        if (open_combo_ >= 0) {
            const D2D1_RECT_F& c = combo_rect_;
            covered = b.right > c.left && b.left < c.right && b.bottom > c.top && b.top < c.bottom;
        }
        if (open_color_ >= 0) {
            const D2D1_RECT_F& c = color_rect_;
            covered = covered || (b.right > c.left && b.left < c.right && b.bottom > c.top && b.top < c.bottom);
        }
        const bool visible = !covered && Px(b.bottom) > 0 && Px(b.top) < static_cast<float>(rc.bottom);
        SetWindowPos(item.edit, nullptr, x, y, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    }
}

void SettingsWindow::DestroyEdits() {
    for (Item& item : items_) {
        if (item.edit) {
            // 销毁前先拿掉焦点，EN_KILLFOCUS 里的提交还能找到这一项
            if (GetFocus() == item.edit) {
                SetFocus(hwnd_);
            }
            RemoveWindowSubclass(item.edit, EditProc, kEditSubclassId);
            DestroyWindow(item.edit);
            item.edit = nullptr;
        }
    }
    focused_edit_ = nullptr;
}

void SettingsWindow::CommitEdit(HWND edit) {
    for (Item& item : items_) {
        if (item.edit != edit || !item.set_text) {
            continue;
        }
        wchar_t text[128];
        GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
        std::wstring value(text);
        // 去掉首尾空白
        const size_t first = value.find_first_not_of(L" \t");
        const size_t last = value.find_last_not_of(L" \t");
        value = first == std::wstring::npos ? L"" : value.substr(first, last - first + 1);
        if (value != item.get_text() && !item.set_text(value)) {
            log::Write(L"settings: rejected [%s] for %s", value.c_str(), item.title.c_str());
        }
        // 显示规范化以后的值（数值截断到范围、颜色写成 #RRGGBB）
        SetWindowTextW(edit, item.get_text().c_str());
        return;
    }
}

void SettingsWindow::FocusNextItem(int current, bool backwards, bool from_edit) {
    const int count = static_cast<int>(items_.size());
    if (!count) {
        return;
    }
    if (current >= 0 && current < count) {
        const Item& item = items_[static_cast<size_t>(current)];
        if ((item.kind == Kind::Slider || item.kind == Kind::Color) && item.edit && from_edit == backwards) {
            focused_item_ = current;
            focused_slider_ = item.kind == Kind::Slider && from_edit ? current : -1;
            SetFocus(from_edit ? hwnd_ : item.edit);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
    }
    int index = current < 0 || current >= count ? (backwards ? 0 : count - 1) : current;
    for (int n = 0; n < count; ++n) {
        index = (index + (backwards ? count - 1 : 1)) % count;
        const Item& item = items_[static_cast<size_t>(index)];
        if (item.kind == Kind::Header || item.kind == Kind::Status || (item.kind == Kind::Undo && !CanUndo())) {
            continue;
        }
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        if (item.card.top < 8) {
            scroll_ = std::max(0.0f, scroll_ + item.card.top - 8);
            Layout();
        } else if (item.card.bottom > Dip(rc.bottom) - 8) {
            scroll_ = std::min(max_scroll_, scroll_ + item.card.bottom - Dip(rc.bottom) + 8);
            Layout();
        }
        focused_item_ = index;
        focused_slider_ = item.kind == Kind::Slider ? index : -1;
        const bool use_edit = item.edit && (item.kind == Kind::Input || item.kind == Kind::Font ||
                                            (backwards && (item.kind == Kind::Slider || item.kind == Kind::Color)));
        SetFocus(use_edit ? item.edit : hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

int SettingsWindow::HitNav(float x, float y) const {
    if (x < 8 || x >= kNavWidth - 8) {
        return -1;
    }
    const float top = 8.0f;
    const int i = static_cast<int>((y - top) / (kNavItemHeight + 2));
    if (y < top || i >= kPageCount) {
        return -1;
    }
    return i;
}

void SettingsWindow::SetSliderFromPosition(int index, float x) {
    if (index < 0 || static_cast<size_t>(index) >= items_.size()) {
        return;
    }
    Item& item = items_[static_cast<size_t>(index)];
    const float left = item.track.left + 8;
    const float right = item.track.right - 8;
    const int value = SliderValueAt(x, left, right, item.minimum, item.maximum);
    if (std::abs(item.get_number() - static_cast<float>(value)) > 0.001f) {
        item.set_number(value);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::OpenCombo(int index) {
    if (index < 0 || static_cast<size_t>(index) >= items_.size()) {
        return;
    }
    const Item& item = items_[static_cast<size_t>(index)];
    if (item.options.empty()) {
        return;
    }
    open_color_ = -1;
    open_combo_ = index;
    combo_cursor_ = std::max(0, item.get_index());
    combo_first_ = std::max(0, combo_cursor_ - kMaxComboRows / 2);
    combo_hovered_ = -1;
    LayoutCombo();
    PositionEdits();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::LayoutCombo() {
    if (open_combo_ < 0) {
        return;
    }
    const Item& item = items_[static_cast<size_t>(open_combo_)];
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int count = static_cast<int>(item.options.size());
    const int available = std::max(1, static_cast<int>((Dip(rc.bottom) - 24) / kComboItemHeight));
    const int rows = std::min({count, kMaxComboRows, available});
    combo_first_ = ClampFirstOption(combo_first_, count, rows);
    const float h = static_cast<float>(rows) * kComboItemHeight + 8;
    const float top_below = item.control.bottom + 4;
    const float top = top_below + h <= Dip(rc.bottom) - 8 ? top_below
                      : std::max(8.0f, item.control.top - h - 4);
    const float width = std::max(180.0f, item.control.right - item.control.left);
    combo_rect_ = Rect(item.control.right - width, top, item.control.right, top + h);
    combo_rects_.clear();
    for (int i = 0; i < rows; ++i) {
        const float y = top + 4 + static_cast<float>(i) * kComboItemHeight;
        combo_rects_.push_back(Rect(combo_rect_.left + 4, y, combo_rect_.right - 4, y + kComboItemHeight));
    }
}

void SettingsWindow::OpenColor(int index) {
    open_combo_ = -1;
    open_color_ = index;
    color_hovered_ = 0;
    const Item& item = items_[static_cast<size_t>(index)];
    const auto current = config::ParseColor(item.get_text().c_str());
    if (current) {
        const auto match = std::find(kAccentColors.begin(), kAccentColors.end(), *current);
        if (match != kAccentColors.end()) {
            color_hovered_ = static_cast<int>(match - kAccentColors.begin());
        }
    }
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    constexpr float width = 272;
    constexpr float height = 208;
    const float below = item.control.bottom + 4;
    const float top = below + height <= Dip(rc.bottom) - 8 ? below : std::max(8.0f, item.control.top - height - 4);
    color_rect_ = Rect(item.control.right - width, top, item.control.right, top + height);
    color_rects_.clear();
    for (size_t i = 0; i < kAccentColors.size(); ++i) {
        const float x = color_rect_.left + 8 + static_cast<float>(i % 8) * 32;
        const float y = top + 8 + static_cast<float>(i / 8) * 32;
        color_rects_.push_back(Rect(x, y, x + 28, y + 28));
    }
    PositionEdits();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::OnClick(float x, float y) {
    if (open_color_ >= 0) {
        Item& item = items_[static_cast<size_t>(open_color_)];
        for (size_t i = 0; i < color_rects_.size(); ++i) {
            if (Contains(color_rects_[i], x, y)) {
                wchar_t hex[16];
                _snwprintf_s(hex, _TRUNCATE, L"#%06X", kAccentColors[i]);
                item.set_text(hex);
                break;
            }
        }
        open_color_ = -1;
        PositionEdits();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (open_combo_ >= 0) {
        // 下拉框开着：点选项就选中，点别处只关下拉框
        Item& item = items_[static_cast<size_t>(open_combo_)];
        for (size_t i = 0; i < combo_rects_.size(); ++i) {
            if (Contains(combo_rects_[i], x, y)) {
                open_combo_ = -1;
                const int option = combo_first_ + static_cast<int>(i);
                if (option != item.get_index()) {
                    const auto select = item.set_index;
                    select(option);
                }
                break;
            }
        }
        open_combo_ = -1;
        PositionEdits();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (const int nav = HitNav(x, y); nav >= 0) {
        if (static_cast<Page>(nav) != page_) {
            page_ = static_cast<Page>(nav);
            scroll_ = 0.0f;
            Build();
        }
        return;
    }
    for (size_t i = 0; i < items_.size(); ++i) {
        Item& item = items_[i];
        if (item.kind == Kind::Radio) {
            for (size_t o = 0; o < item.option_rects.size(); ++o) {
                if (Contains(item.option_rects[o], x, y) && static_cast<int>(o) != item.get_index()) {
                    const auto select = item.set_index;
                    select(static_cast<int>(o));
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    return;
                }
            }
            continue;
        }
        if (item.check && Contains(item.check_rect, x, y)) {
            const auto set = item.set_on;
            set(!item.get_on());
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (!Contains(item.control, x, y)) {
            continue;
        }
        switch (item.kind) {
        case Kind::Toggle:
        {
            const auto set = item.set_on;
            const bool on = item.get_on();
            set(!on);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        case Kind::Combo:
        case Kind::Font: {
            OpenCombo(static_cast<int>(i));
            return;
        }
        case Kind::Color:
            if (x < item.edit_control.left) {
                OpenColor(static_cast<int>(i));
            } else if (item.edit) {
                SetFocus(item.edit);
            }
            return;
        case Kind::Input:
        case Kind::Slider:
            if (item.edit) {
                SetFocus(item.edit);
                SendMessageW(item.edit, EM_SETSEL, 0, -1);
            }
            return;
        case Kind::Button:
            if (item.action) {
                item.action();
            }
            return;
        case Kind::Undo:
            if (CanUndo()) {
                UndoChanges();
            }
            return;
        default:
            return;
        }
    }
}

// ---------------------------------------------------------------- 绘制

bool SettingsWindow::EnsureTarget() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (target_ && width == surface_width_ && height == surface_height_) {
        return true;
    }
    ReleaseTarget();
    if (width <= 0 || height <= 0) {
        return false;
    }
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    dib_.reset(CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0));
    memory_dc_.reset(CreateCompatibleDC(nullptr));
    if (!dib_ || !memory_dc_) {
        ReleaseTarget();
        return false;
    }
    SelectObject(memory_dc_.get(), dib_.get());
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(dpi_), static_cast<float>(dpi_));
    HRESULT hr = d2d_->CreateDCRenderTarget(&props, target_.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = target_->BindDC(memory_dc_.get(), &rc);
    }
    if (SUCCEEDED(hr)) {
        hr = target_->CreateSolidColorBrush(D2D1::ColorF(0), brush_.GetAddressOf());
    }
    if (FAILED(hr)) {
        ReleaseTarget();
        return false;
    }
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    surface_width_ = width;
    surface_height_ = height;
    return true;
}

void SettingsWindow::ReleaseTarget() {
    brush_.Reset();
    target_.Reset();
    memory_dc_.reset();
    dib_.reset();
    surface_width_ = 0;
    surface_height_ = 0;
}

IDWriteTextFormat* SettingsWindow::Font(float size, DWRITE_FONT_WEIGHT weight, bool icon) {
    for (FontEntry& f : fonts_) {
        if (f.size == size && f.weight == weight && f.icon == icon) {
            return f.format.Get();
        }
    }
    FontEntry entry{size, weight, icon, nullptr};
    if (FAILED(dwrite_->CreateTextFormat(icon ? icon_family_ : font_family_, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", entry.format.GetAddressOf()))) {
        return nullptr;
    }
    entry.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    entry.format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // 太长的说明文字在末尾显示省略号
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(entry.format.Get(), &ellipsis))) {
        entry.format->SetTrimming(&trimming, ellipsis.Get());
    }
    fonts_.push_back(entry);
    return fonts_.back().format.Get();
}

void SettingsWindow::Fill(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius) {
    brush_->SetColor(c);
    if (radius > 0) {
        target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
    } else {
        target_->FillRectangle(r, brush_.Get());
    }
}

void SettingsWindow::Outline(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float radius) {
    brush_->SetColor(c);
    const D2D1_RECT_F inset = Rect(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f);
    target_->DrawRoundedRectangle(D2D1::RoundedRect(inset, radius, radius), brush_.Get(), 1.0f);
}

void SettingsWindow::Text(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight, const D2D1_RECT_F& r,
                          const D2D1_COLOR_F& c, DWRITE_TEXT_ALIGNMENT align) {
    IDWriteTextFormat* format = Font(size, weight);
    if (!format || text.empty()) {
        return;
    }
    format->SetTextAlignment(align);
    brush_->SetColor(c);
    target_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, r, brush_.Get(),
                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void SettingsWindow::Glyph(wchar_t glyph, float size, const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    IDWriteTextFormat* format = Font(size, DWRITE_FONT_WEIGHT_NORMAL, true);
    if (!format) {
        return;
    }
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    brush_->SetColor(c);
    target_->DrawText(&glyph, 1, format, r, brush_.Get());
}

float SettingsWindow::TextWidth(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight) {
    IDWriteTextFormat* format = Font(size, weight);
    ComPtr<IDWriteTextLayout> layout;
    if (!format || FAILED(dwrite_->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format, 10000,
                                                    100, &layout))) {
        return 0.0f;
    }
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void SettingsWindow::Render(HDC dc) {
    if (!EnsureTarget()) {
        return;
    }
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const float width = Dip(rc.right);
    const float height = Dip(rc.bottom);
    const Palette& p = palette_;
    target_->BeginDraw();
    target_->Clear(D2D1::ColorF(0, 0, 0, 0));
    // 保留全透明的背景像素，让 DWM 直接绘制 Mica；只有接口不可用时才画实色。
    if (!backdrop_available_) {
        Fill(Rect(0, 0, width, height), p.win);
    }

    // 左侧导航
    const Strings& s = Str();
    const wchar_t* nav_labels[] = {s.kSetNavGeneral, s.kSetNavStrip,   s.kSetNavFlyout,
                                   s.kSetNavSensors, s.kSetNavNetwork, s.kSetNavAbout};
    const wchar_t nav_glyphs[] = {kGlyphGeneral, kGlyphStrip, kGlyphFlyout, kGlyphSensors, kGlyphNetwork, kGlyphInfo};
    for (int i = 0; i < kPageCount; ++i) {
        const float y = 8 + static_cast<float>(i) * (kNavItemHeight + 2);
        const D2D1_RECT_F r = Rect(8, y, kNavWidth - 8, y + kNavItemHeight);
        const bool selected = static_cast<int>(page_) == i;
        if (selected) {
            Fill(r, p.nav_sel, 4);
            Fill(Rect(r.left, y + 10, r.left + 3, y + kNavItemHeight - 10), p.accent, 1.5f);
        } else if (hovered_nav_ == i) {
            Fill(r, p.hover, 4);
        }
        Glyph(nav_glyphs[i], 16, Rect(r.left + 15, y, r.left + 31, y + kNavItemHeight), p.text);
        Text(nav_labels[i], 14, selected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
             Rect(r.left + 43, y, r.right - 8, y + kNavItemHeight), p.text);
    }

    // 右侧内容区：圆角只在左上
    const D2D1_RECT_F content = Rect(kNavWidth, 0, width + 8, height + 8);
    Fill(content, p.content, 8);
    Outline(content, p.stroke, 8);
    target_->PushAxisAlignedClip(Rect(kNavWidth + 1, 1, width, height), D2D1_ANTIALIAS_MODE_ALIASED);
    const float left = kNavWidth + kPadX;
    const float title_right = page_ == Page::About || items_.empty() ? width - kPadX : items_.front().control.left - 12;
    Text(nav_labels[static_cast<int>(page_)], 28, DWRITE_FONT_WEIGHT_SEMI_BOLD,
         Rect(left, kPadTop - scroll_, title_right, kPadTop + 36 - scroll_), p.text);
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& item = items_[i];
        if (item.card.bottom < 0 || item.card.top > height) {
            continue;
        }
        DrawItem(item, static_cast<int>(i));
    }
    // 滚动条
    if (max_scroll_ > 0) {
        const float view = height;
        const float thumb = std::max(32.0f, view * view / content_height_);
        const float ty = (view - thumb) * (scroll_ / max_scroll_);
        D2D1_COLOR_F c = p.text2;
        c.a *= 0.45f;
        Fill(Rect(width - 6, ty + 4, width - 3, ty + thumb - 4), c, 1.5f);
    }
    target_->PopAxisAlignedClip();
    if (open_combo_ >= 0) {
        DrawCombo();
    }
    if (open_color_ >= 0) {
        DrawColor();
    }
    if (FAILED(target_->EndDraw())) {
        ReleaseTarget();
        return;
    }
    BitBlt(dc, 0, 0, surface_width_, surface_height_, memory_dc_.get(), 0, 0, SRCCOPY);
}

bool SettingsWindow::OrderCardAt(float x, float y, int& item, int& card) const {
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        if (it.kind != Kind::Reorder) {
            continue;
        }
        for (size_t c = 0; c < it.option_rects.size(); ++c) {
            if (Contains(it.option_rects[c], x, y)) {
                item = static_cast<int>(i);
                card = static_cast<int>(c);
                return true;
            }
        }
    }
    return false;
}

bool SettingsWindow::StripNoteShown() const {
    return !strip_note_.empty() || !StripFits(strip_layout_.content_width, strip_room_);
}

void SettingsWindow::SetStripSource(config::StripSource source) {
    config::StripStyle next = config_.strip;
    config::StripItemConfig& item = next.items[strip_selected_];
    if (item.source == source) {
        return;
    }
    // 换了来源，名称和竖条回到新来源的默认值：“显卡”这样的名称不该跟到 CPU 上
    item.source = source;
    item.bar = config::DefaultStripBar(source);
    item.label_set = false;
    item.label[0] = L'\0';
    CommitStripItems(next);
}

void SettingsWindow::SetStripTempSource(config::StripTempSource temp) {
    config::StripStyle next = config_.strip;
    next.items[strip_selected_].temp = temp;
    CommitStripItems(next);
}

bool SettingsWindow::SetStripLabel(const std::wstring& text) {
    config::StripStyle next = config_.strip;
    config::StripItemConfig& item = next.items[strip_selected_];
    std::wstring label;
    for (wchar_t c : text) {
        if (c != L',') {  // 逗号是 items 里各项的分隔符
            label.push_back(c);
        }
    }
    label = label.substr(0, std::size(item.label) - 1);
    // 和默认名称一样就当没改，切换界面语言时还能跟着换
    if (label == StripItemDefaultLabel(item)) {
        item.label_set = false;
        item.label[0] = L'\0';
    } else {
        item.label_set = true;
        wcsncpy_s(item.label, label.c_str(), _TRUNCATE);
    }
    // 正在提交输入框，不能重建页面（会销毁这个输入框）
    return CommitStripItems(next, false);
}

void SettingsWindow::AddStripItem() {
    if (strip_count_ >= kStripItemCount) {
        return;
    }
    // 先挑还没有的来源，再允许重复；都放不下时说明原因
    static constexpr config::StripItemConfig kCandidates[] = {
        {config::StripSource::Cpu, config::StripTempSource::Cpu, true},
        {config::StripSource::Memory, config::StripTempSource::Cpu, true},
        {config::StripSource::Net, config::StripTempSource::Cpu, false},
        {config::StripSource::Temp, config::StripTempSource::Cpu, false},
        {config::StripSource::Gpu, config::StripTempSource::Cpu, false},
        {config::StripSource::Temp, config::StripTempSource::Gpu, false},
        {config::StripSource::DiskActivity, config::StripTempSource::Cpu, true},
        {config::StripSource::Temp, config::StripTempSource::Storage, false},
        {config::StripSource::Temp, config::StripTempSource::Board, false},
        {config::StripSource::Disk, config::StripTempSource::Cpu, false},
    };
    const auto present = [this](const config::StripItemConfig& want) {
        return std::any_of(std::begin(config_.strip.items), std::end(config_.strip.items),
                           [&](const config::StripItemConfig& have) {
                               return have.source == want.source &&
                                      (want.source != config::StripSource::Temp || have.temp == want.temp);
                           });
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (const config::StripItemConfig& candidate : kCandidates) {
            if (pass == 0 && present(candidate)) {
                continue;
            }
            config::StripStyle next = config_.strip;
            next.items[strip_count_] = candidate;
            if (StripChoiceFits(next)) {
                strip_selected_ = strip_count_;
                CommitStripItems(next);
                return;
            }
        }
    }
    strip_note_ = config_.strip.position == config::StripPosition::Right ? Str().kSetStripTooWideIcons : Str().kSetStripTooWide;
    Build();
}

void SettingsWindow::RemoveStripItemAt(int index) {
    if (strip_count_ <= 1 || index < 0 || index >= strip_count_) {
        return;
    }
    config::StripStyle next = config_.strip;
    RemoveStripItem(next.items, index);
    strip_selected_ = std::min(index, strip_count_ - 2);
    strip_note_.clear();
    WriteStripItems(next.items);
    Build();
}

bool SettingsWindow::CommitStripItems(config::StripStyle next, bool rebuild) {
    config::CompactStripItems(next.items);
    const float width = LayoutStrip(next.items, ColumnsOf(next, MeasureStrip(next))).content_width;
    // 已经被裁掉时（比如加大了字号），只拒绝让它更宽的改动，变窄的照样放行
    if (!StripFits(width, strip_room_) && width > strip_layout_.content_width + 0.01f) {
        strip_note_ = config_.strip.position == config::StripPosition::Right ? Str().kSetStripTooWideIcons : Str().kSetStripTooWide;
    } else {
        strip_note_.clear();
        WriteStripItems(next.items);
    }
    if (rebuild) {
        Build();
    } else {
        strip_layout_ = LayoutStrip(config_.strip.items, ColumnsOf(config_.strip, MeasureStrip(config_.strip)));
        Layout();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return strip_note_.empty();
}

void SettingsWindow::WriteStripItems(const config::StripItemConfig (&items)[kStripItemCount]) {
    // 旧版的全局名称已经并进每一项（LoadConfig），写 items 时一并删掉，免得两处都有名称（D-068）
    WriteKey(L"strip", L"label_cpu", nullptr);
    WriteKey(L"strip", L"label_memory", nullptr);
    wchar_t text[config::kStripItemsTextChars];
    config::FormatStripItems(items, text, std::size(text));
    Write(L"strip", L"items", text);
}

int SettingsWindow::StripGapAt(float x, float y) const {
    if (strip_layout_.count < 2) {
        return -1;
    }
    const Item* row = nullptr;
    for (const Item& it : items_) {
        if (it.kind == Kind::Reorder) {
            row = &it;
        }
    }
    if (!row || row->track.right <= row->track.left) {
        return -1;
    }
    const D2D1_RECT_F& track = row->track;
    // 竖向比色条本身宽一截，窄缝也好抓住；上沿停在小卡片下面，不抢走卡片的拖动
    if (y < track.top - 14.0f || y >= track.bottom + 8.0f || x < track.left || x >= track.right) {
        return -1;
    }
    const float used = StripWindowWidth(strip_layout_.content_width);
    const float total = std::max({strip_room_, used, 1.0f});
    const float scale = (track.right - track.left) / total;
    if (scale <= 0.0f) {
        return -1;
    }
    const float content_x = (x - track.left) / scale - kStripFramePad;
    return StripGapIndexAt(strip_layout_, content_x, 8.0f / scale);
}

void SettingsWindow::SetStripGap(int gap_index, float pointer_x) {
    const Item* row = nullptr;
    for (const Item& it : items_) {
        if (it.kind == Kind::Reorder) {
            row = &it;
        }
    }
    if (!row || gap_index < 0 || gap_index + 1 >= strip_layout_.count) {
        return;
    }
    const float track_w = row->track.right - row->track.left;
    const float pointer = pointer_x - row->track.left + strip_gap_pointer_offset_;
    const float gap = GapFromMeterPointer(strip_layout_, gap_index, pointer, track_w, strip_room_);
    const float rounded = std::round(gap);
    const float shown = strip_layout_.spans[gap_index + 1].left - strip_layout_.spans[gap_index].right;
    if (rounded == std::round(shown)) {
        return;
    }
    // 只改这一段后面那一项自己的间距，其他段不动。和默认值一样时不单独记
    config::StripStyle next = config_.strip;
    config::StripItemConfig& item = next.items[strip_layout_.spans[gap_index + 1].item];
    const float fallback = IsRate(item.source) && next.variant == config::StripVariant::Stack ? next.net_gap : next.item_gap;
    if (rounded == fallback) {
        item.gap_set = false;
        item.gap = 0;
    } else {
        item.gap_set = true;
        item.gap = rounded;
    }
    const bool was_showing_note = StripNoteShown();
    strip_note_.clear();
    WriteStripItems(next.items);
    strip_layout_ = LayoutStrip(config_.strip.items, ColumnsOf(config_.strip, MeasureStrip(config_.strip)));
    if (was_showing_note != StripNoteShown()) {
        Layout();
    }
}

void SettingsWindow::DrawOrderCard(const D2D1_RECT_F& card, const config::StripItemConfig& item, bool selected,
                                   bool hover, bool floating) {
    const Palette& p = palette_;
    Fill(card, p.ctrl, 6);
    if (selected || floating) {
        D2D1_COLOR_F tint = p.accent;
        tint.a *= 0.14f;
        Fill(card, tint, 6);
    }
    if (hover && !floating) {
        Fill(card, p.hover, 6);
    }
    Outline(card, selected || floating ? p.accent : p.ctrl_stroke, 6);
    // 拖动把手：上方两列三行的小点
    const float cx = (card.left + card.right) / 2.0f;
    brush_->SetColor(p.text2);
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 2; ++row) {
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 5.0f + static_cast<float>(col) * 5.0f,
                                                            card.top + 10.0f + static_cast<float>(row) * 4.0f),
                                               1.1f, 1.1f),
                                 brush_.Get());
        }
    }
    // 卡片写来源（“CPU 温度”），不写自定义名称：名称可以留空，来源才认得出是哪一项
    Text(StripItemName(item), 13, DWRITE_FONT_WEIGHT_SEMI_BOLD,
         Rect(card.left + 6, card.top + 22, card.right - 6, card.bottom - 6), p.text, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void SettingsWindow::DrawAddCard(const D2D1_RECT_F& card, bool hover) {
    const Palette& p = palette_;
    if (hover) {
        Fill(card, p.hover, 6);
    }
    Outline(card, p.ctrl_stroke, 6);
    Glyph(kGlyphAdd, 12, Rect(card.left, card.top + 6, card.right, card.top + 26), p.text2);
    Text(Str().kSetStripAdd, 13, DWRITE_FONT_WEIGHT_NORMAL,
         Rect(card.left + 6, card.top + 22, card.right - 6, card.bottom - 6), p.text2, DWRITE_TEXT_ALIGNMENT_CENTER);
}

// 宽度条：整条是开始按钮前的空白，每一项按真实宽度占一段，选中的那一项用强调色。
// 超出的部分和开始按钮的位置用红色标出
void SettingsWindow::DrawStripMeter(const Item& item) {
    const Palette& p = palette_;
    const D2D1_RECT_F& track = item.track;
    const float used = StripWindowWidth(strip_layout_.content_width);
    const float total = std::max({strip_room_, used, 1.0f});
    const float scale = (track.right - track.left) / total;
    D2D1_COLOR_F groove = p.text;
    groove.a = 0.08f;
    Fill(track, groove, kMeterH / 2);
    D2D1_COLOR_F other = p.text2;
    other.a *= 0.55f;
    const float room_x = track.left + strip_room_ * scale;
    for (int i = 0; i < strip_layout_.count; ++i) {
        const StripSpan& span = strip_layout_.spans[i];
        const float x0 = track.left + (kStripFramePad + span.left) * scale;
        const float x1 = std::max(x0 + 2.0f, track.left + (kStripFramePad + span.right) * scale);
        const D2D1_COLOR_F& color = span.item == strip_selected_ ? p.accent : other;
        Fill(Rect(x0, track.top, std::min(x1, room_x), track.bottom), color, 1.5f);
        if (x1 > room_x) {
            Fill(Rect(std::max(x0, room_x), track.top, x1, track.bottom), p.bad, 1.5f);
        }
    }
    if (used > strip_room_ + 0.01f) {
        Fill(Rect(room_x - 1.0f, track.top - 4.0f, room_x + 1.0f, track.bottom + 4.0f), p.bad);
    }
    // 间距是可以拖的：悬停或拖动时在这段空白上画出把手和当前 px
    const int active_gap = strip_gap_ >= 0 ? strip_gap_ : strip_gap_hover_;
    if (active_gap >= 0 && active_gap + 1 < strip_layout_.count) {
        const StripSpan& left_span = strip_layout_.spans[active_gap];
        const StripSpan& right_span = strip_layout_.spans[active_gap + 1];
        const float mid =
            track.left + (kStripFramePad + (left_span.right + right_span.left) / 2.0f) * scale;
        Fill(Rect(mid - 1.0f, track.top - 4.0f, mid + 1.0f, track.bottom + 4.0f), p.accent);
        wchar_t gap_text[24];
        _snwprintf_s(gap_text, _TRUNCATE, L"%.6g", static_cast<double>(right_span.left - left_span.right));
        const float half = TextWidth(gap_text, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD) / 2 + 2;
        Text(gap_text, 12, DWRITE_FONT_WEIGHT_SEMI_BOLD, Rect(mid - half, track.top - 18, mid + half, track.top - 2),
             p.text, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    const float text_top = track.bottom + 6.0f;
    const D2D1_RECT_F line = Rect(track.left, text_top, track.right, text_top + kMeterTextH);
    wchar_t text[64];
    _snwprintf_s(text, _TRUNCATE, Str().kSetStripUsed, static_cast<double>(used));
    Text(text, 12, DWRITE_FONT_WEIGHT_NORMAL, line, p.text2);
    _snwprintf_s(text, _TRUNCATE, config_.strip.position == config::StripPosition::Right ? Str().kSetStripRoomIcons : Str().kSetStripRoom,
                 static_cast<int>(std::lround(strip_room_)));
    Text(text, 12, DWRITE_FONT_WEIGHT_NORMAL, line, p.text2, DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (StripNoteShown()) {
        const std::wstring& note = strip_note_.empty() ? std::wstring(config_.strip.position == config::StripPosition::Right ? Str().kSetStripClippedIcons : Str().kSetStripClipped) : strip_note_;
        Text(note, 12, DWRITE_FONT_WEIGHT_NORMAL, Rect(track.left, line.bottom + 2, track.right, line.bottom + 20),
             p.bad);
    }
}

void SettingsWindow::DrawStripOrder(const Item& item, int index) {
    const Palette& p = palette_;
    Fill(item.card, p.card, 6);
    Outline(item.card, p.card_stroke, 6);
    const bool dragging = order_dragging_ && index == order_item_;
    const config::StripItemConfig* order = dragging ? order_preview_ : config_.strip.items;
    const config::StripItemConfig dragged = dragging ? order_base_[order_from_] : config::StripItemConfig{};
    const int count = static_cast<int>(item.option_rects.size());
    for (int i = 0; i < count; ++i) {
        const bool hover = !dragging && hovered_item_ == index && hovered_option_ == i;
        const D2D1_RECT_F& rect = item.option_rects[static_cast<size_t>(i)];
        if (i >= strip_count_) {
            DrawAddCard(rect, hover);
            continue;
        }
        if (dragging && i == order_to_) {
            continue;  // 被拖的那一张跟着指针画
        }
        DrawOrderCard(rect, order[i], !dragging && i == strip_selected_, hover, false);
    }
    // 选中卡片右上角的 ×：移除这一项。只剩一项时不给移除
    if (!dragging && strip_remove_rect_.right > strip_remove_rect_.left) {
        if (strip_remove_hover_) {
            Fill(strip_remove_rect_, p.hover, 4);
        }
        Glyph(kGlyphRemove, 10, strip_remove_rect_, strip_remove_hover_ ? p.text : p.text2);
    }
    if (dragging && strip_count_ > 0) {
        const D2D1_RECT_F& home = item.option_rects[0];
        const float width = home.right - home.left;
        const float min_left = home.left;
        const float max_left = item.option_rects[static_cast<size_t>(strip_count_ - 1)].right - width;
        const float left = std::clamp(order_pointer_x_ - width / 2.0f, min_left, std::max(min_left, max_left));
        DrawOrderCard(Rect(left, home.top, left + width, home.bottom), dragged, true, false, true);
    }
    DrawStripMeter(item);
}

void SettingsWindow::DrawItem(const Item& item, int index) {
    const Palette& p = palette_;
    if (item.kind == Kind::Header) {
        Text(item.title, 14, DWRITE_FONT_WEIGHT_SEMI_BOLD, item.card, p.text);
        return;
    }
    if (item.kind == Kind::Reorder) {
        DrawStripOrder(item, index);
        return;
    }
    if (item.kind == Kind::Undo) {
        const bool enabled = CanUndo();
        const D2D1_RECT_F& r = item.control;
        Fill(r, enabled && hovered_item_ == index ? p.hover : p.ctrl, 4);
        Outline(r, enabled && focused_item_ == index && GetFocus() == hwnd_ ? p.accent : p.ctrl_stroke, 4);
        Text(item.value, 14, DWRITE_FONT_WEIGHT_NORMAL, r, enabled ? p.text : p.text2, DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }
    Fill(item.card, p.card, 6);
    Outline(item.card, p.card_stroke, 6);
    const float top = item.card.top;
    const float cy = top + kCardHeight / 2;
    if (item.check) {
        // Win11 复选框：勾上是强调色实心方块加白勾，没勾是细边框
        const D2D1_RECT_F& box = item.check_rect;
        if (item.get_on()) {
            Fill(box, p.accent, 4);
            Glyph(kGlyphCheck, 12, box, p.on_accent);
        } else {
            Outline(box, p.off_ring, 4);
        }
    } else {
        Glyph(item.glyph, 18, Rect(item.card.left + 20, top, item.card.left + 40, top + kCardHeight), p.text);
    }
    // 标题和说明的右边界：让出控件的位置
    float text_right = item.card.right - 16;
    if (!item.stacked && item.control.right > item.control.left) {
        text_right = item.control.left - 16;
    }
    if (item.kind == Kind::Status && !item.value.empty()) {
        text_right -= TextWidth(item.value, 12, DWRITE_FONT_WEIGHT_NORMAL) + 40;
    }
    const float tx = item.card.left + 56;
    if (item.desc.empty()) {
        Text(item.title, 14, DWRITE_FONT_WEIGHT_NORMAL, Rect(tx, top, text_right, top + kCardHeight), p.text);
    } else {
        Text(item.title, 14, DWRITE_FONT_WEIGHT_NORMAL, Rect(tx, cy - 19, text_right, cy + 1), p.text);
        Text(item.desc, 12, DWRITE_FONT_WEIGHT_NORMAL, Rect(tx, cy + 1, text_right, cy + 18), p.text2);
    }

    const bool hovered = hovered_item_ == index && hovered_option_ < 0;
    const D2D1_RECT_F& c = item.control;
    switch (item.kind) {
    case Kind::Toggle: {
        const bool on = item.get_on();
        const D2D1_RECT_F track = Rect(c.right - 40, cy - 10, c.right, cy + 10);
        Text(on ? Str().kSetOn : Str().kSetOff, 14, DWRITE_FONT_WEIGHT_NORMAL, Rect(c.left, c.top, track.left - 12, c.bottom),
             p.text2, DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (on) {
            Fill(track, p.accent, 10);
        } else {
            if (hovered) {
                Fill(track, p.hover, 10);
            }
            Outline(track, p.off_ring, 10);
        }
        const float knob = hovered ? 7.0f : 6.0f;
        const float kx = on ? track.right - 10 : track.left + 10;
        brush_->SetColor(on ? p.on_accent : p.off_ring);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, cy), knob, knob), brush_.Get());
        break;
    }
    case Kind::Combo: {
        Fill(c, p.ctrl, 4);
        if (hovered) {
            Fill(c, p.hover, 4);
        }
        Outline(c, p.ctrl_stroke, 4);
        const int i = item.get_index();
        const std::wstring label = i >= 0 && i < static_cast<int>(item.options.size())
                                       ? item.options[static_cast<size_t>(i)].label
                                       : std::wstring(Str().kUnavailable);
        Text(label, 14, DWRITE_FONT_WEIGHT_NORMAL, Rect(c.left + 12, c.top, c.right - 30, c.bottom), p.text);
        Glyph(kGlyphChevronDown, 10, Rect(c.right - 26, c.top, c.right - 12, c.bottom), p.text2);
        break;
    }
    case Kind::Input:
    case Kind::Slider:
    case Kind::Color:
    case Kind::Font: {
        const D2D1_RECT_F& input = item.kind == Kind::Font ? item.control : item.edit_control;
        if (item.kind == Kind::Slider) {
            const float slider_y = (item.track.top + item.track.bottom) / 2;
            const float left = item.track.left + 8;
            const float right = item.track.right - 8;
            const float value = std::clamp((item.get_number() - static_cast<float>(item.minimum)) /
                                               static_cast<float>(item.maximum - item.minimum),
                                           0.0f, 1.0f);
            const float thumb = left + (right - left) * value;
            D2D1_COLOR_F track_color = p.off_ring;
            track_color.a *= 0.55f;
            Fill(Rect(left, slider_y - 2, right, slider_y + 2), track_color, 2);
            if (thumb > left) {
                Fill(Rect(left, slider_y - 2, thumb, slider_y + 2), p.accent, 2);
            }
            brush_->SetColor(p.ctrl);
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumb, slider_y), 10, 10), brush_.Get());
            brush_->SetColor(p.accent);
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumb, slider_y), hovered ? 6.5f : 6.0f,
                                                hovered ? 6.5f : 6.0f), brush_.Get());
            if (focused_slider_ == index && GetFocus() == hwnd_) {
                Outline(Rect(item.track.left, item.track.top + 1, item.track.right, item.track.bottom - 1), p.accent, 4);
            }
        } else if (item.kind == Kind::Color) {
            const D2D1_RECT_F button = Rect(c.left, c.top, item.edit_control.left - 8, c.bottom);
            Fill(button, p.ctrl, 4);
            Outline(button, p.ctrl_stroke, 4);
            const auto rgb = config::ParseColor(item.get_text().c_str());
            Fill(Rect(button.left + 5, button.top + 5, button.right - 5, button.bottom - 5),
                 Rgba(rgb.value_or(0)), 3);
        }
        const bool focused = item.edit && item.edit == focused_edit_;
        Fill(input, p.ctrl, 4);
        Outline(input, p.ctrl_stroke, 4);
        // Win11 输入框：底边一条线，获得焦点时变成 2 px 强调色
        if (focused) {
            Fill(Rect(input.left + 1, input.bottom - 2, input.right - 1, input.bottom), p.accent);
        } else {
            Fill(Rect(input.left + 1, input.bottom - 1, input.right - 1, input.bottom), p.ctrl_bottom);
        }
        // 编辑框藏起来时（被下拉框盖住）也看得到值
        Text(item.get_text(), 14, DWRITE_FONT_WEIGHT_NORMAL,
             Rect(item.edit_control.left + 10, item.edit_control.top, item.edit_control.right - 10,
                  item.edit_control.bottom), p.text);
        if (item.kind == Kind::Font) {
            Glyph(kGlyphChevronDown, 10, Rect(c.right - 26, c.top, c.right - 12, c.bottom), p.text2);
        }
        if (!item.unit.empty()) {
            Text(item.unit, 14, DWRITE_FONT_WEIGHT_NORMAL,
                 Rect(item.edit_control.right + 8, c.top, item.card.right - 16, c.bottom), p.text2);
        }
        break;
    }
    case Kind::Button:
        Fill(c, p.ctrl, 4);
        if (hovered) {
            Fill(c, p.hover, 4);
        }
        Outline(c, p.ctrl_stroke, 4);
        Text(item.value, 14, DWRITE_FONT_WEIGHT_NORMAL, c, p.text, DWRITE_TEXT_ALIGNMENT_CENTER);
        break;
    case Kind::Status:
        if (!item.value.empty()) {
            const float w = TextWidth(item.value, 12, DWRITE_FONT_WEIGHT_NORMAL) + 30;
            const D2D1_RECT_F badge = Rect(item.card.right - 16 - w, cy - 12, item.card.right - 16, cy + 12);
            Fill(badge, item.good ? p.ok_bg : p.bad_bg, 12);
            brush_->SetColor(item.good ? p.ok : p.bad);
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(badge.left + 13, cy), 3, 3), brush_.Get());
            Text(item.value, 12, DWRITE_FONT_WEIGHT_NORMAL, Rect(badge.left + 22, badge.top, badge.right, badge.bottom),
                 item.good ? p.ok_text : p.bad_text);
        }
        break;
    case Kind::Radio: {
        Fill(Rect(item.card.left + 1, top + kCardHeight, item.card.right - 1, top + kCardHeight + 1), p.card_stroke);
        const int selected = item.get_index();
        for (size_t o = 0; o < item.options.size(); ++o) {
            const Option& opt = item.options[o];
            const D2D1_RECT_F& r = item.option_rects[o];
            const float oy = (r.top + r.bottom) / 2;
            const bool on = static_cast<int>(o) == selected;
            const bool hover = hovered_item_ == index && hovered_option_ == static_cast<int>(o);
            // 单选圈：选中是强调色实心环，没选是细圈
            if (on) {
                brush_->SetColor(p.accent);
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 10, oy), 10, 10), brush_.Get());
                brush_->SetColor(p.on_accent);
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 10, oy), hover ? 5.5f : 5, hover ? 5.5f : 5),
                                     brush_.Get());
            } else {
                brush_->SetColor(p.off_ring);
                target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 10, oy), 9.5f, 9.5f), brush_.Get(), 1.0f);
                if (hover) {
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 10, oy), 5, 5), brush_.Get());
                }
            }
            const float meta_w = opt.meta.empty() ? 0 : TextWidth(opt.meta, 13, DWRITE_FONT_WEIGHT_NORMAL) + 16;
            Glyph(opt.glyph, 14, Rect(r.left + 32, r.top, r.left + 48, r.bottom), p.text2);
            Text(opt.label, 14, DWRITE_FONT_WEIGHT_NORMAL, Rect(r.left + 58, oy - 18, r.right - meta_w, oy + 1), p.text);
            Text(opt.sub, 12, DWRITE_FONT_WEIGHT_NORMAL, Rect(r.left + 58, oy + 1, r.right - meta_w, oy + 17), p.text2);
            if (!opt.meta.empty()) {
                Text(opt.meta, 13, DWRITE_FONT_WEIGHT_NORMAL, Rect(r.right - meta_w, r.top, r.right, r.bottom), p.text2,
                     DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
        }
        break;
    }
    default:
        break;
    }
    if (focused_item_ == index && GetFocus() == hwnd_ && item.kind != Kind::Slider) {
        const D2D1_RECT_F focus = item.kind == Kind::Radio ? item.card : item.control;
        if (focus.right > focus.left) {
            Outline(Rect(focus.left - 2, focus.top - 2, focus.right + 2, focus.bottom + 2), p.accent, 5);
        }
    }
}

void SettingsWindow::DrawCombo() {
    const Palette& p = palette_;
    const Item& item = items_[static_cast<size_t>(open_combo_)];
    Fill(combo_rect_, p.menu, 8);
    Outline(combo_rect_, p.menu_stroke, 8);
    const int selected = item.get_index();
    for (size_t o = 0; o < combo_rects_.size(); ++o) {
        const int index = combo_first_ + static_cast<int>(o);
        const D2D1_RECT_F& r = combo_rects_[o];
        if (index == combo_hovered_ || index == combo_cursor_) {
            Fill(r, p.hover, 4);
        }
        if (index == selected) {
            Fill(Rect(r.left, r.top + 10, r.left + 3, r.bottom - 10), p.accent, 1.5f);
        }
        Text(item.options[static_cast<size_t>(index)].label, 14, DWRITE_FONT_WEIGHT_NORMAL,
             Rect(r.left + 12, r.top, r.right - 12, r.bottom), p.text);
    }
    if (item.options.size() > combo_rects_.size()) {
        const float height = combo_rect_.bottom - combo_rect_.top - 8;
        const float thumb = std::max(24.0f, height * static_cast<float>(combo_rects_.size()) /
                                               static_cast<float>(item.options.size()));
        const float top = combo_rect_.top + 4 +
                          (height - thumb) * static_cast<float>(combo_first_) /
                              static_cast<float>(item.options.size() - combo_rects_.size());
        Fill(Rect(combo_rect_.right - 5, top, combo_rect_.right - 2, top + thumb), p.text2, 1.5f);
    }
}

void SettingsWindow::DrawColor() {
    const Palette& p = palette_;
    Fill(color_rect_, p.menu, 8);
    Outline(color_rect_, p.menu_stroke, 8);
    const auto selected = config::ParseColor(items_[static_cast<size_t>(open_color_)].get_text().c_str());
    for (size_t i = 0; i < color_rects_.size(); ++i) {
        const D2D1_RECT_F& r = color_rects_[i];
        Fill(r, Rgba(kAccentColors[i]), 4);
        if (selected && *selected == kAccentColors[i]) {
            const D2D1_COLOR_F white = Rgba(0xFFFFFF);
            const D2D1_COLOR_F black = Rgba(0x000000, 0.8f);
            Outline(Rect(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2), black, 3);
            Outline(Rect(r.left + 3, r.top + 3, r.right - 3, r.bottom - 3), white, 3);
        } else if (static_cast<int>(i) == color_hovered_) {
            Outline(Rect(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2), Rgba(0xFFFFFF), 3);
        }
    }
}

}  // namespace

int RunSettings(HINSTANCE instance) {
    // 异常不能穿出入口（AGENTS.md）
    try {
        // 已经开着就把它提到前面
        unique_handle mutex(CreateMutexW(nullptr, FALSE, names::kSettingsMutex));
        if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (HWND existing = FindWindowW(names::kSettingsWindowClass, nullptr)) {
                if (IsIconic(existing)) {
                    ShowWindow(existing, SW_RESTORE);
                }
                SetForegroundWindow(existing);
            }
            return 0;
        }
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);
        auto window = std::make_unique<SettingsWindow>();
        return window->Run(instance);
    } catch (const std::exception& e) {
        log::Write(L"settings: stopped by exception: %S", e.what());
        return 1;
    } catch (...) {
        log::Write(L"settings: stopped by exception");
        return 1;
    }
}

}  // namespace pitot::ui
