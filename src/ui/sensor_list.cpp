#include "ui/sensor_list.h"

#include <cwchar>
#include <iterator>
#include <type_traits>

#include "ui/strings.h"

namespace pitot::ui {
namespace {

SensorItem& Add(SensorList& out, int& n, const wchar_t* key, const wchar_t* name, SensorGroup group, bool fan,
                std::optional<float> value) {
    SensorItem& item = out[static_cast<size_t>(n++)];
    wcscpy_s(item.key, key);
    wcsncpy_s(item.name, name, _TRUNCATE);
    item.group = group;
    item.fan = fan;
    item.value = value;
    return item;
}

static_assert(std::extent_v<decltype(Strings::kNctSourceNames)> == sensors::kNctSourceLabels);

// 默认名称（D-085）。NCT6687D 用 MSI 板子的部件名（D-029）；Nuvoton bank 系用芯片的输入名，
// 哪个输入接的是什么由主板决定，用户对照 HWiNFO 后可以改名；其他芯片按槽位编号
template <size_t N>
void BoardTempName(const sampler::BoardChannel& ch, int slot, wchar_t (&name)[N]) {
    const Strings& str = Str();
    switch (sensors::ChipInfo(ch.chip).family) {
    case sensors::BoardFamily::NuvotonEc:
        if (ch.chip == sensors::BoardChip::Nct6687D && ch.id < std::size(str.kBoardTempNames)) {
            wcscpy_s(name, str.kBoardTempNames[ch.id]);
        } else {
            _snwprintf_s(name, _TRUNCATE, str.kBoardTempUnnamed, sensors::kNctTempBase + 2 * ch.id);
        }
        break;
    case sensors::BoardFamily::Nuvoton:
        if (const int label = sensors::NctSourceLabel(ch.id); label >= 0) {
            wcscpy_s(name, str.kNctSourceNames[label]);
        } else {
            _snwprintf_s(name, _TRUNCATE, str.kBoardTempNumbered, slot + 1);
        }
        break;
    default:
        _snwprintf_s(name, _TRUNCATE, str.kBoardTempNumbered, slot + 1);
        break;
    }
}

template <size_t N>
void FanName(const sampler::BoardChannel& ch, int slot, wchar_t (&name)[N]) {
    const Strings& str = Str();
    if (sensors::ChipInfo(ch.chip).family == sensors::BoardFamily::NuvotonEc) {
        if (ch.chip == sensors::BoardChip::Nct6687D && ch.id < std::size(str.kFanNames)) {
            wcscpy_s(name, str.kFanNames[ch.id]);
        } else {
            _snwprintf_s(name, _TRUNCATE, str.kFanUnnamed, sensors::kNctFanBase + 2 * ch.id);
        }
    } else {
        _snwprintf_s(name, _TRUNCATE, str.kFanNumbered, slot + 1);
    }
}

}  // namespace

int ListSensors(const sampler::Snapshot& s, SensorList& out) {
    using sampler::SensorState;
    const Strings& str = Str();
    int n = 0;
    sampler::SensorKey key;
    wchar_t name[48];
    const sampler::SensorReadings& r = s.sensors;

    // 读不到的部分整节不列（和传感器面板一致）
    if (r.cpu_state == SensorState::Ok) {
        if (r.cpu_vendor == sampler::CpuVendor::Intel) {
            // 没有封装温度传感器的老型号整行不列，不常驻一个“—”
            if (r.cpu_temp) {
                Add(out, n, sampler::kIntelPackageKey, str.kCpuPackage, SensorGroup::Cpu, false, r.cpu_temp);
            }
        } else {
            Add(out, n, sampler::kTctlKey, str.kTctlTdie, SensorGroup::Cpu, false, r.cpu_temp);
        }
        for (int i = 0; i < sampler::kCcds; ++i) {
            if (r.ccd[static_cast<size_t>(i)]) {
                sampler::CcdKey(i, key);
                _snwprintf_s(name, _TRUNCATE, str.kCcdFormat, i + 1);
                Add(out, n, key, name, SensorGroup::Cpu, false, r.ccd[static_cast<size_t>(i)]);
            }
        }
    }
    if (r.board_state == SensorState::Ok) {
        for (int i = 0; i < sampler::kBoardTemps; ++i) {
            const sampler::BoardChannel& ch = r.board_temp_ch[static_cast<size_t>(i)];
            if (!(r.board_temp_seen & (1u << i)) || ch.chip == sensors::BoardChip::None) {
                continue;
            }
            sampler::BoardTempKey(ch, key);
            BoardTempName(ch, i, name);
            Add(out, n, key, name, SensorGroup::Board, false, r.board_temps[static_cast<size_t>(i)]);
        }
        for (int i = 0; i < sampler::kBoardFans; ++i) {
            const sampler::BoardChannel& ch = r.fan_ch[static_cast<size_t>(i)];
            if (!(r.fan_seen & (1u << i)) || ch.chip == sensors::BoardChip::None) {
                continue;
            }
            sampler::FanKey(ch, key);
            FanName(ch, i, name);
            const std::optional<int>& rpm = r.fans[static_cast<size_t>(i)];
            Add(out, n, key, name, SensorGroup::Fans, true,
                rpm ? std::optional<float>(static_cast<float>(*rpm)) : std::nullopt);
        }
    }

    // 显卡：核心温度常驻；热点、显存温度和风扇来自 ADLX，浮窗打开时才有
    const sampler::GpuReadings& g = s.gpu;
    if (g.present) {
        if (g.temperature) {
            Add(out, n, sampler::kGpuCoreKey, str.kGpuCore, SensorGroup::Gpu, false, g.temperature);
        }
        if (g.hotspot) {
            Add(out, n, sampler::kGpuHotspotKey, str.kGpuHotspotRow, SensorGroup::Gpu, false, g.hotspot);
        }
        if (g.memory_temperature) {
            Add(out, n, sampler::kGpuMemoryKey, str.kGpuVramTemp, SensorGroup::Gpu, false, g.memory_temperature);
        }
        if (g.fan_rpm) {
            Add(out, n, sampler::kGpuFanKey, str.kGpuFan, SensorGroup::Gpu, true, static_cast<float>(*g.fan_rpm));
        }
    }

    // 存储：只有 NVMe 读温度（D-033），没有温度的盘不列
    for (int i = 0; i < s.storage.count; ++i) {
        const sampler::DiskReadings& d = s.storage.disks[static_cast<size_t>(i)];
        if (d.temperature) {
            sampler::NvmeKey(d.number, key);
            Add(out, n, key, d.model, SensorGroup::Storage, false, d.temperature);
        }
    }
    return n;
}

int HottestSensor(const SensorList& list, int count, const config::SensorsConfig& sensors) {
    int best = -1;
    for (int i = 0; i < count; ++i) {
        const SensorItem& item = list[static_cast<size_t>(i)];
        if (item.fan || !item.value || !config::SensorVisible(sensors, item.key)) {
            continue;
        }
        if (wcscmp(item.key, sampler::kGpuHotspotKey) == 0 || wcscmp(item.key, sampler::kGpuMemoryKey) == 0) {
            continue;
        }
        if (best < 0 || *item.value > *list[static_cast<size_t>(best)].value) {
            best = i;
        }
    }
    return best;
}

}  // namespace pitot::ui
