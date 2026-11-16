#include "etw/kernel_trace.h"

#include <evntcons.h>

#include <algorithm>
#include <cstddef>
#include <system_error>

#include "common/log.h"
#include "common/names.h"
#include "etw/events.h"

namespace pitot::etw {
namespace {

// 线程表 2^14 = 16384 项（本机约 2300 个线程），进程表 2^10 = 1024 项（一秒内有读写的进程）
constexpr int kThreadTableBits = 14;
constexpr int kIoTableBits = 10;
// 查不到进程的线程记成这个，不再反复 OpenThread
constexpr uint32_t kUnknownPid = 0xFFFFFFFF;
static_assert((size_t{1} << kIoTableBits) == KernelTrace::kMaxProcesses);

// EVENT_TRACE_PROPERTIES 后面紧跟会话名
struct Properties {
    EVENT_TRACE_PROPERTIES p;
    wchar_t name[64];
};

void InitProperties(Properties& props) {
    props = {};
    props.p.Wnode.BufferSize = sizeof(props);
    props.p.LoggerNameOffset = offsetof(Properties, name);
}

ULONG StopByName() {
    Properties props;
    InitProperties(props);
    return ControlTraceW(0, names::kEtwSession, &props.p, EVENT_TRACE_CONTROL_STOP);
}

}  // namespace

void StopStaleSession() noexcept {
    const ULONG status = StopByName();
    if (status == ERROR_SUCCESS) {
        log::Write(L"etw: stopped a stale %s session", names::kEtwSession);
    }
}

void KernelTrace::SessionTraits::close(pointer h) noexcept {
    Properties props;
    InitProperties(props);
    ControlTraceW(h, nullptr, &props.p, EVENT_TRACE_CONTROL_STOP);
}

StartResult KernelTrace::Start() {
    if (active()) {
        return StartResult::Ok;
    }
    Properties props;
    auto fill = [&props] {
        InitProperties(props);
        props.p.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props.p.Wnode.ClientContext = 1;  // QPC 时间戳，最便宜
        // 不按处理器分缓冲区（D-063）：按处理器分时，主动 Flush 交不出部分处理器的缓冲区，
        // 本机低负载下每轮约 700 KB 磁盘写要等一秒自动刷新，进程列表比卡片慢一轮。
        // 共用一块缓冲区时每次 Flush 都交齐；本机 6.4 万次 / 秒磁盘读也不丢事件，CPU 比按处理器分时还低
        props.p.LogFileMode =
            EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_SYSTEM_LOGGER_MODE | EVENT_TRACE_NO_PER_PROCESSOR_BUFFERING;
        props.p.EnableFlags = EVENT_TRACE_FLAG_DISK_IO | EVENT_TRACE_FLAG_NETWORK_TCPIP | EVENT_TRACE_FLAG_THREAD;
        props.p.BufferSize = 64;  // KB
        props.p.MinimumBuffers = 4;
        props.p.MaximumBuffers = 32;
        props.p.FlushTimer = 1;  // 秒：流量小时缓冲区写不满，至少每秒交付一次
    };
    fill();
    TRACEHANDLE handle = 0;
    ULONG status = StartTraceW(&handle, names::kEtwSession, &props.p);
    if (status == ERROR_ALREADY_EXISTS) {
        // 启动时清理过；还在说明是别的 Pitot 实例刚退出或清理失败，再试一次
        StopByName();
        fill();
        status = StartTraceW(&handle, names::kEtwSession, &props.p);
    }
    if (status != ERROR_SUCCESS) {
        log::Write(L"etw: StartTrace failed %lu", status);
        return status == ERROR_ACCESS_DENIED         ? StartResult::NoAccess
               : status == ERROR_NO_SYSTEM_RESOURCES ? StartResult::NoSlot
                                                     : StartResult::Failed;
    }
    session_.reset(handle);

    if (!threads_.Init(kThreadTableBits) || !io_.Init(kIoTableBits)) {
        Stop();
        return StartResult::Failed;
    }
    dropped_ = 0;
    merge_lost_ = 0;
    buffers_.store(0, std::memory_order_relaxed);
    activity_.store(0, std::memory_order_relaxed);

    EVENT_TRACE_LOGFILEW logfile{};
    logfile.LoggerName = const_cast<wchar_t*>(names::kEtwSession);  // 只读
    // 原始时间戳：会话用 QPC（ClientContext = 1），不让消费端换算成系统时间，好和 Cut 的分界比较（D-063）
    logfile.ProcessTraceMode =
        PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    logfile.EventRecordCallback = &KernelTrace::OnEvent;
    logfile.BufferCallback = &KernelTrace::OnBuffer;
    logfile.Context = this;
    consumer_.reset(OpenTraceW(&logfile));
    if (!consumer_) {
        log::Write(L"etw: OpenTrace failed %lu", GetLastError());
        Stop();
        return StartResult::Failed;
    }
    running_.store(true);
    try {
        thread_ = std::thread([this] { Consume(); });
    } catch (const std::system_error& e) {
        log::Write(L"etw: thread creation failed: %S", e.what());
        running_.store(false);
        Stop();
        return StartResult::Failed;
    }
    log::Write(L"etw: session started");
    return StartResult::Ok;
}

void KernelTrace::Stop() noexcept {
    const bool had_session = static_cast<bool>(session_);
    Properties props;
    InitProperties(props);
    ULONG status = ERROR_SUCCESS;
    if (had_session) {
        status = ControlTraceW(session_.release(), nullptr, &props.p, EVENT_TRACE_CONTROL_STOP);
    }
    // CloseTrace 让 ProcessTrace 处理完当前缓冲区就返回
    consumer_.reset();
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
    if (had_session) {
        log::Write(L"etw: session stopped %lu; events lost %lu, realtime buffers lost %lu, unattributed %llu", status,
                   props.p.EventsLost, props.p.RealTimeBuffersLost, dropped_ + merge_lost_);
    }
    threads_.Release();
    AcquireSRWLockExclusive(&lock_);
    io_.Release();
    ReleaseSRWLockExclusive(&lock_);
}

void KernelTrace::Consume() noexcept {
    SetThreadDescription(GetCurrentThread(), L"Pitot ETW");
    TRACEHANDLE handle = consumer_.get();
    const ULONG status = ProcessTrace(&handle, 1, nullptr, nullptr);
    if (status != ERROR_SUCCESS && status != ERROR_CANCELLED) {
        log::Write(L"etw: ProcessTrace returned %lu", status);
    }
    running_.store(false);
}

void WINAPI KernelTrace::OnEvent(_EVENT_RECORD* record) {
    // C 回调：Handle 不抛异常（AGENTS.md）
    if (record && record->UserContext) {
        static_cast<KernelTrace*>(record->UserContext)->Handle(*record);
    }
}

uint32_t KernelTrace::PidOfThread(uint32_t tid) noexcept {
    if (const uint32_t* pid = threads_.Find(tid)) {
        return *pid;
    }
    // 线程枚举还没处理到、或者错过了创建事件：直接问系统一次，结果（包括失败）记进表里
    uint32_t pid = kUnknownPid;
    unique_handle thread(OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid));
    if (thread) {
        // 返回 0 表示失败（进程 0 只有空闲线程，不会发起 I/O）
        if (const DWORD id = GetProcessIdOfThread(thread.get()); id != 0) {
            pid = id;
        }
    }
    if (uint32_t* slot = threads_.FindOrInsert(tid)) {
        *slot = pid;
    }
    return pid;
}

