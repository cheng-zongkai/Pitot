// 浮窗用到的纯逻辑：趋势环形缓冲、CPU 短名称、运行时长、紧凑速率

#include <cwchar>

#include "sampler/cpu_info.h"
#include "test.h"
#include "ui/core_bars.h"
#include "ui/format.h"
#include "ui/history.h"
#include "ui/strip_layout.h"
#include "ui/strip_metrics.h"

namespace {

bool ShortName(const wchar_t* brand, const wchar_t* expected) {
    wchar_t out[64];
    pitot::sampler::ShortCpuName(brand, out);
    return wcscmp(out, expected) == 0;
}

bool Uptime(unsigned long long ms, const wchar_t* expected) {
    wchar_t out[32];
    pitot::ui::FormatUptime(ms, out);
    return wcscmp(out, expected) == 0;
}

bool RateShort(std::optional<double> v, const wchar_t* expected) {
    wchar_t out[16];
    pitot::ui::FormatRateShort(v, out);
    return wcscmp(out, expected) == 0;
}

constexpr unsigned long long kMinute = 60'000;

}  // namespace

TEST_CASE("history: keeps the newest N in order") {
    pitot::ui::History<3> h;
    CHECK(h.size() == 0);
    CHECK(!h.at(0));
    h.Push(1.0);
    h.Push(2.0);
    CHECK(h.size() == 2);
    CHECK(h.at(0) == 1.0);
    CHECK(h.at(1) == 2.0);
    h.Push(3.0);
    h.Push(4.0);  // 挤掉 1
    CHECK(h.size() == 3);
    CHECK(h.at(0) == 2.0);
    CHECK(h.at(2) == 4.0);
    CHECK(!h.at(3));
}

TEST_CASE("history: unavailable samples stay empty") {
    pitot::ui::History<4> h;
    h.Push(1.0);
    h.Push(std::nullopt);
    h.Push(3.0);
    CHECK(h.at(0) == 1.0);
    CHECK(!h.at(1));
    CHECK(h.at(2) == 3.0);
}

TEST_CASE("cpu name: strips vendor and core count") {
    CHECK(ShortName(L"AMD Ryzen 5 5600 6-Core Processor", L"Ryzen 5 5600"));
    CHECK(ShortName(L"AMD Ryzen 7 5700X 8-Core Processor            ", L"Ryzen 7 5700X"));
    CHECK(ShortName(L"AMD Ryzen 7 7840HS w/ Radeon 780M Graphics", L"Ryzen 7 7840HS w/ Radeon 780M Graphics"));
    CHECK(ShortName(L"AMD Ryzen 5 5600G with Radeon Graphics", L"Ryzen 5 5600G"));
    CHECK(ShortName(L"  Intel(R) Core(TM) i7-12700K  ", L"i7-12700K"));
    CHECK(ShortName(L"", L""));
}

