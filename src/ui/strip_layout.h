#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include "config/config.h"

// 任务栏条显示项的排布（D-066、D-068）。纯函数，有单元测试，不依赖窗口和字体。
// 每一项的数值来源由配置决定，宽度由调用方量好后传进来。

namespace pitot::ui {

// 窗口左右内边距：悬停底色画在内容区上，两边各留一点，不贴着窗口边
inline constexpr float kStripFramePad = 4.0f;
// 窗口离任务栏左缘、离开始按钮各留多少
inline constexpr float kStripLeftMarginDip = 4.0f;
inline constexpr float kStripStartGapDip = 8.0f;
// 找不到开始按钮时（任务栏结构变了）只用这么宽，免得盖住它（D-057 的旧上限）
inline constexpr float kStripFallbackRoomDip = 240.0f;
inline constexpr float kStripBarWidth = 3.0f;
inline constexpr int kStripItemCount = config::kStripItemCount;

// 各项的宽度。text 按 items 的下标，是这一项文字列的宽度（名称和最宽读数取大的），不含竖条
struct StripColumns {
    float text[kStripItemCount] = {};
    float rate = 0;  // 两行速率项（网速、磁盘读写）的总宽
    float bar = kStripBarWidth;
    float bar_text_gap = 6.0f;
    float item_gap = 10.0f;
    float net_gap = 14.0f;
    float padding = 10.0f;
    // 样式 B / C（D-075）：每一项的宽度都在 text 里量好了（速率项也是），间距一律用 item_gap
    bool fixed_slots = false;
};

struct StripSpan {
    config::StripSource source = config::StripSource::Empty;
    config::StripTempSource temp = config::StripTempSource::Cpu;
    int item = -1;  // 在 items 里的下标
    float left = 0;   // 从内容区左缘算起（内容区本身还要再加 kStripFramePad 才是窗口坐标）
    float right = 0;
};

struct StripLayout {
    float content_width = 0;  // 含左右 padding，不含窗口内边距
    int count = 0;
    StripSpan spans[kStripItemCount]{};
};

// 两行速率项：网速、磁盘读写
inline bool IsRate(config::StripSource source) {
    return source == config::StripSource::Net || source == config::StripSource::Disk;
}

// 这一项画不画竖条：只有百分比项可以，且要用户开着
inline bool HasBar(const config::StripItemConfig& item) {
    return item.bar && config::StripSourceHasBar(item.source);
}

inline float SlotWidth(const config::StripItemConfig& item, float text, const StripColumns& columns) {
    if (item.source == config::StripSource::Empty) {
        return 0.0f;
    }
    if (columns.fixed_slots) {
        return text;
    }
    if (IsRate(item.source)) {
        return columns.rate;
    }
    return HasBar(item) ? columns.bar + columns.bar_text_gap + text : text;
}

// 把 items[from] 挪到 to。from、to 是排列里的下标
inline void MoveStripItem(config::StripItemConfig (&items)[kStripItemCount], int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= kStripItemCount || to >= kStripItemCount) {
        return;
    }
    const config::StripItemConfig item = items[from];
    if (from < to) {
        for (int i = from; i < to; ++i) {
            items[i] = items[i + 1];
        }
    } else {
        for (int i = from; i > to; --i) {
            items[i] = items[i - 1];
        }
    }
    items[to] = item;
}

// 去掉 items[index]，后面的往前挪，末尾补空
inline void RemoveStripItem(config::StripItemConfig (&items)[kStripItemCount], int index) {
    if (index < 0 || index >= kStripItemCount) {
        return;
    }
    for (int i = index; i < kStripItemCount - 1; ++i) {
        items[i] = items[i + 1];
    }
    items[kStripItemCount - 1] = {};
}

// 指针落在哪一张等宽卡片上。pitch 是卡片左边到下一张左边的距离
inline int StripCardIndexAt(float x, float left, float pitch, int count) {
    if (count <= 1 || pitch <= 0.0f) {
        return 0;
    }
    if (x < left) {
        return 0;
    }
    const int index = static_cast<int>((x - left) / pitch);
    return std::min(index, count - 1);
}

// 这一项前面的间距。单独设过的用自己的；否则两行速率项用 net_gap，其余用 item_gap
inline float GapBefore(const config::StripItemConfig& item, const StripColumns& columns) {
    if (item.gap_set) {
        return item.gap;
    }
    return IsRate(item.source) && !columns.fixed_slots ? columns.net_gap : columns.item_gap;
}

// items 的顺序就是从左到右，Empty 跳过。每一段间距只属于它后面的那一项
inline StripLayout LayoutStrip(const config::StripItemConfig (&items)[kStripItemCount],
                               const StripColumns& columns) {
    StripLayout layout;
    float x = columns.padding;
    bool any = false;
    for (int i = 0; i < kStripItemCount; ++i) {
        const config::StripItemConfig& item = items[i];
        if (item.source == config::StripSource::Empty) {
            continue;
        }
        if (any) {
            x += GapBefore(item, columns);
        }
        const float width = SlotWidth(item, columns.text[i], columns);
        layout.spans[layout.count++] = {item.source, item.temp, i, x, x + width};
        x += width;
        any = true;
    }
    layout.content_width = any ? x + columns.padding : 0.0f;
    return layout;
}

// 窗口要多宽才放得下内容区（DIP）
inline float StripWindowWidth(float content_width) {
    return content_width > 0.0f ? content_width + 2.0f * kStripFramePad : 0.0f;
}

