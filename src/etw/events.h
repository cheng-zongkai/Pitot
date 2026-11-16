#pragma once

#include <guiddef.h>

#include <cstddef>
#include <cstdint>

namespace pitot::etw {

// 系统记录器会话（D-007、D-037）收到的内核事件。
// 事件类 GUID、操作码和字段偏移见 docs/hardware.md "ETW Kernel Events"，2026-11-04 用 TDH 在本机逐项核对过。

// 事件类 GUID（MOF 类 Thread、DiskIo、TcpIp、UdpIp）
inline constexpr GUID kThreadGuid = {0x3d6fa8d1, 0xfe05, 0x11d0, {0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};
inline constexpr GUID kDiskIoGuid = {0x3d6fa8d4, 0xfe05, 0x11d0, {0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c}};
inline constexpr GUID kTcpIpGuid = {0x9a280ac0, 0xc8e0, 0x11d1, {0x84, 0xe2, 0x00, 0xc0, 0x4f, 0xb9, 0x98, 0xa2}};
inline constexpr GUID kUdpIpGuid = {0xbf3a50c5, 0xa9c9, 0x4988, {0xa0, 0x05, 0x2d, 0xf0, 0xb7, 0xc8, 0x0f, 0x80}};

// 操作码
inline constexpr uint8_t kThreadStart = 1;
inline constexpr uint8_t kThreadEnd = 2;
inline constexpr uint8_t kThreadDcStart = 3;  // 会话开始时对已有线程的枚举
inline constexpr uint8_t kThreadDcEnd = 4;    // 会话结束时的枚举，不用
inline constexpr uint8_t kDiskRead = 10;
inline constexpr uint8_t kDiskWrite = 11;
inline constexpr uint8_t kNetSendV4 = 10;  // TcpIp 和 UdpIp 相同
inline constexpr uint8_t kNetRecvV4 = 11;
inline constexpr uint8_t kNetSendV6 = 26;
inline constexpr uint8_t kNetRecvV6 = 27;

// 字段偏移（x64，事件带 64 位指针）
inline constexpr size_t kThreadPidOffset = 0;  // Thread_TypeGroup1.ProcessId
inline constexpr size_t kThreadTidOffset = 4;  // TThreadId
inline constexpr size_t kDiskSizeOffset = 8;            // DiskIo_TypeGroup1.TransferSize
inline constexpr size_t kDiskIssuingTidOffset = 48;     // IssuingThreadId（前面有两个指针）
inline constexpr size_t kNetPidOffset = 0;    // PID
inline constexpr size_t kNetSizeOffset = 4;   // size
inline constexpr size_t kNetDaddrOffset = 8;  // 远端地址
inline constexpr size_t kNetSaddrV4Offset = 12;  // 本地地址（收发两个方向都是本机这一端）
inline constexpr size_t kNetSaddrV6Offset = 24;

enum class EventKind : uint8_t {
    None,
    ThreadStart,  // pid、tid（含 DCStart）
    ThreadEnd,    // tid
    DiskRead,     // tid（发起 I/O 的线程）、bytes
    DiskWrite,
    NetReceive,   // pid、bytes
    NetSend,
};

struct ParsedEvent {
    EventKind kind = EventKind::None;
    uint32_t pid = 0;
    uint32_t tid = 0;
    uint32_t bytes = 0;
};

// 只认上面列出的几类事件，其余（含长度不够、32 位指针的事件、本机回环流量）返回 None。
// 回环流量不经过网卡，算进来会和网速对不上
ParsedEvent ParseEvent(const GUID& provider, uint8_t opcode, const void* data, size_t length, bool pointer64) noexcept;

}  // namespace pitot::etw
