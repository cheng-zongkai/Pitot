#pragma once

#include <algorithm>

// 各核心利用率的网格（D-031 的分列，图形是圆环，D-058）。不标核号（D-087）。
// 纯函数，有单元测试，不依赖窗口。

namespace pitot::ui {

struct CoreBarLayout {
    int cols = 0;         // 每行几个核
    int rows = 0;
    float group_w = 0;    // 每个核占的宽度
    float core_gap = 0;   // 核与核之间的空隙
};

inline constexpr int kCoreBarsPerRow = 16;

// width：可用宽度
inline CoreBarLayout LayoutCoreBars(int cores, float width) {
    CoreBarLayout l;
    if (cores <= 0 || width <= 0) {
        return l;
    }
    l.cols = std::min(cores, kCoreBarsPerRow);
    l.rows = (cores + l.cols - 1) / l.cols;
    // 核少时空隙大一些，分组更明显；核多时收紧，把宽度留给圆环
    l.core_gap = l.cols <= 8 ? 10.0f : 6.0f;
    l.group_w = (width - static_cast<float>(l.cols - 1) * l.core_gap) / static_cast<float>(l.cols);
    return l;
}

// 圆环直径不超过一格的宽度，核少时也不放大到 36 DIP 以上，避免一两颗核占满整行
inline float CoreRingDiameter(float group_w) {
    if (group_w <= 0.0f) {
        return 0.0f;
    }
    return std::min(group_w, 36.0f);
}

// 从 12 点方向顺时针的圆心角（度）。刚有负载时至少 8°，否则和空闲看不出差别；接近满载画整圆
inline float CoreRingSweepDegrees(float load_percent) {
    if (load_percent <= 0.5f) {
        return 0.0f;
    }
    if (load_percent >= 99.5f) {
        return 360.0f;
    }
    return std::max(8.0f, load_percent / 100.0f * 360.0f);
}

}  // namespace pitot::ui
