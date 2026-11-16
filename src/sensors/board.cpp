#include "sensors/board.h"

#include <algorithm>

#include "app/resource.h"
#include "common/log.h"
#include "common/names.h"
#include "sensors/decode.h"

namespace pitot::sensors {
namespace {

constexpr DWORD kLockTimeoutMs = 10;
// 识别时拿不到锁就下一轮再试，最多这么多次
constexpr int kMaxDetectAttempts = 30;
// 等 EC 页号变回 0xFF 时最多读这么多次（每次是一次 IOCTL，几微秒）。LHM 会等 500 ms 后强行抢占，
// 这里不等也不抢，直接跳过本轮
constexpr int kEcFreePolls = 32;
// I/O 锁被 BIOS 重新锁上（比如睡眠唤醒后）时，最多每隔这么久再解一次
constexpr ULONGLONG kUnlockRetryMs = 60'000;
// 一颗芯片最多这么多路温度 / 风扇（EC 系 16 路风扇）
constexpr int kMaxChipChannels = 16;

// ---------------------------------------------------------------- Nuvoton EC 系（NCT6683D / 6686D / 6687D）
// 页号 + 索引访问 EC 空间。读完写回 0xFF 释放；EC 被别人占着就跳过本轮，不抢
class NuvotonEcReader final : public BoardChipReader {
public:
    NuvotonEcReader(BoardChip chip, SuperIoPort&& port, uint16_t base)
        : BoardChipReader(chip, std::move(port), base) {}
    [[nodiscard]] int temp_count() const override { return kNctTempCount; }
    [[nodiscard]] int fan_count() const override { return kNctFanCount; }

    bool Read(std::optional<float>* temps, std::optional<int>* fans) override {
        // 先读到临时数组，中途 EC 被占用就整轮作废，避免一半新一半旧
        uint8_t t[kNctTempCount][2];
        uint8_t f[kNctFanCount][2];
        for (int i = 0; i < kNctTempCount; ++i) {
            const auto reg = static_cast<uint16_t>(kNctTempBase + 2 * i);
            if (!EcRead(reg, t[i][0]) || !EcRead(static_cast<uint16_t>(reg + 1), t[i][1])) {
                return false;
            }
        }
        for (int i = 0; i < kNctFanCount; ++i) {
            const auto reg = static_cast<uint16_t>(kNctFanBase + 2 * i);
            if (!EcRead(reg, f[i][0]) || !EcRead(static_cast<uint16_t>(reg + 1), f[i][1])) {
                return false;
            }
        }
        for (int i = 0; i < kNctTempCount; ++i) {
            temps[i] = DecodeNctTemperature(t[i][0], t[i][1]);
        }
        for (int i = 0; i < kNctFanCount; ++i) {
            const int rpm = DecodeNctFan(f[i][0], f[i][1]);
            // 0xFFFF 是读到了无效值
            fans[i] = rpm == 0xFFFF ? std::optional<int>() : std::optional<int>(rpm);
        }
        return true;
    }

    bool EcRead(uint16_t address, uint8_t& value) {
        const auto page_port = static_cast<uint16_t>(base_ + kEcPageOffset);
        uint8_t page = 0;
        bool free = false;
        for (int i = 0; i < kEcFreePolls && !free; ++i) {
            free = port_.In(page_port, page) && page == kEcPageFree;
        }
        if (!free) {
            return false;
        }
        const bool ok = port_.Out(page_port, static_cast<uint8_t>(address >> 8)) &&
                        port_.Out(static_cast<uint16_t>(base_ + kEcIndexOffset), static_cast<uint8_t>(address & 0xFF)) &&
                        port_.In(static_cast<uint16_t>(base_ + kEcDataOffset), value);
        // 不管成功与否都释放
        port_.Out(page_port, kEcPageFree);
        return ok;
    }
};

// ---------------------------------------------------------------- Nuvoton bank 系（NCT6779D–NCT6799D）
class NuvotonReader final : public BoardChipReader {
public:
    NuvotonReader(BoardChip chip, SuperIoPort&& port, uint16_t base)
        : BoardChipReader(chip, std::move(port), base), layout_(NctLayoutFor(chip)) {}
    [[nodiscard]] int temp_count() const override { return layout_.slot_count; }
    [[nodiscard]] int fan_count() const override { return ChipInfo(chip_).fans; }
    [[nodiscard]] uint8_t temp_id(int i) const override { return layout_.slots[i].source; }

