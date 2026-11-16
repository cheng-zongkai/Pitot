#include "sensors/board_chips.h"

#include <iterator>

#include "sensors/decode.h"

namespace pitot::sensors {
namespace {

using F = BoardFamily;

// 下标与 BoardChip 的顺序一致（static_assert 检查个数）。ITE / Fintek 的通道数取自 LHM 各芯片的构造函数
constexpr BoardChipInfo kChips[] = {
    {F::None, L"", L"none", 0, 0},
    {F::NuvotonEc, L"nct6683d", L"NCT6683D", 11, 16},
    {F::NuvotonEc, L"nct6686d", L"NCT6686D", 11, 16},
    {F::NuvotonEc, L"nct6687d", L"NCT6687D", 11, 16},
    {F::Nuvoton, L"nct6779d", L"NCT6779D", 0, 5},
    {F::Nuvoton, L"nct6791d", L"NCT6791D", 0, 6},
    {F::Nuvoton, L"nct6792d", L"NCT6792D", 0, 6},
    {F::Nuvoton, L"nct6793d", L"NCT6793D", 0, 6},
    {F::Nuvoton, L"nct6795d", L"NCT6795D", 0, 6},
    {F::Nuvoton, L"nct6796d", L"NCT6796D", 0, 6},
    {F::Nuvoton, L"nct6796dr", L"NCT6796D-R", 0, 7},
    {F::Nuvoton, L"nct6797d", L"NCT6797D", 0, 7},
    {F::Nuvoton, L"nct6798d", L"NCT6798D", 0, 7},
    {F::Nuvoton, L"nct6799d", L"NCT6799D", 0, 7},
    {F::Ite, L"it8613e", L"IT8613E", 4, 5},
    {F::Ite, L"it8620e", L"IT8620E", 3, 5},
    {F::Ite, L"it8625e", L"IT8625E", 3, 6},
    {F::Ite, L"it8628e", L"IT8628E", 6, 6},
    {F::Ite, L"it8631e", L"IT8631E", 2, 2},
    {F::Ite, L"it8638e", L"IT8638E", 2, 2},
    {F::Ite, L"it8655e", L"IT8655E", 6, 3},
    {F::Ite, L"it8665e", L"IT8665E", 6, 6},
    {F::Ite, L"it8686e", L"IT8686E", 7, 6},
    {F::Ite, L"it8688e", L"IT8688E", 6, 6},
    {F::Ite, L"it8689e", L"IT8689E", 6, 6},
    {F::Ite, L"it8696e", L"IT8696E", 6, 6},
    {F::Ite, L"it8705f", L"IT8705F", 3, 3},
    {F::Ite, L"it8712f", L"IT8712F", 3, 5},
    {F::Ite, L"it8716f", L"IT8716F", 3, 5},
    {F::Ite, L"it8718f", L"IT8718F", 3, 5},
    {F::Ite, L"it8720f", L"IT8720F", 3, 5},
    {F::Ite, L"it8721f", L"IT8721F", 3, 5},
    {F::Ite, L"it8726f", L"IT8726F", 3, 5},
    {F::Ite, L"it8728f", L"IT8728F", 3, 5},
    {F::Ite, L"it8771e", L"IT8771E", 3, 5},
    {F::Ite, L"it8772e", L"IT8772E", 3, 5},
    {F::Ite, L"it8790e", L"IT8790E", 3, 5},
    {F::Ite, L"it8792e", L"IT8792E", 3, 3},
    {F::Ite, L"it87952e", L"IT87952E", 3, 3},
    {F::Fintek, L"f71808e", L"F71808E", 2, 3},
    {F::Fintek, L"f71858", L"F71858", 3, 4},
    {F::Fintek, L"f71862", L"F71862", 3, 3},
    {F::Fintek, L"f71869", L"F71869", 3, 3},
    {F::Fintek, L"f71869a", L"F71869A", 3, 3},
    {F::Fintek, L"f71878ad", L"F71878AD", 3, 3},
    {F::Fintek, L"f71882", L"F71882", 3, 4},
    {F::Fintek, L"f71889ad", L"F71889AD", 3, 3},
    {F::Fintek, L"f71889ed", L"F71889ED", 3, 3},
    {F::Fintek, L"f71889f", L"F71889F", 3, 3},
};
static_assert(std::size(kChips) == static_cast<size_t>(BoardChip::F71889F) + 1);

// ---- Nuvoton bank 系的温度布局（LHM Nct677X 构造函数的 TemperatureSourceData 表） ----

// NCT6791D–NCT6799D 共用的监控通道；TSENSOR 通道只有 NCT6798D / NCT6799D 有，放在最后单独加
constexpr NctTempChannel kNct679xChannels[] = {
    {0x073, 0x074, 7, 0x100}, {0x075, 0x076, 7, 0x200}, {0x077, 0x078, 7, 0x300}, {0x079, 0x07A, 7, 0x800},
    {0x07B, 0x07C, 7, 0x900}, {0x07D, 0x07E, 7, 0xA00}, {0x4A0, 0x49E, 6, 0xB00}, {0x027, 0, 0, 0x621},
    {0x150, 0x151, 7, 0x622}, {0x670, 0, 0, 0xC26},     {0x672, 0, 0, 0xC27},     {0x674, 0, 0, 0xC28},
    {0x676, 0, 0, 0xC29},     {0x678, 0, 0, 0xC2A},     {0x67A, 0, 0, 0xC2B},     {0x4A2, 0x4A1, 7, 0xC00},
};
constexpr NctTempSlot kNct679xSlots[] = {
    {kNctPeci0, 0},          {kNctCputin, 0x491},  {kNctSystin, 0x490},  {kNctAuxtin0, 0x492},
    {kNctAuxtin1, 0x493},    {kNctAuxtin2, 0x494}, {kNctAuxtin3, 0x495}, {kNctAuxtin4, 0},
    {kNctSmbusMaster0, 0},   {kNctPchChip, 0x401}, {kNctTsensor, 0x496},
};

// NCT6779D
constexpr NctTempChannel kNct6779Channels[] = {
    {0x027, 0, 0, 0x621},     {0x073, 0x074, 7, 0x100}, {0x075, 0x076, 7, 0x200}, {0x077, 0x078, 7, 0x300},
    {0x079, 0x07A, 7, 0x800}, {0x07B, 0x07C, 7, 0x900}, {0x150, 0x151, 7, 0x622},
};
constexpr NctTempSlot kNct6779Slots[] = {
    {kNctPeci0, 0},       {kNctCputin, 0x491},  {kNctSystin, 0x490},  {kNctAuxtin0, 0x492},
    {kNctAuxtin1, 0x493}, {kNctAuxtin2, 0x494}, {kNctAuxtin3, 0x495},
};

constexpr int kTsensorChannels = 1;
constexpr int kTsensorSlots = 1;

}  // namespace

const BoardChipInfo& ChipInfo(BoardChip chip) {
    const auto i = static_cast<size_t>(chip);
    return i < std::size(kChips) ? kChips[i] : kChips[0];
}

BoardChip IdentifyNuvotonFintek(uint8_t id, uint8_t revision) {
    switch (id) {
    case 0x05:
        return revision == 0x07 ? BoardChip::F71858 : revision == 0x41 ? BoardChip::F71882 : BoardChip::None;
    case 0x06:
        return revision == 0x01 ? BoardChip::F71862 : BoardChip::None;
    case 0x07:
        return revision == 0x23 ? BoardChip::F71889F : BoardChip::None;
    case 0x08:
        return revision == 0x14 ? BoardChip::F71869 : BoardChip::None;
    case 0x09:
        return revision == 0x01 ? BoardChip::F71808E : revision == 0x09 ? BoardChip::F71889ED : BoardChip::None;
    case 0x10:
        return revision == 0x05 ? BoardChip::F71889AD : revision == 0x07 ? BoardChip::F71869A : BoardChip::None;
    case 0x11:
        // 0x18 是 F71811，寄存器和 F71869A 相同（LHM 里两者是同一个枚举值）
        return revision == 0x06 ? BoardChip::F71878AD : revision == 0x18 ? BoardChip::F71869A : BoardChip::None;
    case 0xC5:
        return (revision & 0xF0) == 0x60 ? BoardChip::Nct6779D : BoardChip::None;
    case 0xC7:
        return revision == 0x32 ? BoardChip::Nct6683D : BoardChip::None;
    case 0xC8:
        return revision == 0x03 ? BoardChip::Nct6791D : BoardChip::None;
    case 0xC9:
        // 0x13 是 NCT6792D-A，读法相同
        return (revision == 0x11 || revision == 0x13) ? BoardChip::Nct6792D : BoardChip::None;
    case 0xD1:
        return revision == 0x21 ? BoardChip::Nct6793D : BoardChip::None;
    case 0xD3:
        return revision == 0x52 ? BoardChip::Nct6795D : BoardChip::None;
    case 0xD4:
        switch (revision) {
        case 0x23:
            return BoardChip::Nct6796D;
        case 0x2A:
            return BoardChip::Nct6796DR;
        case 0x2B:
            return BoardChip::Nct6798D;
        case 0x40:
        case 0x41:
            return BoardChip::Nct6686D;
        case 0x51:
            return BoardChip::Nct6797D;
        default:
            return BoardChip::None;
        }
    case 0xD5:
        return revision == kNct6687Revision ? BoardChip::Nct6687D : BoardChip::None;
    case 0xD8:
        // 0x02 也可能是 NCT6796D-S（LHM 按主板型号区分），这里按 NCT6799D 读
        return revision == 0x02 ? BoardChip::Nct6799D : BoardChip::None;
    default:
        return BoardChip::None;
    }
}

BoardChip IdentifyIte(uint16_t id) {
    switch (id) {
    case 0x8613: return BoardChip::It8613E;
    case 0x8620: return BoardChip::It8620E;
    case 0x8625: return BoardChip::It8625E;
    case 0x8628: return BoardChip::It8628E;
    case 0x8631: return BoardChip::It8631E;
    case 0x8638: return BoardChip::It8638E;
    case 0x8655: return BoardChip::It8655E;
    case 0x8665: return BoardChip::It8665E;
    case 0x8686: return BoardChip::It8686E;
    case 0x8688: return BoardChip::It8688E;
    case 0x8689: return BoardChip::It8689E;
    case 0x8696: return BoardChip::It8696E;
    case 0x8705: return BoardChip::It8705F;
    case 0x8712: return BoardChip::It8712F;
    case 0x8716: return BoardChip::It8716F;
    case 0x8718: return BoardChip::It8718F;
    case 0x8720: return BoardChip::It8720F;
    case 0x8721: return BoardChip::It8721F;
    case 0x8726: return BoardChip::It8726F;
    case 0x8728: return BoardChip::It8728F;
    case 0x8771: return BoardChip::It8771E;
    case 0x8772: return BoardChip::It8772E;
    case 0x8790: return BoardChip::It8790E;
    case 0x8733: return BoardChip::It8792E;  // 也可能是 IT8791E / IT8795E
    case 0x8695: return BoardChip::It87952E;
    default: return BoardChip::None;
    }
}

uint8_t HardwareMonitorDevice(BoardChip chip) {
    switch (ChipInfo(chip).family) {
    case BoardFamily::NuvotonEc:
    case BoardFamily::Nuvoton:
        return kNuvotonHwmLdn;
    case BoardFamily::Ite:
        return kIteHwmLdn;
    case BoardFamily::Fintek:
        return chip == BoardChip::F71858 ? kF71858HwmLdn : kFintekHwmLdn;
    default:
        return 0;
    }
}

bool HasNuvotonIoLock(BoardChip chip) {
    return ChipInfo(chip).family == BoardFamily::Nuvoton && chip != BoardChip::Nct6779D;
}

std::optional<uint16_t> ValidateBase(uint16_t address, uint16_t verify) {
    if (address != verify) {
        return std::nullopt;
    }
    if ((address & 0x07) == 0x05) {
        address &= 0xFFF8;
    }
    if (address < 0x100 || (address & 0xF007) != 0) {
        return std::nullopt;
    }
    return address;
}

int NctSourceLabel(uint8_t source) {
    switch (source) {
    case kNctSystin: return 0;
    case kNctCputin: return 1;
    case kNctAuxtin0: return 2;
    case kNctAuxtin1: return 3;
    case kNctAuxtin2: return 4;
    case kNctAuxtin3: return 5;
    case kNctAuxtin4: return 6;
    case kNctSmbusMaster0: return 7;
    case kNctTsensor: return 8;
    case kNctPeci0: return 9;
    case kNctPchChip: return 10;
    default: return -1;
    }
}

const wchar_t* NctSourceKey(uint8_t source) {
    static constexpr const wchar_t* kKeys[kNctSourceLabels] = {
        L"systin", L"cputin", L"auxtin0", L"auxtin1", L"auxtin2", L"auxtin3",
        L"auxtin4", L"smbus0", L"tsensor", L"peci0", L"pch"};
    const int label = NctSourceLabel(source);
    return label >= 0 ? kKeys[label] : L"";
}

NctLayout NctLayoutFor(BoardChip chip) {
    switch (chip) {
    case BoardChip::Nct6779D:
        return {kNct6779Channels, static_cast<int>(std::size(kNct6779Channels)), kNct6779Slots,
                static_cast<int>(std::size(kNct6779Slots))};
    case BoardChip::Nct6798D:
    case BoardChip::Nct6799D:
        return {kNct679xChannels, static_cast<int>(std::size(kNct679xChannels)), kNct679xSlots,
                static_cast<int>(std::size(kNct679xSlots))};
    case BoardChip::Nct6791D:
    case BoardChip::Nct6792D:
    case BoardChip::Nct6793D:
    case BoardChip::Nct6795D:
    case BoardChip::Nct6796D:
    case BoardChip::Nct6796DR:
    case BoardChip::Nct6797D:
        // 没有 TSENSOR：去掉两张表的最后一项
        return {kNct679xChannels, static_cast<int>(std::size(kNct679xChannels)) - kTsensorChannels, kNct679xSlots,
                static_cast<int>(std::size(kNct679xSlots)) - kTsensorSlots};
    default:
        return {nullptr, 0, nullptr, 0};
    }
}

std::optional<float> DecodeNuvotonTemperature(uint8_t integer, bool half) {
    const float t = static_cast<float>(static_cast<int8_t>(integer)) + (half ? 0.5f : 0.0f);
    if (t > 125.0f || t < -55.0f) {
        return std::nullopt;
    }
    return t;
}

std::optional<float> DecodeNuvotonAltTemperature(uint8_t integer) {
    const auto t = static_cast<float>(static_cast<int8_t>(integer));
    if (t > 125.0f || t <= 0.0f) {
        return std::nullopt;
    }
    return t;
}

std::optional<int> DecodeNuvoton13BitFan(uint8_t high, uint8_t low) {
    const int count = (high << 5) | (low & 0x1F);
    if (count >= kNctFanMaxCount) {
        return 0;
    }
    if (count < kNctFanMinCount) {
        return std::nullopt;
    }
    return static_cast<int>(1.35e6 / count + 0.5);
}

IteTraits IteTraitsFor(BoardChip chip) {
    IteTraits t;
    t.alt_6th_fan = chip == BoardChip::It8665E || chip == BoardChip::It8625E;
    t.bank_select = chip == BoardChip::It8665E || chip == BoardChip::It8655E;
    t.unchecked_index = chip == BoardChip::It8688E;
    return t;
}

bool IteHas16BitFans(BoardChip chip, uint8_t version) {
    return (chip != BoardChip::It8705F || version >= 3) && (chip != BoardChip::It8712F || version >= 8);
}

bool IteFanEnabled(BoardChip chip, int index, uint8_t fan16_reg, uint8_t alt_reg) {
    switch (index) {
    case 3:
        return (fan16_reg & (1 << 4)) != 0;
    case 4:
        return (fan16_reg & (1 << 5)) != 0;
    case 5:
        return chip == BoardChip::It8665E ? (alt_reg & (1 << 3)) != 0 : (fan16_reg & (1 << 2)) != 0;
    default:
        return true;
    }
}

std::optional<float> DecodeIteTemperature(uint8_t value) {
    const auto t = static_cast<int8_t>(value);
    if (t <= 0 || t >= 127) {
        return std::nullopt;
    }
    return static_cast<float>(t);
}

std::optional<int> DecodeIte16BitFan(uint16_t count) {
    if (count <= 0x3F) {
        return std::nullopt;
    }
    if (count == 0xFFFF) {
        return 0;
    }
    return static_cast<int>(1.35e6 / (count * 2.0) + 0.5);
}

std::optional<int> DecodeIte8BitFan(uint8_t count, int divisor) {
    if (count == 0 || divisor <= 0) {
        return std::nullopt;
    }
    if (count == 0xFF) {
        return 0;
    }
    return static_cast<int>(1.35e6 / (count * static_cast<double>(divisor)) + 0.5);
}

std::optional<float> DecodeFintekTemperature(uint8_t value) { return DecodeIteTemperature(value); }

std::optional<float> DecodeF71858Temperature(uint8_t high, uint8_t low, int table_mode) {
    if (high == 0xBB || high == 0xCC) {
        return std::nullopt;
    }
    int bits = 0;
    if (table_mode == 2) {
        bits = (high & 0x80) << 8;
    } else if (table_mode == 3) {
        bits = (low & 0x01) << 15;
    }
    bits |= high << 7;
    bits |= (low & 0xE0) >> 1;
    const auto value = static_cast<int16_t>(bits & 0xFFF0);
    return static_cast<float>(value) / 128.0f;
}

std::optional<int> DecodeFintekFan(uint16_t count) {
    if (count == 0) {
        return std::nullopt;
    }
    if (count >= 0x0FFF) {
        return 0;
    }
    return static_cast<int>(1.5e6 / count + 0.5);
}

}  // namespace pitot::sensors
