// 传感器列表的键和默认名称、隐藏后的最高温度（D-070），以及华氏度显示（D-071）

#include <cwchar>

#include "sampler/sensor_keys.h"
#include "test.h"
#include "ui/format.h"
#include "ui/sensor_list.h"
#include "ui/strings.h"

namespace {

using pitot::sampler::SensorState;
using pitot::sampler::Snapshot;

Snapshot MakeSnapshot() {
    Snapshot s;
    s.sensors.cpu_state = SensorState::Ok;
    s.sensors.board_state = SensorState::Ok;
    s.sensors.cpu_temp = 55.0f;
    s.sensors.ccd[1] = 50.0f;
    s.sensors.board_temps[2] = 70.0f;  // 0x104
    s.sensors.board_temp_seen = 1u << 2;
    s.sensors.board_temp_ch[2] = {pitot::sensors::BoardChip::Nct6687D, 2};
    s.sensors.fans[0] = 1200;
    s.sensors.fan_seen = 1u;
    s.sensors.fan_ch[0] = {pitot::sensors::BoardChip::Nct6687D, 0};
    s.gpu.present = true;
    s.gpu.temperature = 40.0f;
    s.gpu.hotspot = 90.0f;  // 不参与最高温度
    s.storage.count = 1;
    s.storage.disks[0].number = 3;
    wcscpy_s(s.storage.disks[0].model, L"NV7400");
    s.storage.disks[0].temperature = 45.0f;
    return s;
}

}  // namespace

TEST_CASE("sensors: keys follow hardware positions") {
    pitot::ui::SetLanguage(pitot::ui::Language::English);
    const Snapshot s = MakeSnapshot();
    pitot::ui::SensorList list{};
    const int n = pitot::ui::ListSensors(s, list);
    CHECK(n == 7);
    CHECK(wcscmp(list[0].key, L"amd.tctl") == 0);
    CHECK(wcscmp(list[1].key, L"amd.ccd2") == 0);
    CHECK(wcscmp(list[1].name, L"CCD2") == 0);
    CHECK(wcscmp(list[2].key, L"nct6687d.0x104") == 0);
    CHECK(wcscmp(list[3].key, L"nct6687d.0x140") == 0);
    CHECK(list[3].fan);
    CHECK(wcscmp(list[4].key, L"gpu.core") == 0);
    CHECK(wcscmp(list[5].key, L"gpu.hotspot") == 0);
    CHECK(wcscmp(list[6].key, L"nvme.3") == 0);
    CHECK(wcscmp(list[6].name, L"NV7400") == 0);
}

TEST_CASE("sensors: hidden sensors leave the maximum") {
    const Snapshot s = MakeSnapshot();
    pitot::ui::SensorList list{};
    const int n = pitot::ui::ListSensors(s, list);
    pitot::config::SensorsConfig sensors;
    // 热点 90 ℃ 不算，最高的是主板 0x104
    int hottest = pitot::ui::HottestSensor(list, n, sensors);
    CHECK(hottest == 2);
    CHECK(*pitot::sampler::MaxTemperature(s.sensors, s.gpu, s.storage, pitot::sampler::MakeSensorMask(sensors, s.sensors)) ==
          70.0);
    sensors.setting_count = 1;
    wcscpy_s(sensors.settings[0].key, L"nct6687d.0x104");
    sensors.settings[0].visible = false;
    hottest = pitot::ui::HottestSensor(list, n, sensors);
    CHECK(hottest == 0);  // Tctl 55
    const auto mask = pitot::sampler::MakeSensorMask(sensors, s.sensors);
    CHECK(*pitot::sampler::MaxTemperature(s.sensors, s.gpu, s.storage, mask) == 55.0);
    CHECK(!pitot::sampler::MaxBoardTemperature(s.sensors, mask));
    CHECK(*pitot::sampler::MaxStorageTemperature(s.storage, mask) == 45.0);
}

TEST_CASE("format: temperature in Celsius and Fahrenheit") {
    wchar_t text[32];
    pitot::ui::SetFahrenheit(false);
    pitot::ui::FormatTemperature(54.84, 1, text);
    CHECK(wcscmp(text, L"54.8 °C") == 0);
    pitot::ui::SetFahrenheit(true);
    pitot::ui::FormatTemperature(100.0, 0, text);
    CHECK(wcscmp(text, L"212 °F") == 0);
    CHECK(pitot::ui::DisplayTemperature(0.0) == 32.0);
    pitot::ui::SetFahrenheit(false);
}

TEST_CASE("sensors: intel package temperature has its own key") {
    pitot::ui::SetLanguage(pitot::ui::Language::English);
    Snapshot s;
    s.sensors.cpu_state = SensorState::Ok;
    s.sensors.cpu_vendor = pitot::sampler::CpuVendor::Intel;
    s.sensors.board_state = SensorState::Unsupported;
    s.sensors.cpu_temp = 62.0f;
    pitot::ui::SensorList list{};
    int n = pitot::ui::ListSensors(s, list);
    CHECK(n == 1);
    CHECK(wcscmp(list[0].key, L"intel.package") == 0);
    CHECK(wcscmp(list[0].name, L"CPU Package") == 0);
    pitot::ui::SetLanguage(pitot::ui::Language::Chinese);  // 后面的测试按中文断言

    // 隐藏 intel.package 后不再参与最高温度
    pitot::config::SensorsConfig sensors;
    sensors.setting_count = 1;
    wcscpy_s(sensors.settings[0].key, L"intel.package");
    sensors.settings[0].visible = false;
    CHECK(!pitot::sampler::MaxTemperature(s.sensors, s.gpu, s.storage, pitot::sampler::MakeSensorMask(sensors, s.sensors)));

    // 没有封装温度传感器的老型号：整行不列
    s.sensors.cpu_temp.reset();
    n = pitot::ui::ListSensors(s, list);
    CHECK(n == 0);
}
