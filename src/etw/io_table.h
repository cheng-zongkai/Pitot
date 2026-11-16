#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <type_traits>

namespace pitot::etw {

// 定长开放寻址哈希表，键是非零的 32 位整数（线程号、进程号 + 1）。
// ETW 回调里每个事件都要查表，不能分配堆内存，所以容量在 Init 时一次定好；满了就拒绝插入。
// 删除用后移法（backward shift），不留墓碑，长时间运行查找也不会变慢。
// 槽位直接用 VirtualAlloc 分配（清零即为空槽）：每次开会话都要建表，从堆里拿 128 KB 反复申请释放会留下碎片
template <typename V>
class OpenMap {
    static_assert(std::is_trivially_copyable_v<V>);

public:
    // bits：容量是 2 的 bits 次方。返回 false 表示内存不够
    bool Init(int bits) noexcept {
        bits_ = bits;
        const size_t capacity = size_t{1} << bits;
        slots_.reset(static_cast<Slot*>(
            VirtualAlloc(nullptr, capacity * sizeof(Slot), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)));
        mask_ = slots_ ? capacity - 1 : 0;
        count_ = 0;
        return static_cast<bool>(slots_);
    }
    void Release() noexcept {
        slots_.reset();
        mask_ = 0;
        count_ = 0;
    }
    [[nodiscard]] bool ready() const noexcept { return static_cast<bool>(slots_); }
    [[nodiscard]] size_t size() const noexcept { return count_; }

    V* Find(uint32_t key) noexcept {
        if (!slots_ || key == 0) {
            return nullptr;
        }
        for (size_t i = Home(key);; i = (i + 1) & mask_) {
            if (slots_.get()[i].key == key) {
                return &slots_.get()[i].value;
            }
            if (slots_.get()[i].key == 0) {
                return nullptr;
            }
        }
    }

    // 没有就插入一个值初始化的项。表满（超过 7/8）时返回 nullptr
    V* FindOrInsert(uint32_t key) noexcept {
        if (!slots_ || key == 0) {
            return nullptr;
        }
        for (size_t i = Home(key);; i = (i + 1) & mask_) {
            if (slots_.get()[i].key == key) {
                return &slots_.get()[i].value;
            }
            if (slots_.get()[i].key == 0) {
                if (count_ + 1 > (mask_ + 1) / 8 * 7) {
                    return nullptr;
                }
                slots_.get()[i].key = key;
                slots_.get()[i].value = V{};
                ++count_;
                return &slots_.get()[i].value;
            }
        }
    }

    void Erase(uint32_t key) noexcept {
        if (!slots_ || key == 0) {
            return;
        }
        size_t i = Home(key);
        while (slots_.get()[i].key != key) {
            if (slots_.get()[i].key == 0) {
                return;
            }
            i = (i + 1) & mask_;
        }
        // 把后面探测链上“本该更靠前”的项挪进空位，直到遇到空槽
        for (size_t j = (i + 1) & mask_; slots_.get()[j].key != 0; j = (j + 1) & mask_) {
            const size_t home = Home(slots_.get()[j].key);
            // home 不在 (i, j] 之间（环形）时，j 上的项可以挪到 i
            const bool between = i <= j ? (home > i && home <= j) : (home > i || home <= j);
            if (!between) {
                slots_.get()[i] = slots_.get()[j];
                i = j;
            }
        }
        slots_.get()[i].key = 0;
        --count_;
    }

    // 逐项回调后清空，不释放内存
    template <typename F>
    void Drain(F&& f) noexcept {
        if (!slots_ || count_ == 0) {
            return;
        }
        for (size_t i = 0; i <= mask_; ++i) {
            if (slots_.get()[i].key != 0) {
                f(slots_.get()[i].key, slots_.get()[i].value);
                slots_.get()[i].key = 0;
            }
        }
        count_ = 0;
    }

private:
    struct Slot {
        uint32_t key;
        V value;
    };
    // 斐波那契散列：线程号、进程号都是 4 的倍数，直接取低位会扎堆
    size_t Home(uint32_t key) const noexcept {
        return static_cast<size_t>((key * 2654435761u) >> (32 - bits_)) & mask_;
    }

    struct Free {
        void operator()(Slot* p) const noexcept { VirtualFree(p, 0, MEM_RELEASE); }
    };
    std::unique_ptr<Slot, Free> slots_;
    size_t mask_ = 0;
    size_t count_ = 0;
    int bits_ = 1;
};

// 一个进程在一段时间内的读写字节数
struct IoCounters {
    uint64_t disk_read = 0;
    uint64_t disk_write = 0;
    uint64_t net_receive = 0;
    uint64_t net_send = 0;
};

struct IoSample {
    uint32_t pid = 0;
    IoCounters bytes;
};

// 按事件时间戳（QPC）分轮（D-063）：时间戳不晚于分界的记进本轮，晚于分界的先放进下一轮的表。
// 事件按交付顺序到达，不按发生顺序；只按到达时刻分轮的话，刷新前后几毫秒的事件会落到错的一轮。
// 分界之前发生、但取走之后才送达的事件记进下一轮，不丢
class IoBuckets {
public:
    bool Init(int bits) noexcept {
        cut_ = kOpen;
        return current_.Init(bits) && next_.Init(bits);
    }
    void Release() noexcept {
        current_.Release();
        next_.Release();
        cut_ = kOpen;
    }
    // 表满时返回 nullptr
    IoCounters* Slot(uint32_t key, int64_t timestamp) noexcept {
        return (timestamp > cut_ ? next_ : current_).FindOrInsert(key);
    }
    // 新的分界：上一次分界之后的事件都不晚于它，先并回本轮。返回表满丢掉的项数
    size_t Cut(int64_t timestamp) noexcept {
        size_t lost = 0;
        next_.Drain([&](uint32_t key, const IoCounters& c) {
            if (IoCounters* d = current_.FindOrInsert(key)) {
                d->disk_read += c.disk_read;
                d->disk_write += c.disk_write;
                d->net_receive += c.net_receive;
                d->net_send += c.net_send;
            } else {
                ++lost;
            }
        });
        cut_ = timestamp;
        return lost;
    }
    // 取走本轮（不晚于分界）的字节；下一轮的留着
    template <typename F>
    void Drain(F&& f) noexcept {
        current_.Drain(f);
    }

private:
    // 还没设过分界（会话刚开始）：所有事件都记进本轮
    static constexpr int64_t kOpen = INT64_MAX;
    OpenMap<IoCounters> current_;
    OpenMap<IoCounters> next_;
    int64_t cut_ = kOpen;
};

}  // namespace pitot::etw
