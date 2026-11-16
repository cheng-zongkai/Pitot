#pragma once

#include <windows.h>

#include <utility>

namespace pitot {

// 独占所有权的内核句柄包装。Traits 决定“无效值”和释放方式，
// 因为 Win32 句柄的失败值不统一：CreateMutex 等返回 nullptr，CreateFile 返回 INVALID_HANDLE_VALUE。
template <typename Traits>
class unique_handle_t {
public:
    using pointer = typename Traits::pointer;

    unique_handle_t() noexcept = default;
    explicit unique_handle_t(pointer h) noexcept : h_(h) {}
    ~unique_handle_t() { reset(); }

    unique_handle_t(const unique_handle_t&) = delete;
    unique_handle_t& operator=(const unique_handle_t&) = delete;

    unique_handle_t(unique_handle_t&& other) noexcept : h_(other.release()) {}
    unique_handle_t& operator=(unique_handle_t&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] pointer get() const noexcept { return h_; }
    [[nodiscard]] explicit operator bool() const noexcept { return h_ != Traits::invalid(); }

    [[nodiscard]] pointer release() noexcept { return std::exchange(h_, Traits::invalid()); }

    void reset(pointer h = Traits::invalid()) noexcept {
        if (h_ != Traits::invalid()) {
            Traits::close(h_);
        }
        h_ = h;
    }

private:
    pointer h_ = Traits::invalid();
};

struct handle_traits {
    using pointer = HANDLE;
    static pointer invalid() noexcept { return nullptr; }
    static void close(pointer h) noexcept { ::CloseHandle(h); }
};

struct file_handle_traits {
    using pointer = HANDLE;
    static pointer invalid() noexcept { return INVALID_HANDLE_VALUE; }
    static void close(pointer h) noexcept { ::CloseHandle(h); }
};

// 互斥量、事件、线程、进程等
using unique_handle = unique_handle_t<handle_traits>;
// CreateFileW 打开的文件和设备（包括 PawnIO）
using unique_file_handle = unique_handle_t<file_handle_traits>;

}  // namespace pitot
