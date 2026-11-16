#include "etw/events.h"

#include <cstring>

namespace pitot::etw {
namespace {

uint32_t ReadU32(const void* data, size_t offset) noexcept {
    uint32_t v;
    std::memcpy(&v, static_cast<const unsigned char*>(data) + offset, sizeof(v));
    return v;
}

bool SameGuid(const GUID& a, const GUID& b) noexcept { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

// 两端都是回环地址才算本机回环。IPv4 地址按网络字节序存，127.x.x.x 的第一个字节是 127
bool LoopbackV4(const void* data) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    return p[kNetDaddrOffset] == 127 && p[kNetSaddrV4Offset] == 127;
}

bool IsV6Loopback(const unsigned char* a) noexcept {
    for (int i = 0; i < 15; ++i) {
        if (a[i] != 0) {
            return false;
        }
    }
    return a[15] == 1;
}

bool LoopbackV6(const void* data) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    return IsV6Loopback(p + kNetDaddrOffset) && IsV6Loopback(p + kNetSaddrV6Offset);
}

}  // namespace

ParsedEvent ParseEvent(const GUID& provider, uint8_t opcode, const void* data, size_t length, bool pointer64) noexcept {
    ParsedEvent e;
    if (!data) {
        return e;
    }
    if (SameGuid(provider, kThreadGuid)) {
        if (length < kThreadTidOffset + 4) {
            return e;
        }
        if (opcode == kThreadStart || opcode == kThreadDcStart) {
            e.kind = EventKind::ThreadStart;
            e.pid = ReadU32(data, kThreadPidOffset);
        } else if (opcode == kThreadEnd) {
            e.kind = EventKind::ThreadEnd;
        } else {
            return e;
        }
        e.tid = ReadU32(data, kThreadTidOffset);
        return e;
    }
    if (SameGuid(provider, kDiskIoGuid)) {
        // 偏移按 64 位指针算；本程序只有 x64 版，内核事件总是 64 位
        if (!pointer64 || length < kDiskIssuingTidOffset + 4 || (opcode != kDiskRead && opcode != kDiskWrite)) {
            return e;
        }
        e.kind = opcode == kDiskRead ? EventKind::DiskRead : EventKind::DiskWrite;
        e.tid = ReadU32(data, kDiskIssuingTidOffset);
        e.bytes = ReadU32(data, kDiskSizeOffset);
        return e;
    }
    if (SameGuid(provider, kTcpIpGuid) || SameGuid(provider, kUdpIpGuid)) {
        const bool v4 = opcode == kNetSendV4 || opcode == kNetRecvV4;
        const bool v6 = opcode == kNetSendV6 || opcode == kNetRecvV6;
        if (v4 ? length < kNetSaddrV4Offset + 4 : !v6 || length < kNetSaddrV6Offset + 16) {
            return e;
        }
        if (v4 ? LoopbackV4(data) : LoopbackV6(data)) {
            return e;
        }
        e.kind = opcode == kNetRecvV4 || opcode == kNetRecvV6 ? EventKind::NetReceive : EventKind::NetSend;
        e.pid = ReadU32(data, kNetPidOffset);
        e.bytes = ReadU32(data, kNetSizeOffset);
        return e;
    }
    return e;
}

}  // namespace pitot::etw
