#pragma once

#include <d2d1.h>

namespace pitot::ui {

// Pitot 单色标志（D-082）。坐标和 src/app/pitot-glyph.svg 一致，改图形时两边一起改。
constexpr float kLogoLeft = 40.0f;
constexpr float kLogoTop = 68.0f;
constexpr float kLogoWidth = 176.0f;
constexpr float kLogoHeight = 120.0f;

// 路径几何属于工厂而不是设备：和创建它的工厂一起建、一起放
HRESULT CreateLogoGeometry(ID2D1Factory* factory, ID2D1PathGeometry** geometry);

// 按宽高比放进 box 并居中，用 brush 当前的颜色填充；会临时改渲染目标的变换
void FillLogo(ID2D1RenderTarget* target, ID2D1Geometry* geometry, ID2D1Brush* brush, const D2D1_RECT_F& box);

}  // namespace pitot::ui
