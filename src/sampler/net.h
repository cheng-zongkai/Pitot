#pragma once

#include <winsock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>

#include <atomic>
#include <cstdint>
#include <optional>

#include "common/unique_handle.h"
#include "sampler/snapshot.h"
#include "sampler/rate.h"

namespace pitot::sampler {

// 网速。“自动”汇总所有物理网卡（D-021）；手动选了网卡（按 InterfaceGuid，D-011）就只统计它，
// 它不在或没连上时临时回到“自动”。
// 网卡列表只在启动和网络接口变化时扫描；每个周期对已知网卡逐个 GetIfEntry2，不分配堆内存。
class NetSampler {
public:
    bool Init();
    // 只在采样线程调用。adapter 为空是“自动”
    void Configure(const std::optional<GUID>& adapter, bool show_virtual);
    void Sample(Snapshot& out);

    // 这一轮统计范围内收发的字节数（与 Snapshot 里的速率同一范围），流量统计用
    [[nodiscard]] uint64_t round_rx() const noexcept { return round_rx_; }
    [[nodiscard]] uint64_t round_tx() const noexcept { return round_tx_; }
    // 流量统计：“自动”范围（全部物理网卡）这一轮的字节数，和每块网卡各自的
    [[nodiscard]] uint64_t auto_round_rx() const noexcept { return auto_round_rx_; }
    [[nodiscard]] uint64_t auto_round_tx() const noexcept { return auto_round_tx_; }
    template <typename F>
    void ForEachRound(F&& f) const {
        for (int i = 0; i < count_; ++i) {
            if (adapters_[i].round_rx || adapters_[i].round_tx) {
                f(adapters_[i].guid, adapters_[i].round_rx, adapters_[i].round_tx);
            }
        }
    }
    // 正在统计的网卡（手动选中且已连接）；“自动”或回退时为空
    [[nodiscard]] const GUID* counted_guid() const noexcept { return counted_ >= 0 ? &adapters_[counted_].guid : nullptr; }

private:
    static void CALLBACK OnInterfaceChange(PVOID context, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type);
    void Rescan();

    struct NotifyTraits {
        using pointer = HANDLE;
        static pointer invalid() noexcept { return nullptr; }
        // 会等正在执行的回调结束
        static void close(pointer h) noexcept { CancelMibChangeNotify2(h); }
    };

    struct Adapter : NetworkRate {
        NET_LUID luid{};
        GUID guid{};
        wchar_t name[40]{};
        wchar_t description[64]{};
        bool physical = false;
        bool wireless = false;
        bool up = false;
        uint64_t link_bps = 0;
    };

    static constexpr int kMaxAdapters = 16;
    Adapter adapters_[kMaxAdapters]{};
    Adapter scratch_[kMaxAdapters]{};  // 重新扫描时的新表，放成员里免得占采样线程的栈
    int count_ = 0;
    std::optional<double> rx_;
    std::optional<double> tx_;
    std::optional<GUID> selected_;
    bool show_virtual_ = true;
    int counted_ = -1;  // adapters_ 下标
    uint64_t round_rx_ = 0;
    uint64_t round_tx_ = 0;
    uint64_t auto_round_rx_ = 0;
    uint64_t auto_round_tx_ = 0;
    std::atomic<bool> rescan_{true};
    // 放在最后：析构时最先注销通知，回调不会再碰到已销毁的成员
    unique_handle_t<NotifyTraits> notification_;
};

}  // namespace pitot::sampler