    bool Read(std::optional<float>* temps, std::optional<int>* fans) override {
        bank_ = -1;  // 别的程序可能在两轮之间切过 bank
        uint8_t high = 0;
        uint8_t low = 0;
        // 读不到厂商 ID：I/O 映射被锁了（或者芯片没在答话），解锁后下一轮再读
        if (!ReadByte(kNctVendorIdHigh, high) || !ReadByte(kNctVendorIdLow, low) ||
            ((high << 8) | low) != kNuvotonVendorId) {
            Unlock();
            return false;
        }
        bool ok = true;
        ReadNctTemperatures(
            layout_,
            [&](uint16_t reg, uint8_t& value) {
                const bool read = ReadByte(reg, value);
                ok = ok && read;
                return read;
            },
            temps);
        for (int i = 0; i < fan_count(); ++i) {
            if (!ReadByte(kNctFanCountRegs[i], high) || !ReadByte(static_cast<uint16_t>(kNctFanCountRegs[i] + 1), low)) {
                return false;
            }
            fans[i] = DecodeNuvoton13BitFan(high, low);
        }
        return ok;
    }

private:
    bool ReadByte(uint16_t address, uint8_t& value) {
        const auto bank = static_cast<int>(address >> 8);
        const auto index = static_cast<uint16_t>(base_ + kHwmIndexOffset);
        const auto data = static_cast<uint16_t>(base_ + kHwmDataOffset);
        if (bank != bank_) {
            if (!port_.Out(index, kNctBankSelect) || !port_.Out(data, static_cast<uint8_t>(bank))) {
                bank_ = -1;
                return false;
            }
            bank_ = bank;
        }
        return port_.Out(index, static_cast<uint8_t>(address & 0xFF)) && port_.In(data, value);
    }

    // 清掉配置寄存器 0x28 的 I/O 锁位（D-085，用户 2026-11-11 同意）。LHM、FanControl、Linux nct6775 同样处理
    void Unlock() {
        const ULONGLONG now = GetTickCount64();
        if (!HasNuvotonIoLock(chip_) || (last_unlock_ && now - last_unlock_ < kUnlockRetryMs)) {
            return;
        }
        last_unlock_ = now;
        const uint16_t cfg = port_.config_port();
        port_.Out(cfg, kSioEnterKey);
        port_.Out(cfg, kSioEnterKey);
        port_.ConfigWrite(kSioDeviceSelect, kNuvotonHwmLdn);
        const uint8_t options = port_.ConfigRead(kNuvotonIoLockReg);
        if (options & kNuvotonIoLockBit) {
            port_.ConfigWrite(kNuvotonIoLockReg, static_cast<uint8_t>(options & ~kNuvotonIoLockBit));
            log::Write(L"board: %s I/O space was locked (0x%02X), unlocked", ChipInfo(chip_).name, options);
        } else {
            log::Write(L"board: %s vendor ID unreadable, I/O space not locked (0x%02X)", ChipInfo(chip_).name,
                       options);
        }
        port_.Out(cfg, kSioExitKey);
    }

    NctLayout layout_;
    int bank_ = -1;
    ULONGLONG last_unlock_ = 0;
};

// ---------------------------------------------------------------- ITE
class IteReader final : public BoardChipReader {
public:
    IteReader(BoardChip chip, SuperIoPort&& port, uint16_t base, uint8_t version)
        : BoardChipReader(chip, std::move(port), base),
          traits_(IteTraitsFor(chip)),
          has16_(IteHas16BitFans(chip, version)) {}
    [[nodiscard]] int temp_count() const override { return ChipInfo(chip_).temps; }
    [[nodiscard]] int fan_count() const override { return ChipInfo(chip_).fans; }

