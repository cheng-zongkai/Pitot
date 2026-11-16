#include "sampler/smbios.h"

#include <windows.h>

#include <algorithm>
#include <vector>

namespace pitot::sampler {
namespace {

// SMBIOS 规范 DSP0134 3.x 第 7.18 节 Memory Device (Type 17)
constexpr uint8_t kTypeMemoryDevice = 17;
constexpr uint8_t kTypeEndOfTable = 127;
constexpr size_t kHeaderSize = 4;                  // type、length、handle
constexpr size_t kOffsetSize = 0x0C;               // WORD，0 表示插槽空着
constexpr size_t kOffsetConfiguredSpeed = 0x20;    // WORD，2.7+；0 未知，0xFFFF 看扩展字段
constexpr size_t kOffsetExtConfiguredSpeed = 0x58;  // DWORD，3.3+，[30:0]
constexpr uint16_t kSpeedUseExtended = 0xFFFF;
constexpr uint32_t kExtSpeedMask = 0x7FFFFFFF;
// GetSystemFirmwareTable 返回的 RawSMBIOSData 头：调用方式、主次版本、DMI 修订、表长度
constexpr size_t kRawHeaderSize = 8;
constexpr DWORD kProviderRsmb = 'RSMB';

uint16_t Word(std::span<const uint8_t> t, size_t at) {
    return static_cast<uint16_t>(t[at] | (t[at + 1] << 8));
}

uint32_t Dword(std::span<const uint8_t> t, size_t at) {
    return static_cast<uint32_t>(Word(t, at)) | (static_cast<uint32_t>(Word(t, at + 2)) << 16);
}

}  // namespace

std::optional<uint32_t> ParseMemorySpeed(std::span<const uint8_t> t) {
    std::optional<uint32_t> slowest;
    size_t at = 0;
    while (at + kHeaderSize <= t.size()) {
        const uint8_t type = t[at];
        const uint8_t length = t[at + 1];
        if (length < kHeaderSize || at + length > t.size() || type == kTypeEndOfTable) {
            break;
        }
        if (type == kTypeMemoryDevice && length >= kOffsetConfiguredSpeed + 2 && Word(t, at + kOffsetSize) != 0) {
            uint32_t speed = Word(t, at + kOffsetConfiguredSpeed);
            if (speed == kSpeedUseExtended) {
                speed = length >= kOffsetExtConfiguredSpeed + 4 ? Dword(t, at + kOffsetExtConfiguredSpeed) & kExtSpeedMask
                                                                : 0;
            }
            if (speed != 0) {
                slowest = slowest ? std::min(*slowest, speed) : speed;
            }
        }
        // 格式化区之后是字符串区，以两个连续的 0 结束
        size_t p = at + length;
        while (p + 1 < t.size() && (t[p] != 0 || t[p + 1] != 0)) {
            ++p;
        }
        at = p + 2;
    }
    return slowest;
}

std::optional<uint32_t> ReadMemorySpeed() {
    const UINT size = GetSystemFirmwareTable(kProviderRsmb, 0, nullptr, 0);
    if (size <= kRawHeaderSize) {
        return std::nullopt;
    }
    std::vector<uint8_t> raw(size);
    if (GetSystemFirmwareTable(kProviderRsmb, 0, raw.data(), size) != size) {
        return std::nullopt;
    }
    const uint32_t length = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (static_cast<uint32_t>(raw[7]) << 24);
    const size_t usable = std::min<size_t>(length, size - kRawHeaderSize);
    return ParseMemorySpeed(std::span<const uint8_t>(raw.data() + kRawHeaderSize, usable));
}

}  // namespace pitot::sampler
