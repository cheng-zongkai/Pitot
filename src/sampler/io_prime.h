#pragma once

#include <cstdint>

namespace pitot::sampler {

// ETW 自动刷新至少一秒：首次先积累事件，再主动刷新并留时间给消费线程（D-050）
class IoPrime {
public:
    enum class Action { Wait, Flush, Drain };

    void Start(uint64_t now) noexcept {
        phase_ = Phase::Collecting;
        deadline_ = now + 250;
        expires_ = now + 2000;
        next_flush_ = 0;
        flushes_left_ = 0;
    }
    void Flushed(uint64_t now) noexcept {
        phase_ = Phase::Delivering;
        deadline_ = now + 100;
        next_flush_ = now + 200;
        flushes_left_ = 3;
    }
    void Reset() noexcept {
        phase_ = Phase::Off;
        deadline_ = 0;
        expires_ = 0;
        next_flush_ = 0;
        flushes_left_ = 0;
    }
    // 仅检查交付状态，不重复跑完整采样；空闲时也必须结束启动等待。
    bool Retry(uint64_t now, bool has_activity) noexcept {
        if (phase_ != Phase::Delivering || now < deadline_ || has_activity || now >= expires_) {
            return false;
        }
        deadline_ = now + 100 < expires_ ? now + 100 : expires_;
        return true;
    }
    // 第一次刷新之后产生的事件仍可能留在 ETW 缓冲区，短时间补刷几次。
    bool FlushDue(uint64_t now) noexcept {
        if (phase_ != Phase::Delivering || flushes_left_ == 0 || now < next_flush_ || now >= expires_) {
            return false;
        }
        --flushes_left_;
        next_flush_ = now + 200;
        return true;
    }
    void StopFlushing() noexcept { flushes_left_ = 0; }
    [[nodiscard]] bool pending() const noexcept { return phase_ != Phase::Off; }
    [[nodiscard]] uint64_t deadline() const noexcept { return deadline_; }
    [[nodiscard]] Action Next(uint64_t now) const noexcept {
        if (phase_ == Phase::Off || now < deadline_) {
            return Action::Wait;
        }
        return phase_ == Phase::Collecting ? Action::Flush : Action::Drain;
    }

private:
    enum class Phase { Off, Collecting, Delivering };
    Phase phase_ = Phase::Off;
    uint64_t deadline_ = 0;
    uint64_t expires_ = 0;
    uint64_t next_flush_ = 0;
    unsigned flushes_left_ = 0;
};

}  // namespace pitot::sampler
