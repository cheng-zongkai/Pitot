#pragma once

#include <windows.h>

#include <evntrace.h>

#include <atomic>
#include <thread>
#include <vector>

#include "common/unique_handle.h"
#include "etw/events.h"
#include "etw/io_table.h"

struct _EVENT_RECORD;

namespace pitot::etw {

enum class StartResult : uint8_t {
    Ok,
    NoAccess,  // 不是管理员
    NoSlot,    // 系统记录器会话已满（系统同时最多 8 个）
    Failed,    // 其他错误，见日志
};

// 停掉上次崩溃留下的同名会话（names::kEtwSession）。启动时调用一次
void StopStaleSession() noexcept;

// 按进程统计磁盘和网络读写的内核会话（D-007、D-037）：
// EVENT_TRACE_SYSTEM_LOGGER_MODE 的私有系统记录器，启用磁盘 I/O、TCP/IP（含 UDP）和线程事件，
// 实时模式由一个消费线程（ETW 消费线程）处理。只在网络或存储面板打开时运行。
// 磁盘事件头里的进程号无效（I/O 完成时记录），按事件里的发起线程号经线程表换算成进程号；
// 线程表由会话开始时的线程枚举（DCStart）和之后的线程创建 / 退出事件维护。
class KernelTrace {
public:
    KernelTrace() { InitializeSRWLock(&lock_); }
    ~KernelTrace() { Stop(); }
    KernelTrace(const KernelTrace&) = delete;
    KernelTrace& operator=(const KernelTrace&) = delete;

    StartResult Start();
    void Stop() noexcept;
    [[nodiscard]] bool active() const noexcept { return thread_.joinable(); }
    // 消费线程意外退出（会话被别人停掉）时为假
    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] bool HasActivity(bool network) const noexcept {
        return (activity_.load(std::memory_order_relaxed) & (network ? 2u : 1u)) != 0;
    }

    // 取出上次调用以来各进程的读写字节数，按 pid 排序；out 先清空。
    // 首次调用时给 out 预留 kMaxProcesses 项（进程表的容量），之后不再分配
    static constexpr size_t kMaxProcesses = 1024;
    void Drain(std::vector<IoSample>& out);

    // 首次采样时提前交付缓冲区；事件仍由消费线程异步处理（D-050）
    bool Flush() noexcept;
    // 消费线程每处理完一块缓冲区加一。和 WaitForBuffers 配对（D-060）
    [[nodiscard]] uint32_t buffers_delivered() const noexcept { return buffers_.load(std::memory_order_acquire); }
    // 等到 before 之后的缓冲区都记进表里。空刷新没有回调，大约等 20–30 ms 就放弃
    void WaitForBuffers(uint32_t before) noexcept;
    // 刷新后再等。采样线程在取走字节之前调用（D-060）
    void FlushThrough() noexcept;
    // 本轮分界（QPC，和事件时间戳同一时钟，D-063）。时间戳晚于它的事件留给下一轮。
    // 采样线程在读网卡、磁盘计数器的同一时刻调用，再 Flush
    void Cut(int64_t qpc) noexcept;

private:
    static void WINAPI OnEvent(_EVENT_RECORD* record);
    static ULONG WINAPI OnBuffer(EVENT_TRACE_LOGFILEW* logfile);
    void Handle(const _EVENT_RECORD& record) noexcept;
    uint32_t PidOfThread(uint32_t tid) noexcept;
    void Consume() noexcept;

    // 会话句柄释放时停止会话；消费句柄释放时 CloseTrace，ProcessTrace 随即返回
    struct SessionTraits {
        using pointer = TRACEHANDLE;
        static pointer invalid() noexcept { return 0; }
        static void close(pointer h) noexcept;
    };
    struct ConsumerTraits {
        using pointer = TRACEHANDLE;
        static pointer invalid() noexcept { return INVALID_PROCESSTRACE_HANDLE; }
        static void close(pointer h) noexcept { CloseTrace(h); }
    };

    unique_handle_t<SessionTraits> session_;
    unique_handle_t<ConsumerTraits> consumer_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<unsigned> activity_{0};  // 本次会话已交付的磁盘 / 网络事件，仅用于首屏等待

    // 线程号 → 进程号，只在消费线程里访问
    OpenMap<uint32_t> threads_;
    // 进程号 + 1 → 本周期的字节数；消费线程写，采样线程 Cut / Drain，lock_ 保护
    SRWLOCK lock_;
    IoBuckets io_;
    size_t merge_lost_ = 0;  // Cut 并表时表满丢掉的项，lock_ 保护
    uint64_t dropped_ = 0;  // 线程查不到进程或表满而丢掉的事件
    std::atomic<uint32_t> buffers_{0};  // 消费线程每处理完一块缓冲区加一
};

}  // namespace pitot::etw