    // 识别后检查一次（调用方持锁）：厂商 ID、配置寄存器，以及哪些风扇开了 16 位计数
    bool Check() {
        uint8_t vendor = 0;
        if (!ReadByte(kIteVendorIdReg, vendor) || (vendor != 0x90 && vendor != 0x7F)) {
            log::Write(L"board: %s vendor ID 0x%02X not ITE", ChipInfo(chip_).name, vendor);
            return false;
        }
        uint8_t config = 0;
        if (!ReadByte(kIteConfigReg, config) ||
            ((config & 0x10) == 0 && chip_ != BoardChip::It8655E && chip_ != BoardChip::It8665E)) {
            log::Write(L"board: %s configuration 0x%02X unexpected", ChipInfo(chip_).name, config);
            return false;
        }
        uint8_t fan16 = 0;
        uint8_t alt = 0;
        if (has16_ && (!ReadByte(kIteFan16Enable, fan16) || !ReadByte(kIteFan16EnableAlt, alt))) {
            return false;
        }
        for (int i = 0; i < fan_count(); ++i) {
            fan_enabled_[static_cast<size_t>(i)] = !has16_ || IteFanEnabled(chip_, i, fan16, alt);
        }
        return true;
    }

    bool Read(std::optional<float>* temps, std::optional<int>* fans) override {
        uint8_t value = 0;
        if (traits_.bank_select) {
            // bank 选择在睡眠唤醒后会丢，每轮切回 bank 0（LHM issue #780）
            if (!ReadByte(kIteBankReg, value)) {
                return false;
            }
            if (value & 0x60) {
                WriteByte(kIteBankReg, static_cast<uint8_t>(value & 0x9F));
            }
        }
        for (int i = 0; i < temp_count(); ++i) {
            if (!ReadByte(static_cast<uint8_t>(kIteTempBase + i), value)) {
                return false;
            }
            temps[i] = DecodeIteTemperature(value);
        }
        uint8_t divisors = 0;
        if (!has16_ && !ReadByte(kIteFanDivisor, divisors)) {
            return false;
        }
        for (int i = 0; i < fan_count(); ++i) {
            fans[i].reset();
            if (!fan_enabled_[static_cast<size_t>(i)]) {
                continue;
            }
            const bool alt = traits_.alt_6th_fan && i == 5;
            const uint8_t low_reg = alt ? kIteFanLowAlt6 : kIteFanLow[i];
            uint8_t low = 0;
            if (!ReadByte(low_reg, low)) {
                return false;
            }
            if (has16_) {
                uint8_t high = 0;
                if (!ReadByte(alt ? kIteFanHighAlt6 : kIteFanHigh[i], high)) {
                    return false;
                }
                fans[i] = DecodeIte16BitFan(static_cast<uint16_t>((high << 8) | low));
            } else {
                const int divisor = i < 2 ? 1 << ((divisors >> (3 * i)) & 0x7) : 2;
                fans[i] = DecodeIte8BitFan(low, divisor);
            }
        }
        return true;
    }

private:
    // 读回索引寄存器核对，防止别的程序同时改了索引（IT8688E 读回值不可靠，不核对）
    bool ReadByte(uint8_t reg, uint8_t& value) {
        const auto index = static_cast<uint16_t>(base_ + kHwmIndexOffset);
        if (!port_.Out(index, reg) || !port_.In(static_cast<uint16_t>(base_ + kHwmDataOffset), value)) {
            return false;
        }
        if (traits_.unchecked_index) {
            return true;
        }
        uint8_t back = 0;
        return port_.In(index, back) && back == reg;
    }
    void WriteByte(uint8_t reg, uint8_t value) {
        port_.Out(static_cast<uint16_t>(base_ + kHwmIndexOffset), reg);
        port_.Out(static_cast<uint16_t>(base_ + kHwmDataOffset), value);
    }

    IteTraits traits_;
    bool has16_;
    std::array<bool, 6> fan_enabled_{};
};

// ---------------------------------------------------------------- Fintek
class FintekReader final : public BoardChipReader {
public:
    FintekReader(BoardChip chip, SuperIoPort&& port, uint16_t base) : BoardChipReader(chip, std::move(port), base) {}
    [[nodiscard]] int temp_count() const override { return ChipInfo(chip_).temps; }
    [[nodiscard]] int fan_count() const override { return ChipInfo(chip_).fans; }

