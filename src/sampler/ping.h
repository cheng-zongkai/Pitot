#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <cstdint>
#include <optional>

#include "common/unique_handle.h"
#include "sampler/snapshot.h"

namespace pitot::sampler {

// 网络延迟（M7）：对设置的目标发 ICMP 回显，2 秒一次。全部异步，采样线程从不等网络：
// 域名用 GetAddrInfoExW 的重叠模式解析，回显用 IcmpSendEcho2 / Icmp6SendEcho2 带事件发出，
// 每轮只检查事件有没有完成。只在网络面板打开时运行，离开就关句柄、退出 Winsock。
class Pinger {
public:
    Pinger() = default;
    ~Pinger() { Stop(); }
    Pinger(const Pinger&) = delete;
    Pinger& operator=(const Pinger&) = delete;

    // 每轮调用。run 为假时停下并释放资源；target 变了重新解析
    void Tick(bool run, const wchar_t* target, uint64_t now_ms, Snapshot& out);
    void Stop() noexcept;

private:
    void StartResolve(uint64_t now_ms);
    void CheckResolve(uint64_t now_ms);
    void TakeResult(INT result, uint64_t now_ms);
    void CancelResolve() noexcept;
    void Send(uint64_t now_ms);
    void CheckReply();

    struct IcmpTraits {
        using pointer = HANDLE;
        static pointer invalid() noexcept { return INVALID_HANDLE_VALUE; }
        static void close(pointer h) noexcept { IcmpCloseHandle(h); }
    };

    bool winsock_ = false;
    wchar_t target_[64]{};

    // 解析
    bool resolving_ = false;
    OVERLAPPED resolve_overlapped_{};
    unique_handle resolve_event_;
    HANDLE resolve_cancel_ = nullptr;
    PADDRINFOEXW resolve_result_ = nullptr;
    uint64_t resolved_at_ms_ = 0;
    std::optional<SOCKADDR_INET> address_;

    // 回显
    unique_handle_t<IcmpTraits> icmp_;
    int icmp_family_ = 0;
    unique_handle reply_event_;
    bool pending_ = false;
    uint64_t sent_at_ms_ = 0;
    // 回复缓冲区要在请求完成前一直有效。够放 ICMPV6_ECHO_REPLY + 数据 + 8 字节 ICMP 错误 + IO_STATUS_BLOCK
    alignas(8) unsigned char reply_[256]{};

    bool logged_ = false;  // 每次打开面板记一次第一个结果
    PingState state_ = PingState::Off;
    std::optional<uint32_t> ms_;
};

}  // namespace pitot::sampler