void KernelTrace::Handle(const _EVENT_RECORD& record) noexcept {
    const EVENT_HEADER& h = record.EventHeader;
    const ParsedEvent e = ParseEvent(h.ProviderId, h.EventDescriptor.Opcode, record.UserData, record.UserDataLength,
                                     (h.Flags & EVENT_HEADER_FLAG_64_BIT_HEADER) != 0);
    uint32_t pid = e.pid;
    switch (e.kind) {
    case EventKind::None:
        return;
    case EventKind::ThreadStart:
        if (uint32_t* slot = threads_.FindOrInsert(e.tid)) {
            *slot = e.pid;
        }
        return;
    case EventKind::ThreadEnd:
        threads_.Erase(e.tid);
        return;
    case EventKind::DiskRead:
    case EventKind::DiskWrite:
        pid = PidOfThread(e.tid);
        break;
    case EventKind::NetReceive:
    case EventKind::NetSend:
        break;
    }
    if (pid == kUnknownPid) {
        ++dropped_;
        return;
    }
    AcquireSRWLockExclusive(&lock_);
    if (IoCounters* c = io_.Slot(pid + 1, h.TimeStamp.QuadPart)) {
        switch (e.kind) {
        case EventKind::DiskRead:
            c->disk_read += e.bytes;
            break;
        case EventKind::DiskWrite:
            c->disk_write += e.bytes;
            break;
        case EventKind::NetReceive:
            c->net_receive += e.bytes;
            break;
        default:
            c->net_send += e.bytes;
            break;
        }
        if (e.bytes > 0) {
            const unsigned bit = e.kind == EventKind::DiskRead || e.kind == EventKind::DiskWrite ? 1u : 2u;
            const unsigned seen = activity_.load(std::memory_order_relaxed);
            if ((seen & bit) == 0) {
                activity_.store(seen | bit, std::memory_order_relaxed);
            }
        }
    } else {
        ++dropped_;
    }
    ReleaseSRWLockExclusive(&lock_);
}

