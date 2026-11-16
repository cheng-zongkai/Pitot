#include "sampler/ping.h"

#include <cwchar>

#include "common/log.h"

namespace pitot::sampler {
namespace {

constexpr uint64_t kIntervalMs = 2000;
constexpr DWORD kTimeoutMs = 1000;
// 解析失败后隔这么久再试
constexpr uint64_t kResolveRetryMs = 10000;
char kPayload[32] = "Pitot latency probe";

bool Signaled(const unique_handle& event) { return event && WaitForSingleObject(event.get(), 0) == WAIT_OBJECT_0; }

PingState FromStatus(ULONG status) {
    switch (status) {
    case IP_SUCCESS:
        return PingState::Ok;
    case IP_REQ_TIMED_OUT:
        return PingState::Timeout;
    default:
        return PingState::Unreachable;
    }
}

}  // namespace

void Pinger::Tick(bool run, const wchar_t* target, uint64_t now_ms, Snapshot& out) {
    if (!run) {
        Stop();
        out.ping_state = PingState::Off;
        return;
    }
    if (!winsock_) {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            out.ping_state = PingState::Unreachable;
            return;
        }
        winsock_ = true;
    }
    if (wcscmp(target, target_) != 0) {
        // 换了目标：放弃进行中的解析，旧地址作废；进行中的回显让它自然结束
        CancelResolve();
        address_.reset();
        ms_.reset();
        resolved_at_ms_ = 0;
        wcsncpy_s(target_, target, _TRUNCATE);
        state_ = PingState::Waiting;
    }

    if (resolving_) {
        CheckResolve(now_ms);
    } else if (!address_ && (resolved_at_ms_ == 0 || now_ms - resolved_at_ms_ >= kResolveRetryMs)) {
        StartResolve(now_ms);
    }
    if (pending_) {
        CheckReply();
    }
    if (address_ && !pending_ && (sent_at_ms_ == 0 || now_ms - sent_at_ms_ >= kIntervalMs)) {
        Send(now_ms);
    }
    out.ping_state = state_;
    out.ping_ms = state_ == PingState::Ok ? ms_ : std::nullopt;
}

void Pinger::Stop() noexcept {
    // 进行中的请求会往缓冲区里写，等它结束再关句柄（最多等到超时）
    if (pending_ && reply_event_) {
        WaitForSingleObject(reply_event_.get(), kTimeoutMs + 500);
    }
    pending_ = false;
    CancelResolve();
    icmp_.reset();
    icmp_family_ = 0;
    reply_event_.reset();
    resolve_event_.reset();
    address_.reset();
    ms_.reset();
    target_[0] = L'\0';
    sent_at_ms_ = 0;
    resolved_at_ms_ = 0;
    logged_ = false;
    state_ = PingState::Off;
    if (winsock_) {
        WSACleanup();
        winsock_ = false;
    }
}

void Pinger::StartResolve(uint64_t now_ms) {
    if (!resolve_event_) {
        resolve_event_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!resolve_event_) {
            return;
        }
    }
    ResetEvent(resolve_event_.get());
    resolve_overlapped_ = OVERLAPPED{};
    resolve_overlapped_.hEvent = resolve_event_.get();
    resolve_result_ = nullptr;
    resolve_cancel_ = nullptr;
    resolved_at_ms_ = now_ms;
    ADDRINFOEXW hints{};
    hints.ai_family = AF_UNSPEC;
    const INT result = GetAddrInfoExW(target_, nullptr, NS_ALL, nullptr, &hints, &resolve_result_, nullptr,
                                      &resolve_overlapped_, nullptr, &resolve_cancel_);
    if (result == WSA_IO_PENDING) {
        resolving_ = true;
        return;
    }
    // IP 地址字面量会同步完成，这时重叠结构没有用上，不能再查它的结果
    TakeResult(result, now_ms);
}

void Pinger::CheckResolve(uint64_t now_ms) {
    if (!Signaled(resolve_event_)) {
        return;
    }
    resolving_ = false;
    TakeResult(GetAddrInfoExOverlappedResult(&resolve_overlapped_), now_ms);
}