// 任务栏左缘到开始按钮左缘的距离（DIP）→ 任务栏条窗口最多能有多宽。
// 图标靠左时开始按钮就在最左边，结果是 0，任务栏条放不下（见 docs/subsystems/taskbar-strip.md）。
inline float StripRoomDip(std::optional<float> start_offset) {
    if (!start_offset) {
        return kStripFallbackRoomDip;
    }
    return std::max(0.0f, *start_offset - kStripLeftMarginDip - kStripStartGapDip);
}

// 任务栏条能占的一段（D-074），从任务栏左缘算起，DIP。窗口靠左时贴着 left，靠右时贴着 right
struct StripSpace {
    float left = kStripLeftMarginDip;
    float right = kStripLeftMarginDip + kStripFallbackRoomDip;
    bool align_right = false;
    [[nodiscard]] float room() const { return std::max(0.0f, right - left); }
    [[nodiscard]] float WindowLeft(float window_width) const {
        return align_right ? right - std::min(window_width, room()) : left;
    }
};

// 左侧空白：从任务栏左缘到开始按钮前（图标居中时才有空间）
inline StripSpace LeftStripSpace(std::optional<float> start_offset) {
    StripSpace space;
    space.left = kStripLeftMarginDip;
    space.right = space.left + StripRoomDip(start_offset);
    return space;
}

// 通知区左边：从图标区右缘到通知区左缘。找不到图标区时只用通知区左边 240 DIP，免得盖住图标
inline StripSpace RightStripSpace(std::optional<float> tasks_right, std::optional<float> notify_left,
                                  float taskbar_width) {
    StripSpace space;
    space.align_right = true;
    space.right = notify_left.value_or(taskbar_width) - kStripLeftMarginDip;
    space.left = tasks_right ? *tasks_right + kStripStartGapDip : space.right - kStripFallbackRoomDip;
    space.left = std::min(space.left, space.right);
    return space;
}

inline bool StripFits(float content_width, float room) {
    return StripWindowWidth(content_width) <= room + 0.01f;
}

// 内存占用、磁盘活动的着色：80% 起黄色，90% 起红色。温度用的是另一套阈值（℃）
inline constexpr double kUsageWarnPercent = 80.0;
inline constexpr double kUsageHotPercent = 90.0;

enum class UsageTone { Normal, Warn, Hot };

inline bool UsageToneSource(config::StripSource source) {
    return source == config::StripSource::Memory || source == config::StripSource::DiskActivity;
}

inline UsageTone UsageToneOf(std::optional<double> percent) {
    if (!percent || !std::isfinite(*percent)) {
        return UsageTone::Normal;
    }
    if (*percent >= kUsageHotPercent) {
        return UsageTone::Hot;
    }
    if (*percent >= kUsageWarnPercent) {
        return UsageTone::Warn;
    }
    return UsageTone::Normal;
}

// 指针的内容区横坐标落在哪个间距上。slop 是内容区坐标里的最小可点半宽，间距很窄时也能抓住。
// 返回间距左边那一项的下标；没有落在间距上时为 -1
inline int StripGapIndexAt(const StripLayout& layout, float content_x, float slop) {
    int best = -1;
    float best_dist = 0.0f;
    for (int i = 0; i < layout.count - 1; ++i) {
        const float left = layout.spans[i].right;
        const float right = layout.spans[i + 1].left;
        const float mid = (left + right) / 2.0f;
        const float reach = std::max((right - left) / 2.0f, slop);
        const float dist = std::fabs(content_x - mid);
        if (dist <= reach + 0.01f && (best < 0 || dist < best_dist)) {
            best = i;
            best_dist = dist;
        }
    }
    return best;
}

// 拖宽度条上的第 gap_index 个间距，只改这一段，让它的右缘跟着指针。
// pointer_from_track_left 从宽度条左端算起，track_width 是宽度条的宽度，room 是开始按钮前的可用宽度
inline float GapFromMeterPointer(const StripLayout& layout, int gap_index, float pointer_from_track_left,
                                 float track_width, float room) {
    if (gap_index < 0 || gap_index + 1 >= layout.count || track_width <= 0.0f) {
        return 0.0f;
    }
    const float edge = layout.spans[gap_index + 1].left;
    const float current = edge - layout.spans[gap_index].right;
    const float window0 = StripWindowWidth(layout.content_width);
    const auto window_of = [&](float gap) { return window0 + (gap - current); };

    // 还没顶到开始按钮时，宽度条按 room 缩放，间距和指针成正比
    const float scale_room = track_width / std::max(room, 1.0f);
    const float content_at_pointer = pointer_from_track_left / scale_room - kStripFramePad;
    const float gap_in_room = current + (content_at_pointer - edge);
    if (window_of(gap_in_room) <= room + 0.01f) {
        return std::clamp(gap_in_room, 0.0f, config::kMaxStripSpacing);
    }

    // 比开始按钮前的空白还宽：宽度条改为按窗口自己的宽度缩放
    const float denom = track_width - pointer_from_track_left;
    if (denom <= 0.0f) {
        return current;
    }
    const float right_fraction = denom / track_width;
    const float delta = (window0 - (kStripFramePad + edge)) / right_fraction - window0;
    return std::clamp(current + delta, 0.0f, config::kMaxStripSpacing);
}

// 内容区横坐标落在哪一项；落在间距里时以中线为界，两端以外归最近的一项
inline int ItemAt(const StripLayout& layout, float x) {
    if (layout.count <= 0) {
        return -1;
    }
    for (int i = 0; i < layout.count - 1; ++i) {
        const float boundary = (layout.spans[i].right + layout.spans[i + 1].left) / 2.0f;
        if (x < boundary) {
            return i;
        }
    }
    return layout.count - 1;
}

}  // namespace pitot::ui
