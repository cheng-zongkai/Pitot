// NVIDIA 扩展指标的换算：PCI 身份、温度哨兵、毫瓦、风扇转速

#include "sensors/nvidia_decode.h"
#include "test.h"

namespace {

using namespace pitot::sensors;

}  // namespace

TEST_CASE("nvidia: pci id matches either half") {
    // 0x1B8010DE：高 16 位设备号 0x1B80，低 16 位厂商 0x10DE（GTX 1080 这种拼法）
    CHECK(NvidiaDeviceSelected(0x1B80, 0x1B8010DE));
    // 反过来拼也认
    CHECK(NvidiaDeviceSelected(0x1B80, 0x10DE1B80));
    CHECK(!NvidiaDeviceSelected(0x2204, 0x1B8010DE));
    // 0 表示“第一块 NVIDIA 卡”，只要厂商号在就行；AMD 的 0x1002 不行
    CHECK(NvidiaDeviceSelected(0, 0x1B8010DE));
    CHECK(!NvidiaDeviceSelected(0, 0x75901002));
    CHECK(!NvidiaDeviceSelected(0, 0));
    CHECK(!NvidiaDeviceSelected(0x1B80, 0));
}

TEST_CASE("nvidia: celsius keeps zero and drops the not-available sentinel") {
    const auto zero = NvidiaCelsius(kNvmlSuccess, 0);
    CHECK(zero.has_value());
    CHECK(*zero == 0.f);
    const auto warm = NvidiaCelsius(kNvmlSuccess, 67);
    CHECK(warm.has_value());
    CHECK(*warm == 67.f);
    CHECK(!NvidiaCelsius(kNvmlSuccess, 0xFFFFFFFFu));
    CHECK(!NvidiaCelsius(kNvmlSuccess, 251));
    CHECK(!NvidiaCelsius(3, 40));
}

TEST_CASE("nvidia: signed celsius drops negatives and failed calls") {
    const auto warm = NvidiaSignedCelsius(kNvmlSuccess, 71);
    CHECK(warm.has_value());
    CHECK(*warm == 71.f);
    CHECK(NvidiaSignedCelsius(kNvmlSuccess, 0).has_value());
    CHECK(!NvidiaSignedCelsius(kNvmlSuccess, -1));
    CHECK(!NvidiaSignedCelsius(kNvmlSuccess, 251));
    CHECK(!NvidiaSignedCelsius(3, 71));
    // nvmlTemperature_v1 的版本号：12 字节的结构，第一版
    CHECK(NvidiaStructVersion(12, 1) == 0x0100000Cu);
}

TEST_CASE("nvidia: power is milliwatts, including a real zero") {
    const auto idle = NvidiaWatts(kNvmlSuccess, 13250);
    CHECK(idle.has_value());
    CHECK(*idle == 13.25f);
    const auto stopped = NvidiaWatts(kNvmlSuccess, 0);
    CHECK(stopped.has_value());
    CHECK(*stopped == 0.f);
    CHECK(!NvidiaWatts(kNvmlSuccess, 0xFFFFFFFFu));
    CHECK(!NvidiaWatts(3, 10000));
}

TEST_CASE("nvidia: memory field is an integer celsius, not a double or a negative") {
    const auto memory = NvidiaFieldCelsius(kNvmlSuccess, kNvmlSuccess, kNvmlValueUnsignedInt, 46);
    CHECK(memory.has_value());
    CHECK(*memory == 46.f);
    const auto wide = NvidiaFieldCelsius(kNvmlSuccess, kNvmlSuccess, kNvmlValueUnsignedLong, 46);
    CHECK(wide.has_value());
    CHECK(*wide == 46.f);
    const auto narrow = NvidiaFieldCelsius(kNvmlSuccess, kNvmlSuccess, kNvmlValueSignedInt, 12);
    CHECK(narrow.has_value());
    CHECK(*narrow == 12.f);
    CHECK(!NvidiaFieldCelsius(kNvmlSuccess, kNvmlSuccess, kNvmlValueSignedInt, static_cast<unsigned int>(-1)));
    CHECK(!NvidiaFieldCelsius(kNvmlSuccess, kNvmlSuccess, 0, 46));  // double
    CHECK(!NvidiaFieldCelsius(kNvmlSuccess, 3, kNvmlValueUnsignedInt, 46));
    CHECK(!NvidiaFieldCelsius(3, kNvmlSuccess, kNvmlValueUnsignedInt, 46));
}

TEST_CASE("nvidia: nvapi pci id is the device id, with or without the vendor half") {
    CHECK(NvidiaNvapiSelected(0, 0x1B80, 0));
    CHECK(NvidiaNvapiSelected(0x1B80, 0x1B80, 0));
    CHECK(NvidiaNvapiSelected(0x1B80, 0x1B8010DE, 0));
    CHECK(NvidiaNvapiSelected(0x1B80, 0, 0x10DE1B80));
    CHECK(!NvidiaNvapiSelected(0x2204, 0x1B80, 0x1B8010DE));
    CHECK(!NvidiaNvapiSelected(0x1B80, 0, 0));
}

TEST_CASE("nvidia: thermal settings expose memory temperature, not the core") {
    NvidiaThermalSensor sensors[3]{};
    sensors[0] = {1, 0, 100, 48, 1};  // 核心
    sensors[1] = {1, 0, 105, 61, kNvidiaThermalMemory};
    const auto memory = NvidiaMemoryFromThermal(sensors, 2);
    CHECK(memory.has_value());
    CHECK(*memory == 61.f);
    sensors[1].current_c = -1;
    CHECK(!NvidiaMemoryFromThermal(sensors, 2));
    CHECK(!NvidiaMemoryFromThermal(sensors, 1));  // 只有核心
    CHECK(NvidiaNvapiVersion(68, 2) == (68u | (2u << 16)));
}

TEST_CASE("nvidia: fan rpm keeps a stopped fan and drops a sentinel") {
    const auto stopped = NvidiaFanRpm(kNvmlSuccess, 0);
    CHECK(stopped.has_value());
    CHECK(*stopped == 0);
    const auto spinning = NvidiaFanRpm(kNvmlSuccess, 1400);
    CHECK(spinning.has_value());
    CHECK(*spinning == 1400);
    CHECK(!NvidiaFanRpm(kNvmlSuccess, 20001));
    CHECK(!NvidiaFanRpm(3, 0));
    CHECK(NvidiaStructVersion(12, 1) == (12u | (1u << 24)));
}
