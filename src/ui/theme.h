#pragma once

#include <d2d1.h>

#include "config/config.h"

namespace pitot::ui {

// 任务栏条配色，取值来自原设计稿“任务栏条组件”。
struct StripTheme {
    bool light = false;
    D2D1_COLOR_F text{};
    D2D1_COLOR_F text2{};   // 标签、上传行等次要文字
    D2D1_COLOR_F accent{};  // 占用竖条
    D2D1_COLOR_F track{};   // 竖条底槽
    D2D1_COLOR_F hover{};   // 悬停底色
    D2D1_COLOR_F warm{};    // 温度达到警告阈值
    D2D1_COLOR_F hot{};     // 温度达到高温阈值
};

// 深浅色跟随任务栏（“Windows 模式”），不受 general.theme 影响；
// 强调色优先用配置（accent_light / accent_dark，其次 accent），没配时用系统强调色
StripTheme LoadStripTheme(const config::GeneralConfig& general);

// 浮窗配色，取值来自设计稿“浮窗 · 深色 / 浅色”。
struct FlyoutTheme {
    bool dark = false;
    D2D1_COLOR_F panel{};  // 整体底色，半透明以透出 Acrylic
    D2D1_COLOR_F footer{};
    D2D1_COLOR_F stroke{};  // 分隔线
    D2D1_COLOR_F card{};
    D2D1_COLOR_F card_stroke{};
    D2D1_COLOR_F text{};
    D2D1_COLOR_F text2{};
    D2D1_COLOR_F text3{};
    D2D1_COLOR_F accent{};
    D2D1_COLOR_F accent_text{};  // 强调色文字（链接式按钮）
    D2D1_COLOR_F on_accent{};    // 强调色底上的文字
    D2D1_COLOR_F sel_bg{};       // 选中磁贴
    D2D1_COLOR_F sel_border{};
    D2D1_COLOR_F hover{};        // 按钮、磁贴悬停
    D2D1_COLOR_F menu{};         // 下拉菜单底色，不透明（盖在内容上）
    D2D1_COLOR_F menu_stroke{};
    D2D1_COLOR_F track{};        // 进度条底槽
    D2D1_COLOR_F warm{};         // 60–79 °C
    D2D1_COLOR_F hot{};          // ≥ 80 °C
    D2D1_COLOR_F ok{};           // 状态正常圆点
    D2D1_COLOR_F idle{};         // 状态未接入圆点
    D2D1_COLOR_F process[6]{};   // 进程图标底色
};

// general.theme 为 System 时跟随“Windows 模式”，与任务栏一致（D-024）
FlyoutTheme LoadFlyoutTheme(const config::GeneralConfig& general);

}  // namespace pitot::ui