TEST_CASE("uptime: days, hours, minutes") {
    CHECK(Uptime(0, L"0 分钟"));
    CHECK(Uptime(12 * kMinute + 59'999, L"12 分钟"));
    CHECK(Uptime(4 * 60 * kMinute + 12 * kMinute, L"4 小时 12 分钟"));
    CHECK(Uptime((3 * 24 + 4) * 60 * kMinute + 30 * kMinute, L"3 天 4 小时"));
}

TEST_CASE("core bars: layouts from 6 to 32 cores") {
    using pitot::ui::LayoutCoreBars;
    constexpr float kWidth = 348.0f;  // 浮窗 380 减两边各 16
    // 每个布局都要放得下：总宽不超、每核至少留得出圆环
    for (int cores : {1, 4, 6, 8, 12, 16, 24, 32}) {
        const auto l = LayoutCoreBars(cores, kWidth);
        CHECK(l.cols * l.rows >= cores);
        CHECK(static_cast<float>(l.cols) * l.group_w + static_cast<float>(l.cols - 1) * l.core_gap <= kWidth + 0.01f);
        CHECK(l.group_w >= 14.0f);
    }
    // 6 核（5600）、8 核（5700X）、16 核：都是一行
    auto l = LayoutCoreBars(6, kWidth);
    CHECK(l.cols == 6 && l.rows == 1);
    l = LayoutCoreBars(8, kWidth);
    CHECK(l.cols == 8 && l.rows == 1);
    l = LayoutCoreBars(16, kWidth);
    CHECK(l.cols == 16 && l.rows == 1);
    // 32 核：两行
    l = LayoutCoreBars(32, kWidth);
    CHECK(l.cols == 16 && l.rows == 2);
    // 不可用
    l = LayoutCoreBars(0, kWidth);
    CHECK(l.cols == 0 && l.rows == 0);
}

TEST_CASE("core rings: diameter cap and sweep") {
    using pitot::ui::CoreRingDiameter;
    using pitot::ui::CoreRingSweepDegrees;
    CHECK(CoreRingDiameter(0) == 0.0f);
    CHECK(CoreRingDiameter(20) == 20.0f);
    CHECK(CoreRingDiameter(48) == 36.0f);
    CHECK(CoreRingSweepDegrees(0) == 0.0f);
    CHECK(CoreRingSweepDegrees(0.5f) == 0.0f);
    CHECK(CoreRingSweepDegrees(1) == 8.0f);
    CHECK(CoreRingSweepDegrees(50) == 180.0f);
    CHECK(CoreRingSweepDegrees(100) == 360.0f);
}

TEST_CASE("strip layout: per-item widths, bars and the room before Start") {
    using pitot::config::StripItemConfig;
    using pitot::config::StripSource;
    using pitot::config::StripTempSource;
    using pitot::ui::ColumnsOf;
    using pitot::ui::ItemAt;
    using pitot::ui::LayoutStrip;
    using pitot::ui::StripColumns;
    using pitot::ui::StripFits;
    using pitot::ui::StripMeasured;
    using pitot::ui::StripRoomDip;
    using pitot::ui::StripWindowWidth;
    using pitot::ui::kStripBarWidth;
    using pitot::ui::kStripFramePad;
    pitot::config::StripStyle style;
    StripMeasured measured;
    measured.text[0] = 32;
    measured.text[1] = 28;
    measured.rate = 70;
    const StripColumns columns = ColumnsOf(style, measured);
    const auto layout = LayoutStrip(style.items, columns);
    const float meter_cpu = kStripBarWidth + style.bar_text_gap + 32;
    const float meter_memory = kStripBarWidth + style.bar_text_gap + 28;
    const float expected = 2 * style.padding + meter_cpu + style.item_gap + meter_memory + style.net_gap + 70;
    CHECK(layout.count == 3);
    CHECK(layout.content_width == expected);
    CHECK(layout.spans[0].source == StripSource::Cpu);
    CHECK(layout.spans[1].source == StripSource::Memory);
    CHECK(layout.spans[2].source == StripSource::Net);
    CHECK(layout.spans[2].item == 2);
    // 间距中线归左边那一项
    const float gap_mid = (layout.spans[0].right + layout.spans[1].left) / 2.0f;
    CHECK(ItemAt(layout, gap_mid - 0.1f) == 0);
    CHECK(ItemAt(layout, gap_mid + 0.1f) == 1);

    // 窗口宽度是内容区加两边的内边距；可用宽度是开始按钮左缘减去两头的留白
    CHECK(StripWindowWidth(expected) == expected + 2 * kStripFramePad);
    CHECK(StripWindowWidth(0) == 0);
    CHECK(StripRoomDip(612.0f) == 600.0f);
    CHECK(StripRoomDip(5.0f) == 0.0f);  // 图标靠左：开始按钮在最左边
    CHECK(StripRoomDip(std::nullopt) == 240.0f);
    CHECK(StripFits(expected, StripWindowWidth(expected)));
    CHECK(!StripFits(expected, StripWindowWidth(expected) - 1));
    CHECK(StripFits(expected, StripRoomDip(1000.0f)));

    // 宽度跟着每一项：同一来源出现两次各算各的，竖条可以关掉，温度没有竖条
    StripItemConfig items[pitot::config::kStripItemCount] = {
        {StripSource::Cpu, StripTempSource::Cpu, false},
        {StripSource::Gpu, StripTempSource::Cpu, true},
        {StripSource::Temp, StripTempSource::Board, true},
        {StripSource::Cpu, StripTempSource::Cpu, true},
        {StripSource::Disk, StripTempSource::Cpu, true},
    };
    StripColumns per_item = columns;
    per_item.text[0] = 30;
    per_item.text[1] = 31;
    per_item.text[2] = 40;
    per_item.text[3] = 30;
    const auto wide = LayoutStrip(items, per_item);
    CHECK(wide.count == 5);
    CHECK(wide.spans[0].right - wide.spans[0].left == 30.0f);
    CHECK(wide.spans[1].right - wide.spans[1].left == kStripBarWidth + style.bar_text_gap + 31.0f);
    CHECK(wide.spans[2].right - wide.spans[2].left == 40.0f);
    CHECK(wide.spans[3].right - wide.spans[3].left == kStripBarWidth + style.bar_text_gap + 30.0f);
    CHECK(wide.spans[4].right - wide.spans[4].left == 70.0f);
    CHECK(wide.spans[4].left == wide.spans[3].right + style.net_gap);
    CHECK(wide.content_width == wide.spans[4].right + style.padding);

    StripItemConfig none[pitot::config::kStripItemCount]{};
    CHECK(LayoutStrip(none, columns).count == 0);
    CHECK(LayoutStrip(none, columns).content_width == 0);
}

TEST_CASE("strip order: item list runs left to right, empty slots are skipped") {
    using pitot::config::StripItemConfig;
    using pitot::config::StripSource;
    using pitot::config::StripTempSource;
    using pitot::ui::ItemAt;
    using pitot::ui::LayoutStrip;
    using pitot::ui::MoveStripItem;
    using pitot::ui::RemoveStripItem;
    using pitot::ui::StripCardIndexAt;
    using pitot::ui::StripColumns;
    using pitot::ui::kStripItemCount;
    StripItemConfig items[kStripItemCount] = {
        {StripSource::Net, StripTempSource::Cpu},    {StripSource::Cpu, StripTempSource::Cpu},
        {},                                          {StripSource::Memory, StripTempSource::Cpu},
        {StripSource::Temp, StripTempSource::Gpu},   {},
    };
    StripColumns columns;
    for (float& text : columns.text) {
        text = 20;
    }
    columns.rate = 20;
    columns.bar = 0;
    columns.bar_text_gap = 0;
    columns.item_gap = 10;
    columns.net_gap = 14;
    columns.padding = 0;
    const auto layout = LayoutStrip(items, columns);
    CHECK(layout.count == 4);
    CHECK(layout.spans[0].source == StripSource::Net);
    CHECK(layout.spans[1].source == StripSource::Cpu);
    CHECK(layout.spans[2].source == StripSource::Memory);
    CHECK(layout.spans[2].item == 3);  // 下标指回 items，空格不算
    CHECK(layout.spans[3].source == StripSource::Temp);
    // 网速在最左时，它和后面一项之间用的是各项间距，不是速率项前的大间距
    CHECK(layout.spans[1].left == 30.0f);
    CHECK(ItemAt(layout, -5.0f) == 0);
    CHECK(ItemAt(layout, 1000.0f) == 3);

    StripItemConfig moved[kStripItemCount] = {
        {StripSource::Cpu, StripTempSource::Cpu},    {StripSource::Memory, StripTempSource::Cpu},
        {StripSource::Gpu, StripTempSource::Cpu},    {StripSource::Disk, StripTempSource::Cpu},
        {StripSource::Net, StripTempSource::Cpu},    {},
    };
    MoveStripItem(moved, 0, 2);
    CHECK(moved[0].source == StripSource::Memory);
    CHECK(moved[1].source == StripSource::Gpu);
    CHECK(moved[2].source == StripSource::Cpu);
    MoveStripItem(moved, 2, 0);
    CHECK(moved[0].source == StripSource::Cpu);
    CHECK(moved[1].source == StripSource::Memory);
    CHECK(moved[2].source == StripSource::Gpu);
    RemoveStripItem(moved, 1);
    CHECK(moved[0].source == StripSource::Cpu);
    CHECK(moved[1].source == StripSource::Gpu);
    CHECK(moved[3].source == StripSource::Net);
    CHECK(moved[4].source == StripSource::Empty);
    CHECK(moved[kStripItemCount - 1].source == StripSource::Empty);
    CHECK(StripCardIndexAt(0, 10, 20, 6) == 0);
    CHECK(StripCardIndexAt(29, 10, 20, 6) == 0);
    CHECK(StripCardIndexAt(30, 10, 20, 6) == 1);
    CHECK(StripCardIndexAt(1000, 10, 20, 6) == 5);
}

TEST_CASE("strip usage tone: memory and disk activity at 80 and 90") {
    using pitot::ui::UsageTone;
    using pitot::ui::UsageToneOf;
    using pitot::ui::UsageToneSource;
    CHECK(UsageToneSource(pitot::config::StripSource::Memory));
    CHECK(UsageToneSource(pitot::config::StripSource::DiskActivity));
    CHECK(!UsageToneSource(pitot::config::StripSource::Cpu));
    CHECK(!UsageToneSource(pitot::config::StripSource::Gpu));
    CHECK(UsageToneOf(std::nullopt) == UsageTone::Normal);
    CHECK(UsageToneOf(79.9) == UsageTone::Normal);
    CHECK(UsageToneOf(80.0) == UsageTone::Warn);
    CHECK(UsageToneOf(89.9) == UsageTone::Warn);
    CHECK(UsageToneOf(90.0) == UsageTone::Hot);
    CHECK(UsageToneOf(100.0) == UsageTone::Hot);
}

TEST_CASE("strip meter: dragging a gap sets that gap, and the pointer stays on its right edge") {
    using pitot::config::StripItemConfig;
    using pitot::config::StripSource;
    using pitot::config::StripTempSource;
    using pitot::ui::GapFromMeterPointer;
    using pitot::ui::LayoutStrip;
    using pitot::ui::StripColumns;
    using pitot::ui::StripGapIndexAt;
    using pitot::ui::kStripFramePad;
    using pitot::ui::kStripItemCount;
    StripItemConfig items[kStripItemCount] = {
        {StripSource::Cpu, StripTempSource::Cpu, true},
        {StripSource::Memory, StripTempSource::Cpu, true},
        {StripSource::Net, StripTempSource::Cpu, false},
        {StripSource::Gpu, StripTempSource::Cpu, false},
    };
    StripColumns columns;
    columns.text[0] = columns.text[1] = columns.text[3] = 30;
    columns.rate = 40;
    columns.bar = 0;
    columns.bar_text_gap = 0;
    columns.item_gap = 10;
    columns.net_gap = 14;
    columns.padding = 10;
    const auto layout = LayoutStrip(items, columns);
    CHECK(layout.count == 4);
    // 内存前面是普通间距，网速前面是速率间距，GPU 前面又是普通间距
    CHECK(layout.spans[1].left - layout.spans[0].right == 10.0f);
    CHECK(layout.spans[2].left - layout.spans[1].right == 14.0f);
    CHECK(layout.spans[3].left - layout.spans[2].right == 10.0f);
    // 单独设过的间距不影响旁边那段
    items[1].gap_set = true;
    items[1].gap = 6;
    items[3].gap_set = true;
    items[3].gap = 22;
    const auto custom = LayoutStrip(items, columns);
    CHECK(custom.spans[1].left - custom.spans[0].right == 6.0f);
    CHECK(custom.spans[2].left - custom.spans[1].right == 14.0f);
    CHECK(custom.spans[3].left - custom.spans[2].right == 22.0f);
    CHECK(StripGapIndexAt(layout, layout.spans[0].right + 5.0f, 4.0f) == 0);
    CHECK(StripGapIndexAt(layout, layout.spans[1].right + 7.0f, 4.0f) == 1);
    CHECK(StripGapIndexAt(layout, layout.spans[0].left + 2.0f, 4.0f) == -1);

    const float room = 800.0f;
    const float track = 400.0f;
    const float scale = track / room;
    // 只移动这一段的右缘，旁边的间距不参与
    const float edge = layout.spans[1].left + 10.0f;
    const float pointer = (kStripFramePad + edge) * scale;
    CHECK(GapFromMeterPointer(layout, 0, pointer, track, room) == 20.0f);
    const float edge2 = layout.spans[3].left + 10.0f;
    const float pointer2 = (kStripFramePad + edge2) * scale;
    CHECK(GapFromMeterPointer(layout, 2, pointer2, track, room) == 20.0f);
    const float edge_rate = layout.spans[2].left + 6.0f;
    const float pointer_rate = (kStripFramePad + edge_rate) * scale;
    CHECK(GapFromMeterPointer(layout, 1, pointer_rate, track, room) == 20.0f);
    CHECK(GapFromMeterPointer(layout, 0, 0.0f, track, room) == 0.0f);

    // 比开始按钮前的空白还宽时，宽度条按窗口自己的宽度缩放，右缘仍然落在指针上
    const float window = pitot::ui::StripWindowWidth(layout.content_width);
    const float desired_gap = 150.0f;
    const float delta = desired_gap - 10.0f;
    const float pointer_b = (kStripFramePad + layout.spans[1].left + delta) * 100.0f / (window + delta);
    const float gap = GapFromMeterPointer(layout, 0, pointer_b, 100.0f, 50.0f);
    CHECK(std::fabs(gap - desired_gap) < 0.02f);
    const float moved = layout.spans[1].left + (gap - 10.0f);
    const float width = window + (gap - 10.0f);
    CHECK(std::fabs((kStripFramePad + moved) * 100.0f / width - pointer_b) < 0.05f);
}

TEST_CASE("format: short rate for tile corner") {
    CHECK(RateShort(512.0, L"512B"));
    CHECK(RateShort(180.0 * 1024, L"180K"));
    CHECK(RateShort(2.4 * 1024 * 1024, L"2.4M"));
    CHECK(RateShort(std::nullopt, L"—"));
}
