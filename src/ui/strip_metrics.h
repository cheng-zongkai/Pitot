#pragma once

#include <dwrite.h>

#include <algorithm>

#include "ui/strip_layout.h"

// 任务栏条各列的宽度。条本身和设置窗口用同一套量法，放不放得下的判断才和实际画出的一样（D-057、D-068）

namespace pitot::ui {

struct StripMeasured {
    // 按 items 下标。样式 A：名称和最宽读数（“100%”“999°”）里较宽的，不含竖条，速率项不用；
    // 样式 B / C：整项的宽度（速率项也是），D-075
    float text[kStripItemCount] = {};
    // 样式 B / C：名称的宽度，数值紧跟在名称后面
    float label[kStripItemCount] = {};
    // 样式 A：两行速率项的总宽（网速、磁盘共用）和其中各段
    float rate = 0;
    float rate_prefix = 0;
    float rate_number = 0;
    float rate_space = 0;
    float rate_unit = 0;
    // 样式 C：速率项每一半（“↓2.4M”）的宽度，第二半从这里开始
    float compact_rate_part = 0;
    float compact_space = 0;
};

// 两行速率项每行开头的标记：网速的 ↓ / ↑、磁盘的读 / 写；样式 B 的磁盘项用磁盘的名称
inline constexpr int kRatePrefixCount = 5;

// 样式 B / C 的数值字号：比样式 A 的数值小一号，和设计稿一致（12 px 配 13 px）
inline float CompactSize(const config::StripStyle& style) { return std::max(6.0f, style.value_size - 1.0f); }

// 字体不存在时退回 Segoe UI。unavailable 是“—”。
// labels 按 items 下标，是每一项实际显示的名称（可以是空串或空指针：不显示名称）。
// rate_prefixes 依次是 ↓、↑、读、写和磁盘的名称
StripMeasured MeasureStripColumns(IDWriteFactory* factory, const config::StripStyle& style,
                                  const wchar_t* const labels[kStripItemCount],
                                  const wchar_t* const rate_prefixes[kRatePrefixCount], const wchar_t* unavailable);

// 同上，名称和标记取当前界面语言（UI 线程）
StripMeasured MeasureStripStyle(IDWriteFactory* factory, const config::StripStyle& style);

// 用当前配置的间距和量好的文字宽度拼出各列
inline StripColumns ColumnsOf(const config::StripStyle& style, const StripMeasured& measured) {
    StripColumns columns;
    for (int i = 0; i < kStripItemCount; ++i) {
        columns.text[i] = measured.text[i];
    }
    columns.rate = measured.rate;
    columns.bar_text_gap = style.bar_text_gap;
    columns.item_gap = style.item_gap;
    columns.net_gap = style.net_gap;
    columns.padding = style.padding;
    columns.fixed_slots = style.variant != config::StripVariant::Stack;
    return columns;
}

}  // namespace pitot::ui
