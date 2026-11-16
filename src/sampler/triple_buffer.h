#pragma once

#include <atomic>

namespace pitot::sampler {

// 单写单读的无锁三缓冲（D-020）。
// 写线程总有一份私有缓冲可写，读线程总有一份私有缓冲可读，第三份在中间交接，
// 所以读线程拿到的永远是完整的一份，且写线程不会改到读线程正在读的那份。
template <typename T>
class TripleBuffer {
public:
    // 写线程：当前可写的缓冲。内容是若干轮之前的旧数据，要整份覆盖。
    T& WriteBuffer() noexcept { return buffers_[write_]; }

    // 写线程：把写好的缓冲交给读线程
    void Publish() noexcept { write_ = middle_.exchange(write_ | kFresh, std::memory_order_acq_rel) & kIndexMask; }

    // 读线程：有新数据时换到最新一份并返回 true；没有新数据时返回 false，ReadBuffer() 不变
    bool Acquire() noexcept {
        if ((middle_.load(std::memory_order_relaxed) & kFresh) == 0) {
            return false;
        }
        read_ = middle_.exchange(read_, std::memory_order_acq_rel) & kIndexMask;
        return true;
    }

    // 读线程：最近一次 Acquire 拿到的数据
    const T& ReadBuffer() const noexcept { return buffers_[read_]; }

private:
    static constexpr unsigned kIndexMask = 3;
    static constexpr unsigned kFresh = 4;

    T buffers_[3]{};
    unsigned write_ = 0;
    unsigned read_ = 1;
    std::atomic<unsigned> middle_{2};
};

}  // namespace pitot::sampler
