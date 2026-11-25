#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace pitot::sampler {

// 内存运行速度，单位 MT/s，口径同任务管理器“速度”：SMBIOS Type 17（Memory Device）的
// Configured Memory Speed。只看装了内存条的插槽；各条速度不同时取最小值（实际都按最慢的跑）。
// 偏移见 docs/subsystems/memory.md 的 SMBIOS 小节。
// table 是 SMBIOS 结构表本身（不含 GetSystemFirmwareTable 返回的 8 字节 RawSMBIOSData 头）。
std::optional<uint32_t> ParseMemorySpeed(std::span<const uint8_t> table);

// 用 GetSystemFirmwareTable('RSMB') 读整张表再解析。启动时调一次
std::optional<uint32_t> ReadMemorySpeed();

}  // namespace pitot::sampler
