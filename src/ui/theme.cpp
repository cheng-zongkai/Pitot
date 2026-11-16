#include "ui/theme.h"

#include <windows.h>

#include <cstdint>
#include <optional>

namespace pitot::ui {
namespace {

constexpr wchar_t kPersonalizeKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kAccentKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";

// AccentPalette 的 8 个颜色依次是 Light3、Light2、Light1、Base、Dark1、Dark2、Dark3、（未用）。
// 与 WinUI 的 AccentFillColorDefault 一致：深色主题用 Light2，浅色主题用 Dark1。
constexpr int kPaletteLight2 = 1;
constexpr int kPaletteDark1 = 4;
constexpr int kPaletteDark2 = 5;

constexpr D2D1_COLOR_F Rgba(uint32_t rgb, float alpha = 1.0f) {
    return {((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha};
}

bool SystemUsesLightTheme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, kPersonalizeKey, L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr,
                        &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

std::optional<D2D1_COLOR_F> AccentFromPalette(int index) {
    BYTE palette[32];
    DWORD size = sizeof(palette);
    if (RegGetValueW(HKEY_CURRENT_USER, kAccentKey, L"AccentPalette", RRF_RT_REG_BINARY, nullptr, palette, &size) !=
            ERROR_SUCCESS ||
        size < sizeof(palette)) {
        return std::nullopt;
    }
    const BYTE* c = palette + index * 4;
    return D2D1_COLOR_F{c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, 1.0f};
}

}  // namespace

namespace {

D2D1_COLOR_F WithAlpha(D2D1_COLOR_F c, float alpha) {
    c.a = alpha;
    return c;
}

// 配置里的强调色，按深浅色挑选；没配时返回空
std::optional<uint32_t> CustomAccent(const config::GeneralConfig& general, bool light) {
    if (light) {
        return general.accent_light ? general.accent_light : general.accent;
    }
    return general.accent_dark ? general.accent_dark : general.accent;
}

}  // namespace

StripTheme LoadStripTheme(const config::GeneralConfig& general) {
    StripTheme t;
    t.light = SystemUsesLightTheme();
    const std::optional<uint32_t> custom = CustomAccent(general, t.light);
    if (t.light) {
        t.text = Rgba(0x1A1A1A);
        t.text2 = Rgba(0x000000, 0.64f);
        t.accent = custom ? Rgba(*custom) : AccentFromPalette(kPaletteDark1).value_or(Rgba(0x005FB8));
        t.track = Rgba(0x000000, 0.12f);
        t.hover = Rgba(0x000000, 0.05f);
        t.warm = Rgba(0x9D5D00);
        t.hot = Rgba(0xC42B1C);
    } else {
        t.text = Rgba(0xFFFFFF);
        t.text2 = Rgba(0xFFFFFF, 0.66f);
        t.accent = custom ? Rgba(*custom) : AccentFromPalette(kPaletteLight2).value_or(Rgba(0x60CDFF));
        t.track = Rgba(0xFFFFFF, 0.14f);
        t.hover = Rgba(0xFFFFFF, 0.07f);
        t.warm = Rgba(0xFCB84C);
        t.hot = Rgba(0xFF99A4);
    }
    return t;
}

FlyoutTheme LoadFlyoutTheme(const config::GeneralConfig& general) {
    FlyoutTheme t;
    switch (general.theme) {
    case config::ThemeMode::Light:
        t.dark = false;
        break;
    case config::ThemeMode::Dark:
        t.dark = true;
        break;
    default:
        t.dark = !SystemUsesLightTheme();
        break;
    }
    const std::optional<uint32_t> custom = CustomAccent(general, !t.dark);

    // 底色用设计稿的颜色但留一半透明，让 Acrylic 的模糊和噪点透出来
    constexpr float kPanelAlpha = 0.55f;
    if (t.dark) {
        t.panel = Rgba(0x272727, kPanelAlpha);
        t.footer = Rgba(0x202020, kPanelAlpha);
        t.stroke = Rgba(0xFFFFFF, 0.08f);
        t.card = Rgba(0xFFFFFF, 0.05f);
        t.card_stroke = Rgba(0xFFFFFF, 0.07f);
        t.text = Rgba(0xFFFFFF);
        t.text2 = Rgba(0xFFFFFF, 0.79f);
        t.text3 = Rgba(0xFFFFFF, 0.58f);
        t.accent = custom ? Rgba(*custom) : AccentFromPalette(kPaletteLight2).value_or(Rgba(0x60CDFF));
        t.accent_text = t.accent;
        t.on_accent = Rgba(0x000000);
        t.hover = Rgba(0xFFFFFF, 0.06f);
        t.menu = Rgba(0x2C2C2C);
        t.menu_stroke = Rgba(0x000000, 0.4f);
        t.track = Rgba(0xFFFFFF, 0.09f);
        t.warm = Rgba(0xFCB84C);
        t.hot = Rgba(0xFF99A4);
        t.ok = Rgba(0x6CCB5F);
        t.idle = Rgba(0xFFFFFF, 0.36f);
        const uint32_t process[] = {0x3A6EA5, 0x6B5B95, 0x287A4C, 0x9A5518, 0x526B7A, 0x8B4A62};
        for (int i = 0; i < 6; ++i) {
            t.process[i] = Rgba(process[i]);
        }
        t.sel_bg = WithAlpha(t.accent, 0.12f);
        t.sel_border = WithAlpha(t.accent, 0.7f);
    } else {
        t.panel = Rgba(0xF3F3F3, kPanelAlpha);
        t.footer = Rgba(0xEEEEEE, kPanelAlpha);
        t.stroke = Rgba(0x000000, 0.08f);
        t.card = Rgba(0xFFFFFF, 0.75f);
        t.card_stroke = Rgba(0x000000, 0.06f);
        t.text = Rgba(0x1A1A1A);
        t.text2 = Rgba(0x000000, 0.68f);
        t.text3 = Rgba(0x000000, 0.58f);
        t.accent = custom ? Rgba(*custom) : AccentFromPalette(kPaletteDark1).value_or(Rgba(0x005FB8));
        t.accent_text = custom ? Rgba(*custom) : AccentFromPalette(kPaletteDark2).value_or(Rgba(0x005A9E));
        t.on_accent = Rgba(0xFFFFFF);
        t.hover = Rgba(0x000000, 0.04f);
        t.menu = Rgba(0xF9F9F9);
        t.menu_stroke = Rgba(0x000000, 0.12f);
        t.track = Rgba(0x000000, 0.09f);
        t.warm = Rgba(0x9D5D00);
        t.hot = Rgba(0xC42B1C);
        t.ok = Rgba(0x0F7B0F);
        t.idle = Rgba(0x000000, 0.36f);
        const uint32_t process[] = {0x2F5F92, 0x5E4F86, 0x226B42, 0x8A4B14, 0x4A6170, 0x7D4258};
        for (int i = 0; i < 6; ++i) {
            t.process[i] = Rgba(process[i]);
        }
        t.sel_bg = WithAlpha(t.accent, 0.08f);
        t.sel_border = WithAlpha(t.accent, 0.6f);
    }
    return t;
}

}  // namespace pitot::ui