    bool Read(std::optional<float>* temps, std::optional<int>* fans) override {
        uint8_t high = 0;
        uint8_t low = 0;
        if (chip_ == BoardChip::F71858) {
            uint8_t mode = 0;
            if (!ReadByte(kF71858TempConfig, mode)) {
                return false;
            }
            for (int i = 0; i < temp_count(); ++i) {
                const auto reg = static_cast<uint8_t>(kFintekTempBase + 2 * i);
                if (!ReadByte(reg, high) || !ReadByte(static_cast<uint8_t>(reg + 1), low)) {
                    return false;
                }
                temps[i] = DecodeF71858Temperature(high, low, mode & 0x3);
            }
        } else {
            for (int i = 0; i < temp_count(); ++i) {
                if (!ReadByte(static_cast<uint8_t>(kFintekTempBase + 2 * (i + 1)), high)) {
                    return false;
                }
                temps[i] = DecodeFintekTemperature(high);
            }
        }
        for (int i = 0; i < fan_count(); ++i) {
            if (!ReadByte(kFintekFanRegs[i], high) || !ReadByte(static_cast<uint8_t>(kFintekFanRegs[i] + 1), low)) {
                return false;
            }
            fans[i] = DecodeFintekFan(static_cast<uint16_t>((high << 8) | low));
        }
        return true;
    }

private:
    bool ReadByte(uint8_t reg, uint8_t& value) {
        return port_.Out(static_cast<uint16_t>(base_ + kHwmIndexOffset), reg) &&
               port_.In(static_cast<uint16_t>(base_ + kHwmDataOffset), value);
    }
};

}  // namespace

// ---------------------------------------------------------------- SuperIoPort

DWORD SuperIoPort::Open(int slot) {
    slot_ = slot;
    const DWORD error = module_.Open(IDR_PAWNIO_LPCIO);
    if (error != ERROR_SUCCESS) {
        return error;
    }
    if (!module_.Call("ioctl_select_slot", static_cast<uint64_t>(slot))) {
        return GetLastError() ? GetLastError() : ERROR_GEN_FAILURE;
    }
    return ERROR_SUCCESS;
}

bool SuperIoPort::In(uint16_t port, uint8_t& value) {
    uint64_t out = 0;
    if (!module_.Call("ioctl_pio_inb", port, out)) {
        return false;
    }
    value = static_cast<uint8_t>(out);
    return true;
}

uint8_t SuperIoPort::ConfigRead(uint8_t reg) {
    uint64_t out = 0;
    module_.Call("ioctl_superio_inb", reg, out);
    return static_cast<uint8_t>(out);
}

uint16_t SuperIoPort::ConfigReadWord(uint8_t reg) {
    uint64_t out = 0;
    module_.Call("ioctl_superio_inw", reg, out);
    return static_cast<uint16_t>(out);
}

// ---------------------------------------------------------------- BoardSensors

void BoardSensors::Init() {
    // 先试开一次模块，分清“没装 PawnIO”和“没找到芯片”
    SuperIoPort probe;
    const DWORD error = probe.Open(0);
    if (error != ERROR_SUCCESS) {
        log::Write(L"board: PawnIO LpcIO unavailable, error %lu", error);
        state_ = (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) ? sampler::SensorState::NoDriver
                                                                                  : sampler::SensorState::Failed;
        return;
    }
    if (!isa_mutex_.Open(names::kIsaBusMutex)) {
        log::Write(L"board: cannot open %s, error %lu", names::kIsaBusMutex, GetLastError());
        state_ = sampler::SensorState::Failed;
        return;
    }
    bool busy = false;
    if (Detect(busy)) {
        state_ = sampler::SensorState::Ok;
    } else if (!busy) {
        state_ = sampler::SensorState::Unsupported;
    }
}

bool BoardSensors::Detect(bool& busy) {
    ++detect_attempts_;
    // 加载模块要几毫秒，放在拿锁之前，少让 FanControl 等
    SuperIoPort ports[2];
    bool opened[2] = {};
    for (int slot = 0; slot < 2; ++slot) {
        opened[slot] = ports[slot].Open(slot) == ERROR_SUCCESS;
    }
    SharedMutexLock lock(isa_mutex_, kLockTimeoutMs);
    if (!lock) {
        busy = true;
        return false;
    }
    busy = false;
    chip_count_ = 0;
    for (int slot = 0; slot < 2; ++slot) {
        if (!opened[slot]) {
            continue;
        }
        SuperIoPort& port = ports[slot];
        std::unique_ptr<BoardChipReader> reader = DetectNuvotonFintek(port);
        if (!reader) {
            reader = DetectIte(port);
        }
        if (reader) {
            chips_[static_cast<size_t>(chip_count_++)] = std::move(reader);
        }
    }
    if (chip_count_ == 0) {
        log::Write(L"board: no supported chip found");
        return false;
    }
    BuildLayout();
    return true;
}

std::unique_ptr<BoardChipReader> BoardSensors::DetectNuvotonFintek(SuperIoPort& port) {
    const uint16_t cfg = port.config_port();
    // 进入配置模式（Nuvoton / Fintek：0x87 写两次）
    if (!port.Out(cfg, kSioEnterKey) || !port.Out(cfg, kSioEnterKey)) {
        return nullptr;
    }
    const uint8_t id = port.ConfigRead(kSioChipId);
    const uint8_t revision = port.ConfigRead(kSioChipRevision);
    const BoardChip chip = IdentifyNuvotonFintek(id, revision);
    if (chip == BoardChip::None) {
        if (id != 0 && id != 0xFF) {
            log::Write(L"board: slot %d has unknown Nuvoton/Fintek chip %02X:%02X", port.slot(), id, revision);
            port.Out(cfg, kSioExitKey);
        }
        return nullptr;
    }
    port.FindBars();
    port.ConfigWrite(kSioDeviceSelect, HardwareMonitorDevice(chip));
    const uint16_t address = port.ConfigReadWord(kSioBaseAddress);
    Sleep(1);
    const uint16_t verify = port.ConfigReadWord(kSioBaseAddress);
    const uint16_t vendor = port.ConfigReadWord(kFintekVendorIdReg);
    if (HasNuvotonIoLock(chip)) {
        // BIOS 锁住了硬件监控的 I/O 映射就解开（D-085）
        const uint8_t options = port.ConfigRead(kNuvotonIoLockReg);
        if (options & kNuvotonIoLockBit) {
            port.ConfigWrite(kNuvotonIoLockReg, static_cast<uint8_t>(options & ~kNuvotonIoLockBit));
            log::Write(L"board: %s I/O space was locked (0x%02X), unlocked", ChipInfo(chip).name, options);
        }
    }
    port.Out(cfg, kSioExitKey);

    const auto base = ValidateBase(address, verify);
    if (!base) {
        log::Write(L"board: %s at slot %d has invalid base 0x%04X / 0x%04X", ChipInfo(chip).name, port.slot(), address,
                   verify);
        return nullptr;
    }
    const BoardFamily family = ChipInfo(chip).family;
    if (family == BoardFamily::Fintek && vendor != kFintekVendorId) {
        log::Write(L"board: %s vendor ID 0x%04X not Fintek", ChipInfo(chip).name, vendor);
        return nullptr;
    }
    log::Write(L"board: found %s at slot %d, base 0x%04X", ChipInfo(chip).name, port.slot(), *base);
    switch (family) {
    case BoardFamily::NuvotonEc: {
        auto reader = std::make_unique<NuvotonEcReader>(chip, std::move(port), *base);
        uint8_t control = 0;
        const bool read = reader->EcRead(kNctMonitorControl, control);
        log::Write(L"board: monitor control %s0x%02X", read ? L"" : L"(unread) ", control);
        return reader;
    }
    case BoardFamily::Nuvoton:
        return std::make_unique<NuvotonReader>(chip, std::move(port), *base);
    case BoardFamily::Fintek:
        return std::make_unique<FintekReader>(chip, std::move(port), *base);
    default:
        return nullptr;
    }
}

std::unique_ptr<BoardChipReader> BoardSensors::DetectIte(SuperIoPort& port) {
    const uint16_t cfg = port.config_port();
    const bool secondary = port.slot() == 1;
    // 0x4E 上的第二颗 ITE 如果已在配置模式（ID 读得出来），不要再进一次：IT8792E 会因此报错 ID（LHM）
    uint16_t id = secondary ? port.ConfigReadWord(kSioChipId) : 0xFFFF;
    if (id == 0xFFFF) {
        for (int i = 0; i < 3; ++i) {
            port.Out(cfg, kIteEnterKeys[i]);
        }
        port.Out(cfg, secondary ? kIteEnterLastKey4E : kIteEnterKeys[3]);
        id = port.ConfigReadWord(kSioChipId);
    }
    auto exit = [&] {
        // 0x4E 上的第二颗不退出配置模式（LHM 的做法）
        if (!secondary) {
            port.ConfigWrite(kIteConfigControl, 0x02);
        }
    };
    const BoardChip chip = IdentifyIte(id);
    if (chip == BoardChip::None) {
        if (id != 0 && id != 0xFFFF) {
            log::Write(L"board: slot %d has unknown ITE chip %04X", port.slot(), id);
            exit();
        }
        return nullptr;
    }
    port.FindBars();
    port.ConfigWrite(kSioDeviceSelect, kIteHwmLdn);
    const uint16_t address = port.ConfigReadWord(kSioBaseAddress);
    Sleep(1);
    const uint16_t verify = port.ConfigReadWord(kSioBaseAddress);
    const auto version = static_cast<uint8_t>(port.ConfigRead(kIteChipVersion) & 0x0F);
    exit();

    const auto base = ValidateBase(address, verify);
    if (!base) {
        log::Write(L"board: %s at slot %d has invalid base 0x%04X / 0x%04X", ChipInfo(chip).name, port.slot(), address,
                   verify);
        return nullptr;
    }
    const int slot = port.slot();
    auto reader = std::make_unique<IteReader>(chip, std::move(port), *base, version);
    if (!reader->Check()) {
        return nullptr;
    }
    log::Write(L"board: found %s version %u at slot %d, base 0x%04X", ChipInfo(chip).name, version, slot, *base);
    return reader;
}

void BoardSensors::BuildLayout() {
    int temp_slot = 0;
    int fan_slot = 0;
    for (int c = 0; c < chip_count_; ++c) {
        const BoardChipReader& chip = *chips_[static_cast<size_t>(c)];
        const auto ci = static_cast<size_t>(c);
        temp_offset_[ci] = temp_slot;
        fan_offset_[ci] = fan_slot;
        temp_slots_[ci] = std::min(chip.temp_count(), sampler::kBoardTemps - temp_slot);
        fan_slots_[ci] = std::min(chip.fan_count(), sampler::kBoardFans - fan_slot);
        for (int i = 0; i < temp_slots_[ci]; ++i) {
            temp_ch_[static_cast<size_t>(temp_slot++)] = {chip.chip(), chip.temp_id(i)};
        }
        for (int i = 0; i < fan_slots_[ci]; ++i) {
            fan_ch_[static_cast<size_t>(fan_slot++)] = {chip.chip(), static_cast<uint8_t>(i)};
        }
    }
    const BoardChip second = chip_count_ > 1 ? chips_[1]->chip() : BoardChip::None;
    layout_ = static_cast<uint16_t>((static_cast<unsigned>(chips_[0]->chip()) << 8) | static_cast<unsigned>(second));
}

void BoardSensors::Sample(sampler::SensorReadings& r) {
    if (state_ == sampler::SensorState::Starting && detect_attempts_ > 0) {
        if (detect_attempts_ >= kMaxDetectAttempts) {
            state_ = sampler::SensorState::Failed;
            return;
        }
        bool busy = false;
        if (Detect(busy)) {
            state_ = sampler::SensorState::Ok;
        } else if (!busy) {
            state_ = sampler::SensorState::Unsupported;
        }
    }
    if (state_ != sampler::SensorState::Ok) {
        return;
    }

    SharedMutexLock lock(isa_mutex_, kLockTimeoutMs);
    if (!lock) {
        return;  // 本轮保留上一次读数
    }
    for (int c = 0; c < chip_count_; ++c) {
        const auto ci = static_cast<size_t>(c);
        // 超出快照槽位的通道丢掉
        std::optional<float> temps[kMaxChipChannels];
        std::optional<int> fans[kMaxChipChannels];
        if (!chips_[ci]->Read(temps, fans)) {
            continue;  // 这颗芯片本轮作废，保留上一次
        }
        for (int i = 0; i < temp_slots_[ci]; ++i) {
            const int slot = temp_offset_[ci] + i;
            r.board_temps[static_cast<size_t>(slot)] = temps[i];
            if (temps[i]) {
                temp_seen_ |= 1u << slot;
            }
        }
        for (int i = 0; i < fan_slots_[ci]; ++i) {
            const int slot = fan_offset_[ci] + i;
            r.fans[static_cast<size_t>(slot)] = fans[i];
            if (fans[i] && *fans[i] > 0) {
                fan_seen_ |= 1u << slot;
            }
        }
    }
    r.board_temp_seen = temp_seen_;
    r.fan_seen = fan_seen_;
    r.board_temp_ch = temp_ch_;
    r.fan_ch = fan_ch_;
    r.board_layout = layout_;
}

}  // namespace pitot::sensors