void Pinger::TakeResult(INT result, uint64_t now_ms) {
    if (result != NO_ERROR || !resolve_result_) {
        if (resolve_result_) {
            FreeAddrInfoExW(resolve_result_);
            resolve_result_ = nullptr;
        }
        log::Write(L"ping: cannot resolve [%s] error %d", target_, result);
        state_ = PingState::Unresolved;
        resolved_at_ms_ = now_ms;
        return;
    }
    // 取系统排序的第一个地址（Windows 按 RFC 6724 排好了 IPv4 / IPv6 的先后）
    for (const ADDRINFOEXW* ai = resolve_result_; ai; ai = ai->ai_next) {
        SOCKADDR_INET a{};
        if (ai->ai_family == AF_INET && ai->ai_addrlen >= sizeof(a.Ipv4)) {
            memcpy(&a.Ipv4, ai->ai_addr, sizeof(a.Ipv4));
        } else if (ai->ai_family == AF_INET6 && ai->ai_addrlen >= sizeof(a.Ipv6)) {
            memcpy(&a.Ipv6, ai->ai_addr, sizeof(a.Ipv6));
        } else {
            continue;
        }
        address_ = a;
        break;
    }
    FreeAddrInfoExW(resolve_result_);
    resolve_result_ = nullptr;
    if (!address_) {
        state_ = PingState::Unresolved;
        return;
    }
    log::Write(L"ping: [%s] resolved, family %d", target_, address_->si_family == AF_INET ? 4 : 6);
}

void Pinger::CancelResolve() noexcept {
    if (!resolving_) {
        return;
    }
    // 取消后系统仍会通过事件报告完成，等到了才能释放 OVERLAPPED 和结果
    if (resolve_cancel_) {
        GetAddrInfoExCancel(&resolve_cancel_);
    }
    if (resolve_event_) {
        WaitForSingleObject(resolve_event_.get(), 2000);
        GetAddrInfoExOverlappedResult(&resolve_overlapped_);
    }
    if (resolve_result_) {
        FreeAddrInfoExW(resolve_result_);
        resolve_result_ = nullptr;
    }
    resolving_ = false;
}

void Pinger::Send(uint64_t now_ms) {
    const int family = address_->si_family;
    if (!icmp_ || icmp_family_ != family) {
        icmp_.reset(family == AF_INET ? IcmpCreateFile() : Icmp6CreateFile());
        icmp_family_ = family;
        if (!icmp_) {
            log::Write(L"ping: IcmpCreateFile failed %lu", GetLastError());
            state_ = PingState::Unreachable;
            return;
        }
    }
    if (!reply_event_) {
        reply_event_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!reply_event_) {
            return;
        }
    }
    ResetEvent(reply_event_.get());
    sent_at_ms_ = now_ms;
    DWORD result = 0;
    if (family == AF_INET) {
        result = IcmpSendEcho2(icmp_.get(), reply_event_.get(), nullptr, nullptr, address_->Ipv4.sin_addr.S_un.S_addr,
                               kPayload, sizeof(kPayload), nullptr, reply_, sizeof(reply_), kTimeoutMs);
    } else {
        SOCKADDR_IN6 source{};
        source.sin6_family = AF_INET6;
        result = Icmp6SendEcho2(icmp_.get(), reply_event_.get(), nullptr, nullptr, &source, &address_->Ipv6, kPayload,
                                sizeof(kPayload), nullptr, reply_, sizeof(reply_), kTimeoutMs);
    }
    if (result == 0 && GetLastError() == ERROR_IO_PENDING) {
        pending_ = true;
        return;
    }
    if (result != 0) {
        // 带事件时一般不会同步完成；完成了就直接解析
        pending_ = true;
        SetEvent(reply_event_.get());
        CheckReply();
        return;
    }
    state_ = PingState::Unreachable;
}

void Pinger::CheckReply() {
    if (!Signaled(reply_event_)) {
        return;
    }
    pending_ = false;
    if (icmp_family_ == AF_INET) {
        if (IcmpParseReplies(reply_, sizeof(reply_)) > 0) {
            const auto* r = reinterpret_cast<const ICMP_ECHO_REPLY*>(reply_);
            state_ = FromStatus(r->Status);
            ms_ = r->RoundTripTime;
        } else {
            state_ = FromStatus(GetLastError());
        }
    } else {
        if (Icmp6ParseReplies(reply_, sizeof(reply_)) > 0) {
            const auto* r = reinterpret_cast<const ICMPV6_ECHO_REPLY*>(reply_);
            state_ = FromStatus(r->Status);
            ms_ = r->RoundTripTime;
        } else {
            state_ = FromStatus(GetLastError());
        }
    }
    if (!logged_) {
        logged_ = true;
        log::Write(L"ping: [%s] first result state %d, %lu ms", target_, static_cast<int>(state_),
                   static_cast<unsigned long>(ms_.value_or(0)));
    }
}

}  // namespace pitot::sampler
