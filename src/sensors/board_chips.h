#pragma once

#include <cstdint>
#include <optional>

// 主板 Super I/O 芯片的识别表、通道布局和读数解码（D-085）。纯函数，有单元测试。
// 芯片 ID、寄存器和公式来自 LHM master LibreHardwareMonitorLib/Hardware/Motherboard/Lpc/
// （LpcIO.cs、Nct677X.cs、IT87XX.cs、F718XX.cs，2026-11-11），出处细节见 docs/hardware.md。

namespace pitot::sensors {

enum class BoardChip : uint8_t {
    None,
    // Nuvoton EC 系：页号 + 索引访问（MSI、部分华擎）
    Nct6683D,
    Nct6686D,
    Nct6687D,
    // Nuvoton bank 系：bank 选择 + 索引访问（华硕、华擎等）
    Nct6779D,
    Nct6791D,
    Nct6792D,
    Nct6793D,
    Nct6795D,
    Nct6796D,
    Nct6796DR,
    Nct6797D,
    Nct6798D,
    Nct6799D,
    // ITE（技嘉、部分华擎）
    It8613E,
    It8620E,
    It8625E,
    It8628E,
    It8631E,
    It8638E,
    It8655E,
    It8665E,
    It8686E,
    It8688E,
    It8689E,
    It8696E,
    It8705F,
    It8712F,
    It8716F,
    It8718F,
    It8720F,
    It8721F,
    It8726F,
    It8728F,
    It8771E,
    It8772E,
    It8790E,
    It8792E,
    It87952E,
    // Fintek
    F71808E,
    F71858,
    F71862,
    F71869,
    F71869A,
    F71878AD,
    F71882,
    F71889AD,
    F71889ED,
    F71889F,
};

enum class BoardFamily : uint8_t { None, NuvotonEc, Nuvoton, Ite, Fintek };

struct BoardChipInfo {
    BoardFamily family = BoardFamily::None;
    const wchar_t* key = L"";   // 传感器键的前缀（D-070），如 nct6798d
    const wchar_t* name = L"";  // 日志用
    uint8_t temps = 0;          // ITE / Fintek：温度通道数；Nuvoton bank 系的温度按来源排，见 NctLayoutFor
    uint8_t fans = 0;
};
const BoardChipInfo& ChipInfo(BoardChip chip);

// 用 0x87 0x87 进入配置模式后读到的芯片 ID / 修订号 → Nuvoton 或 Fintek 芯片；不认识返回 None
BoardChip IdentifyNuvotonFintek(uint8_t id, uint8_t revision);
// 用 ITE 序列进入后读到的 16 位芯片 ID → ITE 芯片；不认识返回 None
BoardChip IdentifyIte(uint16_t id);
// 硬件监控所在的逻辑设备号
uint8_t HardwareMonitorDevice(BoardChip chip);
// NCT6791D 起 BIOS 可能锁住硬件监控的 I/O 映射（配置寄存器 0x28 [4]），读不到厂商 ID 时要解锁
bool HasNuvotonIoLock(BoardChip chip);

// 运行时基址是否可用：两次读一致、≥ 0x100、按 8 字节对齐且在 0x0FFF 以内（LHM IsInvalidRuntimeBase）。
// Fintek 有的芯片读出来已经加了 5（地址寄存器偏移），先去掉
std::optional<uint16_t> ValidateBase(uint16_t address, uint16_t verify);

// ---- Nuvoton bank 系的温度 ----
// 芯片里有若干“监控通道”，每个通道的来源由 BIOS 选（来源寄存器）；另有按来源固定的整数读数寄存器（备用）。
// Pitot 只列下面这些来源，其他（DIMM、PECI 1 等）不显示
enum NctSource : uint8_t {
    kNctSystin = 1,
    kNctCputin = 2,
    kNctAuxtin0 = 3,
    kNctAuxtin1 = 4,
    kNctAuxtin2 = 5,
    kNctAuxtin3 = 6,
    kNctAuxtin4 = 7,
    kNctSmbusMaster0 = 8,
    kNctTsensor = 10,
    kNctPeci0 = 16,
    kNctPchChip = 19,
};
inline constexpr int kNctSourceLabels = 11;
// 来源 → 显示名下标（strings 里 kNctSourceNames 的顺序）、传感器键后缀。不在列表里的来源返回 -1 / 空
int NctSourceLabel(uint8_t source);
const wchar_t* NctSourceKey(uint8_t source);

struct NctTempChannel {
    uint16_t reg;
    uint16_t half_reg;  // 0：没有半度位
    uint8_t half_bit;
    uint16_t source_reg;
};
struct NctTempSlot {
    uint8_t source;
    uint16_t alt_reg;  // 0：没有备用寄存器
};
struct NctLayout {
    const NctTempChannel* channels;
    int channel_count;
    const NctTempSlot* slots;
    int slot_count;
};
NctLayout NctLayoutFor(BoardChip chip);

// 温度 = 0.5 × (有符号整数 × 2 + 半度位)，超出 −55…125 ℃ 视为无效
std::optional<float> DecodeNuvotonTemperature(uint8_t integer, bool half);
// 备用寄存器只有整数，≤ 0 或 > 125 ℃ 视为无效
std::optional<float> DecodeNuvotonAltTemperature(uint8_t integer);

// 按来源把读数放进槽位。read(reg, value) 读一个 bank 寄存器，失败返回 false。
// 同一来源先读到的为准（监控通道带半度，比备用寄存器精确）；槽位没读到的用备用寄存器补
template <class Read>
void ReadNctTemperatures(const NctLayout& layout, Read&& read, std::optional<float>* out) {
    uint64_t seen = 0;
    for (int s = 0; s < layout.slot_count; ++s) {
        out[s].reset();
    }
    for (int c = 0; c < layout.channel_count; ++c) {
        const NctTempChannel& ch = layout.channels[c];
        uint8_t source = 0;
        uint8_t integer = 0;
        if (!read(ch.source_reg, source) || !read(ch.reg, integer)) {
            continue;
        }
        source &= 0x1F;
        if (seen & (1ull << source)) {
            continue;
        }
        bool half = false;
        if (ch.half_reg) {
            uint8_t h = 0;
            if (!read(ch.half_reg, h)) {
                continue;
            }
            half = ((h >> ch.half_bit) & 1) != 0;
        }
        const auto t = DecodeNuvotonTemperature(integer, half);
        if (!t) {
            continue;
        }
        seen |= 1ull << source;
        for (int s = 0; s < layout.slot_count; ++s) {
            if (layout.slots[s].source == source) {
                out[s] = t;
            }
        }
    }
    for (int s = 0; s < layout.slot_count; ++s) {
        const NctTempSlot& slot = layout.slots[s];
        uint8_t integer = 0;
        if (slot.alt_reg && !(seen & (1ull << slot.source)) && read(slot.alt_reg, integer)) {
            out[s] = DecodeNuvotonAltTemperature(integer);
        }
    }
}

// 风扇：13 位计数 = 高字节 << 5 | 低字节 [4:0]，RPM = 1.35e6 / 计数。
// 计数到顶（0x1FFF）是停转，返回 0；太小（< 0x15）是无效读数
std::optional<int> DecodeNuvoton13BitFan(uint8_t high, uint8_t low);

// ---- ITE ----
struct IteTraits {
    bool alt_6th_fan = false;   // 第 6 个风扇在 0x93 / 0x94（IT8665E、IT8625E）
    bool bank_select = false;   // 每轮先切回 bank 0（IT8655E、IT8665E）
    bool unchecked_index = false;  // IT8688E 读回的地址寄存器不等于写入值，不能用来校验
};
IteTraits IteTraitsFor(BoardChip chip);
// 老款 IT8705F（版本 < 3）、IT8712F（版本 < 8）只有 8 位风扇计数
bool IteHas16BitFans(BoardChip chip, uint8_t version);
// 风扇 4、5、6 是否启用了 16 位计数（未启用的不读）。fan16_reg = 0x0C，alt_reg = 0x0B（只有 IT8665E 的第 6 个用）
bool IteFanEnabled(BoardChip chip, int index, uint8_t fan16_reg, uint8_t alt_reg);
// 温度：有符号字节，0 < t < 127 有效
std::optional<float> DecodeIteTemperature(uint8_t value);
// 16 位计数：≤ 0x3F 无效，0xFFFF 是停转，否则 RPM = 1.35e6 / (计数 × 2)
std::optional<int> DecodeIte16BitFan(uint16_t count);
// 8 位计数：0 无效，0xFF 是停转，否则 RPM = 1.35e6 / (计数 × 分频)
std::optional<int> DecodeIte8BitFan(uint8_t count, int divisor);

// ---- Fintek ----
// 温度：0x70 + 2 × (i + 1) 的有符号字节，0 < t < 127 有效
std::optional<float> DecodeFintekTemperature(uint8_t value);
// F71858 的 12 位温度，table_mode = 0x69 [1:0]；高字节 0xBB / 0xCC 表示没接
std::optional<float> DecodeF71858Temperature(uint8_t high, uint8_t low, int table_mode);
// 风扇：16 位计数，0 无效，≥ 0x0FFF 是停转，否则 RPM = 1.5e6 / 计数
std::optional<int> DecodeFintekFan(uint16_t count);

}  // namespace pitot::sensors
