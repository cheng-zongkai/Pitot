// 主板 Super I/O 芯片（D-085）：识别表、基址校验、各家的温度 / 风扇解码，以及传感器键和默认名称

#include <array>
#include <cmath>
#include <cwchar>

#include "sampler/sensor_keys.h"
#include "sensors/board_chips.h"
#include "test.h"
#include "ui/sensor_list.h"
#include "ui/strings.h"

namespace {

using namespace pitot::sensors;

bool Near(double a, double b) { return std::fabs(a - b) < 1e-4; }

}  // namespace

TEST_CASE("board: chip identification") {
    CHECK(IdentifyNuvotonFintek(0xD5, 0x92) == BoardChip::Nct6687D);
    CHECK(IdentifyNuvotonFintek(0xD4, 0x40) == BoardChip::Nct6686D);
    CHECK(IdentifyNuvotonFintek(0xD4, 0x2B) == BoardChip::Nct6798D);
    CHECK(IdentifyNuvotonFintek(0xD8, 0x02) == BoardChip::Nct6799D);
    CHECK(IdentifyNuvotonFintek(0xC5, 0x62) == BoardChip::Nct6779D);  // 修订号只看高 4 位
    CHECK(IdentifyNuvotonFintek(0xC9, 0x13) == BoardChip::Nct6792D);
    CHECK(IdentifyNuvotonFintek(0x10, 0x05) == BoardChip::F71889AD);
    CHECK(IdentifyNuvotonFintek(0x11, 0x18) == BoardChip::F71869A);
    CHECK(IdentifyNuvotonFintek(0xD8, 0x06) == BoardChip::None);  // NCT6701D 不支持
    CHECK(IdentifyNuvotonFintek(0xFF, 0xFF) == BoardChip::None);
    CHECK(IdentifyIte(0x8689) == BoardChip::It8689E);
    CHECK(IdentifyIte(0x8733) == BoardChip::It8792E);
    CHECK(IdentifyIte(0xFFFF) == BoardChip::None);

    CHECK(ChipInfo(BoardChip::Nct6687D).family == BoardFamily::NuvotonEc);
    CHECK(ChipInfo(BoardChip::Nct6798D).family == BoardFamily::Nuvoton);
    CHECK(ChipInfo(BoardChip::It8686E).temps == 7);
    CHECK(ChipInfo(BoardChip::F71882).fans == 4);
    CHECK(HardwareMonitorDevice(BoardChip::F71858) == 0x02);
    CHECK(HardwareMonitorDevice(BoardChip::Nct6798D) == 0x0B);
    CHECK(HasNuvotonIoLock(BoardChip::Nct6798D));
    CHECK(!HasNuvotonIoLock(BoardChip::Nct6779D));
    CHECK(!HasNuvotonIoLock(BoardChip::Nct6687D));
}

TEST_CASE("board: base address validation") {
    CHECK(ValidateBase(0x0A20, 0x0A20) == 0x0A20);
    CHECK(!ValidateBase(0x0A20, 0x0A28));  // 两次读不一致
    CHECK(ValidateBase(0x0A25, 0x0A25) == 0x0A20);  // Fintek 已加了偏移 5
    CHECK(!ValidateBase(0x00F0, 0x00F0));
    CHECK(!ValidateBase(0x1290, 0x1290));
    CHECK(!ValidateBase(0x0A22, 0x0A22));  // 没按 8 字节对齐
}

TEST_CASE("board: nuvoton temperature sources") {
    CHECK(NctLayoutFor(BoardChip::Nct6798D).slot_count == 11);
    CHECK(NctLayoutFor(BoardChip::Nct6796D).slot_count == 10);
    CHECK(NctLayoutFor(BoardChip::Nct6779D).slot_count == 7);
    CHECK(NctLayoutFor(BoardChip::Nct6687D).slot_count == 0);

    std::array<uint8_t, 0x1000> regs{};
    regs[0x100] = kNctPeci0;  // 通道 0x073 接的是 PECI
    regs[0x073] = 45;
    regs[0x074] = 0x80;  // 半度
    regs[0x200] = kNctCputin;
    regs[0x075] = 40;
    regs[0x300] = kNctSystin;
    regs[0x077] = 33;
    regs[0x800] = kNctCputin;  // 同一来源第二次出现：先到的为准
    regs[0x079] = 77;
    regs[0x492] = 28;  // AUXTIN0 只有备用寄存器有读数
    regs[0x491] = 99;  // CPUTIN 已经从监控通道读到，备用寄存器不用
    regs[0x401] = 0x80;  // PCH：−128 ℃ 是无效值

    const NctLayout layout = NctLayoutFor(BoardChip::Nct6798D);
    std::optional<float> out[11];
    ReadNctTemperatures(
        layout,
        [&](uint16_t reg, uint8_t& value) {
            value = regs[reg];
            return true;
        },
        out);
    auto slot_of = [&](uint8_t source) {
        for (int i = 0; i < layout.slot_count; ++i) {
            if (layout.slots[i].source == source) {
                return i;
            }
        }
        return -1;
    };
    CHECK(Near(*out[slot_of(kNctPeci0)], 45.5));
    CHECK(Near(*out[slot_of(kNctCputin)], 40.0));
    CHECK(Near(*out[slot_of(kNctSystin)], 33.0));
    CHECK(Near(*out[slot_of(kNctAuxtin0)], 28.0));
    CHECK(!out[slot_of(kNctPchChip)]);
    CHECK(!out[slot_of(kNctTsensor)]);

    // 读失败的通道跳过，不影响其他
    ReadNctTemperatures(
        layout,
        [&](uint16_t reg, uint8_t& value) {
            value = regs[reg];
            return reg != 0x073;
        },
        out);
    CHECK(!out[slot_of(kNctPeci0)]);
    CHECK(Near(*out[slot_of(kNctSystin)], 33.0));
}

