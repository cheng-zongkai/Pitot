#pragma once

#include <windows.h>

#include "common/unique_handle.h"

namespace pitot {

// CreateCompatibleDC 创建的内存 DC
struct memory_dc_traits {
    using pointer = HDC;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { ::DeleteDC(h); }
};

struct bitmap_traits {
    using pointer = HBITMAP;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { ::DeleteObject(h); }
};

struct font_traits {
    using pointer = HFONT;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { ::DeleteObject(h); }
};

struct brush_traits {
    using pointer = HBRUSH;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { ::DeleteObject(h); }
};

using unique_memory_dc = unique_handle_t<memory_dc_traits>;
// 删除前必须先从 DC 中选出，或者先删除 DC
using unique_bitmap = unique_handle_t<bitmap_traits>;
using unique_font = unique_handle_t<font_traits>;
using unique_brush = unique_handle_t<brush_traits>;

}  // namespace pitot
