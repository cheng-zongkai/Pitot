#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

#include "common/unique_handle.h"

namespace pitot::sensors {

// 一个 PawnIO 模块：每个模块单独打开一次设备（D-004）。
// 协议照 PawnIOLib（namazso/PawnIO 的 PawnIOLib.cpp、pawnio_um.h），直接 DeviceIoControl，不依赖 PawnIOLib.dll。
class PawnIoModule {
public:
    // 打开设备并加载嵌入在 exe 里的模块。失败返回 Win32 错误码：
    // ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND 表示没装 PawnIO；ERROR_NOT_SUPPORTED 多半是模块不支持这台机器。
    DWORD Open(int resource_id);
    void Close() { device_.reset(); }
    [[nodiscard]] bool is_open() const noexcept { return static_cast<bool>(device_); }

    // 调用模块函数。in / out 都是 64 位整数数组，最多各 4 个。失败时 GetLastError 有原因。
    bool Execute(const char* function, const uint64_t* in, size_t in_count, uint64_t* out, size_t out_count);

    // 常用的单入单出形式
    bool Call(const char* function, uint64_t in, uint64_t& out) { return Execute(function, &in, 1, &out, 1); }
    bool Call(const char* function, uint64_t in) { return Execute(function, &in, 1, nullptr, 0); }
    bool Call2(const char* function, uint64_t a, uint64_t b) {
        const uint64_t in[2] = {a, b};
        return Execute(function, in, 2, nullptr, 0);
    }

private:
    unique_file_handle device_;
};

// 与 FanControl / LibreHardwareMonitor 共用的全局互斥量（D-003）。
// 等待上限由调用方给（规则是 10 ms），拿不到就跳过本轮。
class SharedMutex {
public:
    bool Open(const wchar_t* name);
    [[nodiscard]] bool TryLock(DWORD timeout_ms);
    void Unlock();

private:
    unique_handle mutex_;
};

// 作用域锁：构造时尝试加锁，析构时释放
class SharedMutexLock {
public:
    SharedMutexLock(SharedMutex& m, DWORD timeout_ms) : mutex_(m), locked_(m.TryLock(timeout_ms)) {}
    ~SharedMutexLock() {
        if (locked_) {
            mutex_.Unlock();
        }
    }
    SharedMutexLock(const SharedMutexLock&) = delete;
    SharedMutexLock& operator=(const SharedMutexLock&) = delete;
    explicit operator bool() const noexcept { return locked_; }

private:
    SharedMutex& mutex_;
    bool locked_;
};

}  // namespace pitot::sensors
