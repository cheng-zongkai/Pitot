#include "sampler/icon_store.h"

#include <shellapi.h>
#include <shlobj_core.h>

#include <cstring>

#include "common/unique_gdi.h"
#include "common/unique_handle.h"

namespace pitot::sampler {
namespace {

struct IconTraits {
    using pointer = HICON;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { DestroyIcon(h); }
};
using unique_icon = unique_handle_t<IconTraits>;

// HICON → 32 × 32 预乘 BGRA。旧式图标没有 alpha 通道时用掩码补上透明度
bool IconToPixels(HICON icon, uint32_t* out) {
    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) {
        return false;
    }
    unique_bitmap color(info.hbmColor);
    unique_bitmap mask(info.hbmMask);
    if (!color) {
        return false;  // 单色图标，罕见，不处理
    }
    BITMAP bm{};
    if (!GetObjectW(color.get(), sizeof(bm), &bm) || bm.bmWidth != IconStore::kSize ||
        bm.bmHeight != IconStore::kSize) {
        return false;
    }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = IconStore::kSize;
    bi.bmiHeader.biHeight = -IconStore::kSize;  // 自上而下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(nullptr);
    bool ok = GetDIBits(dc, color.get(), 0, IconStore::kSize, out, &bi, DIB_RGB_COLORS) == IconStore::kSize;
    bool has_alpha = false;
    for (int i = 0; ok && i < IconStore::kPixels && !has_alpha; ++i) {
        has_alpha = (out[i] >> 24) != 0;
    }
    if (ok && !has_alpha) {
        uint32_t mask_pixels[IconStore::kPixels];
        ok = mask && GetDIBits(dc, mask.get(), 0, IconStore::kSize, mask_pixels, &bi, DIB_RGB_COLORS) ==
                         IconStore::kSize;
        for (int i = 0; ok && i < IconStore::kPixels; ++i) {
            // 掩码为白表示透明
            out[i] = (out[i] & 0x00FFFFFF) | ((mask_pixels[i] & 0x00FFFFFF) ? 0u : 0xFF000000u);
        }
    }
    ReleaseDC(nullptr, dc);
    if (!ok) {
        return false;
    }
    for (int i = 0; i < IconStore::kPixels; ++i) {
        const uint32_t p = out[i];
        const uint32_t a = p >> 24;
        const uint32_t r = ((p >> 16) & 0xFF) * a / 255;
        const uint32_t g = ((p >> 8) & 0xFF) * a / 255;
        const uint32_t b = (p & 0xFF) * a / 255;
        out[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
    return true;
}

unique_icon ExtractFrom(const wchar_t* path, int index) {
    HICON icon = nullptr;
    if (FAILED(SHDefExtractIconW(path, index, 0, &icon, nullptr, IconStore::kSize))) {
        return unique_icon();
    }
    return unique_icon(icon);
}

}  // namespace

IconStore::~IconStore() { delete[] slots_.load(); }

int IconStore::Extract(const wchar_t* path, uint32_t& version) {
    if (path && *path) {
        unique_icon icon = ExtractFrom(path, 0);
        if (icon) {
            const int slot = Store(icon.get(), version);
            if (slot >= 0) {
                return slot;
            }
        }
    }
    return Default(version);
}

int IconStore::Default(uint32_t& version) {
    if (Valid(default_slot_, default_version_)) {
        version = default_version_;
        return default_slot_;
    }
    SHSTOCKICONINFO sii{sizeof(sii)};
    if (FAILED(SHGetStockIconInfo(SIID_APPLICATION, SHGSI_ICONLOCATION, &sii))) {
        return -1;
    }
    unique_icon icon = ExtractFrom(sii.szPath, sii.iIcon);
    if (!icon) {
        return -1;
    }
    default_slot_ = Store(icon.get(), default_version_);
    version = default_version_;
    return default_slot_;
}

bool IconStore::Valid(int slot, uint32_t version) const {
    Slot* slots = slots_.load(std::memory_order_acquire);
    return slots && slot >= 0 && slot < kSlots && version != 0 &&
           slots[slot].sequence.load(std::memory_order_acquire) == version;
}

void IconStore::Touch(int slot) {
    if (slot >= 0 && slot < kSlots) {
        last_used_[static_cast<size_t>(slot)] = ++clock_;
    }
}

int IconStore::PickSlot() {
    int best = 0;
    for (int i = 0; i < kSlots; ++i) {
        // 默认图标常用，不淘汰
        if (i == default_slot_) {
            continue;
        }
        if (last_used_[static_cast<size_t>(i)] == 0) {
            return i;
        }
        if (best == default_slot_ || last_used_[static_cast<size_t>(i)] < last_used_[static_cast<size_t>(best)]) {
            best = i;
        }
    }
    return best;
}

int IconStore::Store(HICON icon, uint32_t& version) {
    uint32_t pixels[kPixels];
    if (!IconToPixels(icon, pixels)) {
        return -1;
    }
    Slot* slots = slots_.load(std::memory_order_relaxed);
    if (!slots) {
        slots = new Slot[kSlots];
        slots_.store(slots, std::memory_order_release);
    }
    const int index = PickSlot();
    Slot& s = slots[index];
    const uint32_t seq = s.sequence.load(std::memory_order_relaxed);
    // 奇数表示正在写
    s.sequence.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    memcpy(s.pixels, pixels, sizeof(pixels));
    s.sequence.store(seq + 2, std::memory_order_release);
    version = seq + 2;
    Touch(index);
    return index;
}

bool IconStore::Read(int slot, uint32_t version, uint32_t* out) const {
    Slot* slots = slots_.load(std::memory_order_acquire);
    if (!slots || slot < 0 || slot >= kSlots || version == 0) {
        return false;
    }
    const Slot& s = slots[slot];
    if (s.sequence.load(std::memory_order_acquire) != version) {
        return false;
    }
    memcpy(out, s.pixels, sizeof(s.pixels));
    std::atomic_thread_fence(std::memory_order_acquire);
    return s.sequence.load(std::memory_order_relaxed) == version;
}

}  // namespace pitot::sampler
