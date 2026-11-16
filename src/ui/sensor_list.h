#pragma once

#include <array>
#include <optional>

#include "config/config.h"
#include "sampler/sensor_keys.h"
#include "sampler/snapshot.h"

// 当前读得到的全部传感器，按传感器面板的顺序（D-070）。传感器面板、设置窗口的“显示与重命名”
// 和高温通知都从这里取，键和默认名称只在一处决定。纯函数，有单元测试。

namespace pitot::ui {

enum class SensorGroup : uint8_t { Cpu, Board, Fans, Gpu, Storage };

struct SensorItem {
    sampler::SensorKey key{};
    wchar_t name[48]{};  // 界面语言的默认名称，不含别名
    SensorGroup group = SensorGroup::Cpu;
    bool fan = false;
    std::optional<float> value;  // ℃ 或 RPM；为空是这个通道接过但这一轮读不到
};

// Tctl + 8 个 CCD + 11 个主板温度 + 16 个风扇 + 显卡 4 项 + 8 块盘
inline constexpr int kMaxSensorItems = 1 + sampler::kCcds + sampler::kBoardTemps + sampler::kBoardFans + 4 +
                                       sampler::kMaxDisks;
using SensorList = std::array<SensorItem, kMaxSensorItems>;

// 返回个数。隐藏与否不在这里过滤，调用方按 config::SensorVisible 自己挑
int ListSensors(const sampler::Snapshot& s, SensorList& out);

// 温度里最高的一项（跳过隐藏的和风扇），没有时返回 -1。和 MaxTemperature 同一口径：不含热点、显存温度
int HottestSensor(const SensorList& list, int count, const config::SensorsConfig& sensors);

}  // namespace pitot::ui