ULONG WINAPI KernelTrace::OnBuffer(EVENT_TRACE_LOGFILEW* logfile) {
    if (logfile && logfile->Context) {
        auto* self = static_cast<KernelTrace*>(logfile->Context);
        self->buffers_.fetch_add(1, std::memory_order_release);
    }
    return TRUE;
}

bool KernelTrace::Flush() noexcept {
    if (!session_) {
        return false;
    }
    Properties props;
    InitProperties(props);
    const ULONG status = ControlTraceW(session_.get(), nullptr, &props.p, EVENT_TRACE_CONTROL_FLUSH);
    if (status != ERROR_SUCCESS) {
        log::Write(L"etw: flush failed %lu", status);
        return false;
    }
    return true;
}

void KernelTrace::WaitForBuffers(uint32_t before) noexcept {
    // 不用 WaitOnAddress：它在 synchronization.lib 里，而 Sleep(1) 在这块机器上大约就是 15 ms，
    // 和 20 ms 上限同一量级，够把刚刷新的缓冲区收进来
    const ULONGLONG deadline = GetTickCount64() + 20;
    uint32_t seen = before;
    ULONGLONG quiet_at = 0;
    while (GetTickCount64() < deadline) {
        const ULONGLONG now = GetTickCount64();
        const uint32_t count = buffers_.load(std::memory_order_acquire);
        if (count != seen) {
            seen = count;
            quiet_at = now + 4;
        } else if (quiet_at != 0 && now >= quiet_at) {
            return;
        }
        Sleep(1);
    }
}

void KernelTrace::FlushThrough() noexcept {
    const uint32_t before = buffers_.load(std::memory_order_acquire);
    if (Flush()) {
        WaitForBuffers(before);
    }
}

void KernelTrace::Cut(int64_t qpc) noexcept {
    AcquireSRWLockExclusive(&lock_);
    merge_lost_ += io_.Cut(qpc);
    ReleaseSRWLockExclusive(&lock_);
}

void KernelTrace::Drain(std::vector<IoSample>& out) {
    out.clear();
    out.reserve(kMaxProcesses);  // 容量够时什么也不做；持锁时就不会再分配
    AcquireSRWLockExclusive(&lock_);
    io_.Drain([&out](uint32_t key, const IoCounters& c) {
        out.push_back({key - 1, c});
    });
    ReleaseSRWLockExclusive(&lock_);
    std::sort(out.begin(), out.end(), [](const IoSample& a, const IoSample& b) { return a.pid < b.pid; });
}

}  // namespace pitot::etw
