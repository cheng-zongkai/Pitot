#pragma once

#include <windows.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include "sampler/snapshot.h"
#include "sensors/board_chips.h"
#include "sensors/pawnio.h"

namespace pitot::sensors {

// 一个 Super I/O 插槽（0x2E 或 0x4E）上的 PawnIO LpcIO 模块实例。
// 模块在 select_slot 时会清掉已登记的端口范围，所以两颗芯片各用一个实例
class SuperIoPort {
public:
    DWORD Open(int slot);
    [[nodiscard]] int slot() const noexcept { return slot_; }
    [[nodiscard]] uint16_t config_port() const noexcept { return slot_ == 0 ? 0x2E : 0x4E; }

    bool Out(uint16_t port, uint8_t value) { return module_.Call2("ioctl_pio_outb", port, value); }
    bool In(uint16_t port, uint8_t& value);
    uint8_t ConfigRead(uint8_t reg);
    uint16_t ConfigReadWord(uint8_t reg);
    bool ConfigWrite(uint8_t reg, uint8_t value) { return module_.Call2("ioctl_superio_outb", reg, value); }
    // 让模块登记芯片的端口范围，之后才能访问硬件监控的端口
    void FindBars() { module_.Execute("ioctl_find_bars", nullptr, 0, nullptr, 0); }

private:
    PawnIoModule module_;
    int slot_ = 0;
};

// 一颗芯片的读取器。温度 / 风扇按芯片内序号排，BoardSensors 再把它们接到快照的槽位上
class BoardChipReader {
public:
    virtual ~BoardChipReader() = default;
    [[nodiscard]] BoardChip chip() const noexcept { return chip_; }
    [[nodiscard]] virtual int temp_count() const = 0;
    [[nodiscard]] virtual int fan_count() const = 0;
    [[nodiscard]] virtual uint8_t temp_id(int i) const { return static_cast<uint8_t>(i); }
    // 读一轮（调用方已持 ISA 锁）。返回 false 表示本轮作废，调用方保留上一轮
    virtual bool Read(std::optional<float>* temps, std::optional<int>* fans) = 0;

protected:
    BoardChipReader(BoardChip chip, SuperIoPort&& port, uint16_t base)
        : chip_(chip), port_(std::move(port)), base_(base) {}

    BoardChip chip_;
    SuperIoPort port_;
    uint16_t base_;
};

// 主板温度和风扇（D-028、D-085）。识别 0x2E / 0x4E 上的 Nuvoton、ITE、Fintek 芯片，最多两颗。
// 访问期间持 Global\Access_ISABUS.HTP.Method，最多等 10 ms，拿不到就跳过本轮
class BoardSensors {
public:
    void Init();
    [[nodiscard]] sampler::SensorState state() const noexcept { return state_; }
    void Sample(sampler::SensorReadings& r);

private:
    // 返回 true 表示找到至少一颗芯片；锁拿不到时 busy 为真，下一轮再试
    bool Detect(bool& busy);
    std::unique_ptr<BoardChipReader> DetectNuvotonFintek(SuperIoPort& port);
    std::unique_ptr<BoardChipReader> DetectIte(SuperIoPort& port);
    void BuildLayout();

    SharedMutex isa_mutex_;
    sampler::SensorState state_ = sampler::SensorState::Starting;
    int detect_attempts_ = 0;
    std::array<std::unique_ptr<BoardChipReader>, 2> chips_;
    int chip_count_ = 0;

    // 每颗芯片在快照里的起始槽位和通道数
    std::array<int, 2> temp_offset_{};
    std::array<int, 2> fan_offset_{};
    std::array<int, 2> temp_slots_{};
    std::array<int, 2> fan_slots_{};
    std::array<sampler::BoardChannel, sampler::kBoardTemps> temp_ch_{};
    std::array<sampler::BoardChannel, sampler::kBoardFans> fan_ch_{};
    uint16_t layout_ = 0;
    uint32_t temp_seen_ = 0;
    uint32_t fan_seen_ = 0;
};

}  // namespace pitot::sensors