TEST_CASE("board: nuvoton values") {
    CHECK(Near(*DecodeNuvotonTemperature(0xFB, true), -4.5));
    CHECK(!DecodeNuvotonTemperature(0x80, false));  // −128 ℃
    CHECK(!DecodeNuvotonTemperature(127, false));
    CHECK(!DecodeNuvotonAltTemperature(0));
    // 1350 计数 = 1000 RPM：高字节 1350 >> 5 = 42，低 5 位 6
    CHECK(DecodeNuvoton13BitFan(42, 6) == 1000);
    CHECK(DecodeNuvoton13BitFan(0xFF, 0x1F) == 0);  // 计数到顶：停转
    CHECK(!DecodeNuvoton13BitFan(0, 0x10));          // 太小：无效
}

TEST_CASE("board: ite values") {
    CHECK(DecodeIteTemperature(45) == 45.0f);
    CHECK(!DecodeIteTemperature(0x80));
    CHECK(!DecodeIteTemperature(127));
    CHECK(DecodeIte16BitFan(675) == 1000);
    CHECK(DecodeIte16BitFan(0xFFFF) == 0);
    CHECK(!DecodeIte16BitFan(0x3F));
    CHECK(DecodeIte8BitFan(135, 4) == 2500);
    CHECK(DecodeIte8BitFan(0xFF, 2) == 0);
    CHECK(!DecodeIte8BitFan(0, 2));
    CHECK(!IteHas16BitFans(BoardChip::It8705F, 2));
    CHECK(IteHas16BitFans(BoardChip::It8705F, 3));
    CHECK(IteFanEnabled(BoardChip::It8689E, 3, 0x10, 0));
    CHECK(!IteFanEnabled(BoardChip::It8689E, 4, 0x10, 0));
    CHECK(IteFanEnabled(BoardChip::It8665E, 5, 0x00, 0x08));  // IT8665E 的第 6 个看 0x0B [3]
    CHECK(IteFanEnabled(BoardChip::It8689E, 5, 0x04, 0x00));
    CHECK(IteTraitsFor(BoardChip::It8665E).alt_6th_fan);
    CHECK(IteTraitsFor(BoardChip::It8688E).unchecked_index);
}

TEST_CASE("board: fintek values") {
    CHECK(DecodeFintekFan(1500) == 1000);
    CHECK(DecodeFintekFan(0x0FFF) == 0);
    CHECK(!DecodeFintekFan(0));
    CHECK(Near(*DecodeF71858Temperature(45, 0x00, 0), 45.0));
    CHECK(Near(*DecodeF71858Temperature(45, 0x80, 0), 45.5));
    CHECK(!DecodeF71858Temperature(0xBB, 0, 0));
}

TEST_CASE("board: sensor keys and default names") {
    using pitot::sampler::BoardChannel;
    pitot::sampler::SensorKey key;
    pitot::sampler::BoardTempKey(BoardChannel{BoardChip::Nct6687D, 2}, key);
    CHECK(wcscmp(key, L"nct6687d.0x104") == 0);  // 和 D-070 的旧键一致
    pitot::sampler::FanKey(BoardChannel{BoardChip::Nct6687D, 1}, key);
    CHECK(wcscmp(key, L"nct6687d.0x142") == 0);
    pitot::sampler::BoardTempKey(BoardChannel{BoardChip::Nct6798D, kNctSystin}, key);
    CHECK(wcscmp(key, L"nct6798d.systin") == 0);
    pitot::sampler::BoardTempKey(BoardChannel{BoardChip::It8689E, 0}, key);
    CHECK(wcscmp(key, L"it8689e.temp1") == 0);
    pitot::sampler::FanKey(BoardChannel{BoardChip::It87952E, 1}, key);
    CHECK(wcscmp(key, L"it87952e.fan2") == 0);

    // 两颗 ITE：第二颗接在第一颗后面，名称按槽位连续编号，键各用各的芯片名
    pitot::ui::SetLanguage(pitot::ui::Language::English);
    pitot::sampler::Snapshot s;
    s.sensors.cpu_state = pitot::sampler::SensorState::Unsupported;
    s.sensors.board_state = pitot::sampler::SensorState::Ok;
    s.sensors.board_temps[0] = 30.0f;
    s.sensors.board_temp_ch[0] = {BoardChip::It8689E, 0};
    s.sensors.board_temps[6] = 41.0f;
    s.sensors.board_temp_ch[6] = {BoardChip::It87952E, 0};
    s.sensors.board_temp_seen = (1u << 0) | (1u << 6);
    s.sensors.board_temps[1] = 35.0f;
    s.sensors.board_temp_ch[1] = {BoardChip::Nct6798D, kNctPeci0};
    s.sensors.board_temp_seen |= 1u << 1;
    pitot::ui::SensorList list{};
    const int n = pitot::ui::ListSensors(s, list);
    CHECK(n == 3);
    CHECK(wcscmp(list[0].name, L"Temp 1") == 0);
    CHECK(wcscmp(list[1].key, L"nct6798d.peci0") == 0);
    CHECK(wcscmp(list[1].name, L"CPU (PECI)") == 0);
    CHECK(wcscmp(list[2].key, L"it87952e.temp1") == 0);
    CHECK(wcscmp(list[2].name, L"Temp 7") == 0);
    pitot::ui::SetLanguage(pitot::ui::Language::Chinese);  // 后面的测试按中文断言
}
