#pragma once

#include <algorithm>
#include <cmath>

namespace pitot::ui {

// 指针移到轨道以外时仍截到两端；字号、间距和温度的起点不相同。
inline int SliderValueAt(float position, float left, float right, int minimum, int maximum) noexcept {
    if (right <= left || maximum <= minimum) {
        return minimum;
    }
    const float fraction = std::clamp((position - left) / (right - left), 0.0f, 1.0f);
    return minimum + static_cast<int>(std::lround(fraction * static_cast<float>(maximum - minimum)));
}

// 长列表只画可见行，同时让键盘选中的项目留在视口内。
inline int ClampFirstOption(int first, int count, int visible) noexcept {
    return std::clamp(first, 0, std::max(0, count - std::max(0, visible)));
}

inline int ShowOption(int first, int selected, int count, int visible) noexcept {
    if (count <= 0 || visible <= 0) {
        return 0;
    }
    selected = std::clamp(selected, 0, count - 1);
    first = ClampFirstOption(first, count, visible);
    if (selected < first) {
        first = selected;
    } else if (selected >= first + visible) {
        first = selected - visible + 1;
    }
    return ClampFirstOption(first, count, visible);
}

}  // namespace pitot::ui
