#pragma once

#include <cstdint>
#include <optional>

namespace pitot::sampler {

// 累计计数器两次读数之差。计数器重置（网卡重启、驱动重载）时 cur < prev，按 0 处理，避免出现巨大的假峰值。
inline uint64_t CounterDelta(uint64_t prev, uint64_t cur) noexcept { return cur >= prev ? cur - prev : 0; }

inline double PerSecond(uint64_t delta, uint64_t elapsed_ms) noexcept {
    return elapsed_ms == 0 ? 0.0 : static_cast<double>(delta) * 1000.0 / static_cast<double>(elapsed_ms);
}

// 每个接口单独记时；缺测后重新建立基准，避免把几秒的流量算成一秒。
struct NetworkRate {
    uint64_t rx_bytes = 0;
    uint64_t tx_bytes = 0;
    uint64_t tick = 0;
    bool primed = false;
    uint64_t round_rx = 0;
    uint64_t round_tx = 0;
    std::optional<double> rx;
    std::optional<double> tx;

    void Reset() noexcept { *this = {}; }
    void Update(uint64_t received, uint64_t sent, uint64_t now) noexcept {
        round_rx = round_tx = 0;
        rx.reset();
        tx.reset();
        if (primed && now == tick) {
            return;  // 同一时刻的补采不丢字节，留给下一次有效区间。
        }
        if (primed && now > tick && received >= rx_bytes && sent >= tx_bytes) {
            round_rx = received - rx_bytes;
            round_tx = sent - tx_bytes;
            rx = PerSecond(round_rx, now - tick);
            tx = PerSecond(round_tx, now - tick);
        }
        rx_bytes = received;
        tx_bytes = sent;
        tick = now;
        primed = true;
    }
};

}  // namespace pitot::sampler
