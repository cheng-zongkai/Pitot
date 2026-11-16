#include "sampler/net.h"

#include <cwchar>

#include "common/log.h"
#include "sampler/clock.h"
#include "sampler/rate.h"

namespace pitot::sampler {
namespace {

bool SameLuid(const NET_LUID& a, const NET_LUID& b) { return a.Value == b.Value; }

bool HasLuid(const MIB_IPINTERFACE_TABLE* table, const NET_LUID& luid) {
    if (!table) {
        return false;
    }
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        if (SameLuid(table->Table[i].InterfaceLuid, luid)) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool NetSampler::Init() {
    HANDLE handle = nullptr;
    const DWORD status = NotifyIpInterfaceChange(AF_UNSPEC, &OnInterfaceChange, this, FALSE, &handle);
    if (status != NO_ERROR) {
        // 没有通知也能工作：GetIfEntry2 失败时会触发重新扫描
        log::Write(L"net: NotifyIpInterfaceChange failed %lu", status);
        return false;
    }
    notification_.reset(handle);
    return true;
}

void NetSampler::Configure(const std::optional<GUID>& adapter, bool show_virtual) {
    const bool changed = adapter.has_value() != selected_.has_value() ||
                         (adapter && !IsEqualGUID(*adapter, *selected_));
    selected_ = adapter;
    show_virtual_ = show_virtual;
    // 选中的虚拟网卡断开时也要留在表里，换了选择就重新扫一遍
    if (changed) {
        rescan_.store(true, std::memory_order_relaxed);
    }
}

void CALLBACK NetSampler::OnInterfaceChange(PVOID context, PMIB_IPINTERFACE_ROW, MIB_NOTIFICATION_TYPE) {
    // 在系统线程池里调用，只置标志，扫描留给采样线程
    static_cast<NetSampler*>(context)->rescan_.store(true, std::memory_order_relaxed);
}

void NetSampler::Rescan() {
    PMIB_IF_TABLE2 table = nullptr;
    const DWORD status = GetIfTable2(&table);
    if (status != NO_ERROR) {
        log::Write(L"net: GetIfTable2 failed %lu", status);
        rescan_.store(true, std::memory_order_relaxed);
        return;
    }
    // 有 IP 接口的才算“能用的”虚拟网卡；WAN Miniport 之类的隐藏接口没有
    PMIB_IPINTERFACE_TABLE ip = nullptr;
    if (GetIpInterfaceTable(AF_UNSPEC, &ip) != NO_ERROR) {
        ip = nullptr;
    }

    int count = 0;
    // 两遍：物理网卡排前面，虚拟网卡在后
    for (int pass = 0; pass < 2; ++pass) {
        for (ULONG i = 0; i < table->NumEntries && count < kMaxAdapters; ++i) {
            const MIB_IF_ROW2& row = table->Table[i];
            // 过滤驱动（WFP、QoS 等）会把同一块网卡再列一遍，计入会重复计数
            if (row.InterfaceAndOperStatusFlags.FilterInterface || row.Type == IF_TYPE_SOFTWARE_LOOPBACK) {
                continue;
            }
            const bool physical = row.InterfaceAndOperStatusFlags.HardwareInterface != FALSE;
            const bool selected = selected_ && IsEqualGUID(*selected_, row.InterfaceGuid);
            if (physical != (pass == 0)) {
                continue;
            }
            // 虚拟网卡：已连接、有 IP 接口、不是 Teredo / 6to4 之类的系统隧道。选中的那块无论如何都留着
            if (!physical && !selected &&
                (row.OperStatus != IfOperStatusUp || row.Type == IF_TYPE_TUNNEL || !HasLuid(ip, row.InterfaceLuid))) {
                continue;
            }
            Adapter& a = scratch_[count++];
            a = Adapter{};
            a.luid = row.InterfaceLuid;
            a.guid = row.InterfaceGuid;
            wcsncpy_s(a.name, row.Alias, _TRUNCATE);
            wcsncpy_s(a.description, row.Description, _TRUNCATE);
            a.physical = physical;
            a.wireless = row.Type == IF_TYPE_IEEE80211;
            a.up = row.OperStatus == IfOperStatusUp;
            // 已经在表里的网卡沿用基准和速率，不用等一轮
            for (int j = 0; j < count_; ++j) {
                if (SameLuid(adapters_[j].luid, a.luid)) {
                    static_cast<NetworkRate&>(a) = adapters_[j];
                    break;
                }
            }
            log::Write(L"net: adapter [%s] %s physical=%d up=%d", a.name, a.description, a.physical, a.up);
        }
    }
    if (ip) {
        FreeMibTable(ip);
    }
    FreeMibTable(table);
    for (int i = 0; i < count; ++i) {
        adapters_[i] = scratch_[i];
    }
    count_ = count;
    log::Write(L"net: %d adapter(s) tracked", count_);
}

void NetSampler::Sample(Snapshot& out) {
    if (rescan_.exchange(false, std::memory_order_relaxed)) {
        Rescan();
    }

    for (int i = 0; i < count_; ++i) {
        Adapter& a = adapters_[i];
        a.round_rx = 0;
        a.round_tx = 0;
        MIB_IF_ROW2 row{};
        row.InterfaceLuid = a.luid;
        if (GetIfEntry2(&row) != NO_ERROR) {
            // 网卡没了：等重新扫描把它去掉
            a.up = false;
            a.Reset();
            rescan_.store(true, std::memory_order_relaxed);
            continue;
        }
        a.up = row.OperStatus == IfOperStatusUp;
        // 连接速率未知时驱动会报一个极大值
        a.link_bps = row.ReceiveLinkSpeed < (1ull << 48) ? row.ReceiveLinkSpeed : 0;
        if (a.up) {
            // 和进程列表同一个时钟（D-063）
            a.Update(row.InOctets, row.OutOctets, QpcMs());
        } else {
            a.Reset();
        }
    }

    // “自动”：所有物理网卡之和（D-021）
    std::optional<double> auto_rx;
    std::optional<double> auto_tx;
    uint64_t auto_round_rx = 0;
    uint64_t auto_round_tx = 0;
    int physical = 0;
    for (int i = 0; i < count_; ++i) {
        const Adapter& a = adapters_[i];
        if (!a.physical) {
            continue;
        }
        ++physical;
        auto_round_rx += a.round_rx;
        auto_round_tx += a.round_tx;
        if (a.rx) {
            auto_rx = auto_rx.value_or(0.0) + *a.rx;
            auto_tx = auto_tx.value_or(0.0) + a.tx.value_or(0.0);
        }
    }
    rx_ = auto_rx;
    tx_ = auto_tx;

    int selected = -1;
    if (selected_) {
        for (int i = 0; i < count_; ++i) {
            if (IsEqualGUID(adapters_[i].guid, *selected_)) {
                selected = i;
                break;
            }
        }
    }
    counted_ = selected >= 0 && adapters_[selected].up ? selected : -1;
    if (counted_ >= 0) {
        out.net_rx_bytes_per_sec = adapters_[counted_].rx;
        out.net_tx_bytes_per_sec = adapters_[counted_].tx;
        round_rx_ = adapters_[counted_].round_rx;
        round_tx_ = adapters_[counted_].round_tx;
        out.net_fallback = NetFallback::None;
    } else {
        out.net_rx_bytes_per_sec = rx_;
        out.net_tx_bytes_per_sec = tx_;
        round_rx_ = auto_round_rx;
        round_tx_ = auto_round_tx;
        out.net_fallback = !selected_ ? NetFallback::None
                           : selected >= 0 ? NetFallback::Disconnected
                                           : NetFallback::Missing;
    }
    out.net_adapter_count = physical;
    auto_round_rx_ = auto_round_rx;
    auto_round_tx_ = auto_round_tx;

    // 给浮窗网卡菜单的列表：物理网卡都列，虚拟网卡看设置，选中的那块总在
    out.net_list_count = 0;
    out.net_selected = -1;
    for (int i = 0; i < count_ && out.net_list_count < kMaxNetAdapters; ++i) {
        const Adapter& a = adapters_[i];
        if (!a.physical && !show_virtual_ && i != selected) {
            continue;
        }
        if (i == selected) {
            out.net_selected = out.net_list_count;
        }
        NetAdapter& n = out.net_list[static_cast<size_t>(out.net_list_count++)];
        n.guid = a.guid;
        wcscpy_s(n.name, a.name);
        wcscpy_s(n.description, a.description);
        n.physical = a.physical;
        n.wireless = a.wireless;
        n.up = a.up;
        n.link_bps = a.link_bps;
        n.rx_bytes_per_sec = a.rx;
        n.tx_bytes_per_sec = a.tx;
    }
}

}  // namespace pitot::sampler
