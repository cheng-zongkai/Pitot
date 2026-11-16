#pragma once

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace pitot::sampler {

// 进程图标库（D-030）：采样线程从可执行文件取图标，转成 32 × 32 预乘 BGRA 像素放进固定槽位；
// UI 线程按“槽位 + 版本号”读出来建 D2D 位图。读写之间不加锁，用顺序锁（seqlock）：
// 写方先把版本号改成奇数，写完像素再改成新的偶数；读方前后各读一次版本号，对不上就放弃这次。
// 槽位不够时淘汰最久没用的，版本号随之变化，UI 发现版本不同就重建位图。
// 像素内存（32 槽 × 4 KB）第一次用到时才分配，之后一直保留。
class IconStore {
public:
    static constexpr int kSlots = 32;
    static constexpr int kSize = 32;
    static constexpr int kPixels = kSize * kSize;

    IconStore() = default;
    ~IconStore();
    IconStore(const IconStore&) = delete;
    IconStore& operator=(const IconStore&) = delete;

    // ---- 采样线程 ----
    // 从可执行文件取图标；拿不到时用 Windows 默认程序图标。成功时返回槽位并写出版本号
    int Extract(const wchar_t* path, uint32_t& version);
    // 默认程序图标（没有路径的进程用，如 System）
    int Default(uint32_t& version);
    // 这个槽位还存着当初的图标吗（没被淘汰）
    [[nodiscard]] bool Valid(int slot, uint32_t version) const;
    // 标记本轮用到了，淘汰时靠它判断新旧
    void Touch(int slot);

    // ---- UI 线程 ----
    // 版本对得上且读的过程中没被改写时，把像素复制到 out 并返回 true
    bool Read(int slot, uint32_t version, uint32_t* out) const;

private:
    struct Slot {
        std::atomic<uint32_t> sequence{0};
        uint32_t pixels[kPixels];
    };

    int Store(HICON icon, uint32_t& version);
    int PickSlot();

    std::atomic<Slot*> slots_{nullptr};
    // 以下只有采样线程访问
    std::array<uint64_t, kSlots> last_used_{};
    uint64_t clock_ = 0;
    int default_slot_ = -1;
    uint32_t default_version_ = 0;
};

}  // namespace pitot::sampler
