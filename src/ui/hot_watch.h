#pragma once

#include <cmath>
#include <cstdint>
#include <optional>

// 高温通知的判断（M8，D-079）。纯逻辑，有单元测试。
// 最高温度在高温阈值以上连续 10 秒才通知一次；降到阈值以下 3 ℃ 之后才重新计。
// 两次通知至少隔 10 分钟，温度在阈值附近上下跳时不会反复弹。

namespace pitot::ui {

class HotWatch {
public:
    static constexpr uint64_t kHoldMs = 10'000;
    static constexpr uint64_t kCooldownMs = 10 * 60'000;
    static constexpr double kRearmMargin = 3.0;

    // 每轮采样调用一次。返回 true 表示现在该发通知
    bool Update(uint64_t now_ms, std::optional<double> celsius, int hot) {
        if (!celsius || !std::isfinite(*celsius)) {
            // 读不到时不算降温，也不算过热：保持原状，等读数回来
            return false;
        }
        if (*celsius < hot - kRearmMargin) {
            above_ = false;
            armed_ = true;
            return false;
        }
        if (*celsius < hot) {
            above_ = false;
            return false;
        }
        if (!above_) {
            above_ = true;
            above_since_ = now_ms;
        }
        if (!armed_ || now_ms - above_since_ < kHoldMs) {
            return false;
        }
        if (notified_ && now_ms - last_notified_ < kCooldownMs) {
            return false;
        }
        armed_ = false;
        notified_ = true;
        last_notified_ = now_ms;
        return true;
    }

    void Reset() { *this = HotWatch{}; }

private:
    bool above_ = false;
    bool armed_ = true;
    bool notified_ = false;
    uint64_t above_since_ = 0;
    uint64_t last_notified_ = 0;
};

}  // namespace pitot::ui
