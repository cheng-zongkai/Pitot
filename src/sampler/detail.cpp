#include "sampler/detail.h"

#include <psapi.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>

#include "common/log.h"
#include "common/unique_handle.h"
#include "sampler/nt_system.h"

namespace pitot::sampler {
namespace {

constexpr ULONG kInitialBuffer = 512 * 1024;
constexpr ULONG kBufferGranularity = 64 * 1024;
constexpr size_t kReserveProcesses = 1024;
constexpr wchar_t kMemoryCompression[] = L"Memory Compression";

bool EqualsIgnoreCase(const wchar_t* s, size_t length, const wchar_t* literal) {
    return CompareStringOrdinal(s, static_cast<int>(length), literal, -1, TRUE) == CSTR_EQUAL;
}

bool EnablePrivilege(const wchar_t* name) {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw)) {
        return false;
    }
    unique_handle token(raw);
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid)) {
        return false;
    }
    // 成功返回时还要看 GetLastError：令牌里根本没有这个特权会是 ERROR_NOT_ALL_ASSIGNED
    return AdjustTokenPrivileges(token.get(), FALSE, &tp, sizeof(tp), nullptr, nullptr) &&
           GetLastError() == ERROR_SUCCESS;
}

}  // namespace

void DetailSampler::Init() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    page_size_ = si.dwPageSize;
    cpu_count_ = std::max<DWORD>(1, si.dwNumberOfProcessors);
    if (!EnablePrivilege(SE_PROF_SINGLE_PROCESS_NAME)) {
        log::Write(L"detail: SeProfileSingleProcessPrivilege not enabled %lu", GetLastError());
    }
}

void DetailSampler::Reset() {
    FreeBuffer();
    std::vector<ProcessRecord>().swap(records_);
    stats_.Reset();
    gpu_groups_.Reset();
    io_groups_.Reset();
    std::vector<App>().swap(apps_);
    std::unordered_map<std::wstring, uint32_t>().swap(app_index_);
    std::vector<ProcessApp>().swap(process_apps_);
    std::vector<ProcessApp>().swap(next_process_apps_);
    last_tick_ = 0;
}

void DetailSampler::FreeBuffer() {
    if (buffer_) {
        VirtualFree(buffer_, 0, MEM_RELEASE);
        buffer_ = nullptr;
        buffer_size_ = 0;
    }
}

void DetailSampler::Sample(ULONGLONG now, std::optional<double> cpu_total, int top_n, bool english, Snapshot& s) {
    s.detail = true;
    if (english != english_names_) {
        // 界面语言变了，友好名称按新语言重新取
        english_names_ = english;
        for (App& app : apps_) {
            app.resolved = false;
        }
    }

    PERFORMANCE_INFORMATION pi{sizeof(pi)};
    if (GetPerformanceInfo(&pi, sizeof(pi))) {
        page_size_ = pi.PageSize;
        physical_total_ = static_cast<uint64_t>(pi.PhysicalTotal) * page_size_;
        s.commit_bytes = static_cast<uint64_t>(pi.CommitTotal) * page_size_;
        s.commit_limit_bytes = static_cast<uint64_t>(pi.CommitLimit) * page_size_;
    }
    SampleSwap(s);
    SampleMemoryLists(s);
    SampleProcesses(now, cpu_total, top_n, s);
}

void DetailSampler::SampleSwap(Snapshot& s) {
    s.swap_used_bytes.reset();
    uint64_t pages = 0;
    // TotalInUse 是页面文件当前占用，不是提交量或峰值。
    const auto collect = [](LPVOID context, PENUM_PAGE_FILE_INFORMATION info, LPCWSTR) -> BOOL {
        *static_cast<uint64_t*>(context) += static_cast<uint64_t>(info->TotalInUse);
        return TRUE;
    };
    if (EnumPageFilesW(collect, &pages)) {
        s.swap_used_bytes = static_cast<double>(pages) * static_cast<double>(page_size_);
    }
}

void DetailSampler::SampleMemoryLists(Snapshot& s) {
    if (lists_denied_ || physical_total_ == 0) {
        return;
    }
    nt::SystemMemoryListInfo info{};
    const NTSTATUS status = NtQuerySystemInformation(nt::kSystemMemoryListInformation, &info, sizeof(info), nullptr);
    if (status != nt::kStatusSuccess) {
        log::Write(L"detail: memory lists unavailable 0x%08lX", static_cast<unsigned long>(status));
        lists_denied_ = true;
        return;
    }
    MemoryLists m;
    m.total = physical_total_;
    m.free = (static_cast<uint64_t>(info.ZeroPageCount) + info.FreePageCount) * page_size_;
    m.modified = (static_cast<uint64_t>(info.ModifiedPageCount) + info.ModifiedNoWritePageCount) * page_size_;
    uint64_t standby = 0;
    for (ULONG_PTR pages : info.PageCountByPriority) {
        standby += pages;
    }
    m.standby = standby * page_size_;
    const uint64_t others = m.free + m.modified + m.standby;
    m.in_use = m.total > others ? m.total - others : 0;
    s.memory_lists = m;
}

bool DetailSampler::QueryProcesses() {
    // 进程数会变，缓冲区不够就按系统给的大小加一点余量重来
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (!buffer_) {
            buffer_size_ = std::max(buffer_size_, kInitialBuffer);
            buffer_ = VirtualAlloc(nullptr, buffer_size_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!buffer_) {
                buffer_size_ = 0;
                return false;
            }
        }
        ULONG needed = 0;
        const NTSTATUS status = NtQuerySystemInformation(nt::kSystemProcessInformation, buffer_, buffer_size_, &needed);
        if (status == nt::kStatusSuccess) {
            return true;
        }
        if (status != nt::kStatusInfoLengthMismatch) {
            log::Write(L"detail: process snapshot failed 0x%08lX", static_cast<unsigned long>(status));
            return false;
        }
        const ULONG wanted = std::max(needed, buffer_size_) + kBufferGranularity;
        FreeBuffer();
        buffer_size_ = (wanted + kBufferGranularity - 1) / kBufferGranularity * kBufferGranularity;
    }
    return false;
}

void DetailSampler::SampleProcesses(ULONGLONG now, std::optional<double> cpu_total, int top_n, Snapshot& s) {
    if (!QueryProcesses()) {
        return;
    }
    if (records_.capacity() == 0) {
        records_.reserve(kReserveProcesses);
        stats_.Reserve(kReserveProcesses);
        process_apps_.reserve(kReserveProcesses);
        next_process_apps_.reserve(kReserveProcesses);
    }
    records_.clear();
    next_process_apps_.clear();
    const auto* base = static_cast<const BYTE*>(buffer_);
    for (ULONG offset = 0;;) {
        const auto* p = reinterpret_cast<const nt::SystemProcessInfo*>(base + offset);
        ProcessRecord r;
        r.pid = reinterpret_cast<uint64_t>(p->UniqueProcessId);
        r.create_time = p->CreateTime.QuadPart;
        r.cpu_time = p->KernelTime.QuadPart + p->UserTime.QuadPart;
        r.private_bytes = static_cast<uint64_t>(p->WorkingSetPrivateSize.QuadPart);
        const wchar_t* name = p->ImageName.Buffer;
        const size_t name_length = name ? p->ImageName.Length / sizeof(wchar_t) : 0;
        if (r.pid == 0 || name_length == 0) {
            r.group = kHiddenGroup;  // 空闲进程
        } else if (EqualsIgnoreCase(name, name_length, kMemoryCompression)) {
            // 不进列表，在内存面板单独显示为“已压缩”
            r.group = kHiddenGroup;
            s.compressed_bytes = static_cast<uint64_t>(p->WorkingSetSize);
        } else {
            // 上一轮见过的进程直接沿用它的程序编号，只有新进程才去查路径
            const auto it = std::lower_bound(process_apps_.begin(), process_apps_.end(), r.pid,
                                             [](const ProcessApp& a, uint64_t pid) { return a.pid < pid; });
            if (it != process_apps_.end() && it->pid == r.pid && it->create_time == r.create_time) {
                r.group = it->app;
            } else {
                r.group = AppFor(r.pid, name, name_length);
            }
            next_process_apps_.push_back({r.pid, r.create_time, r.group});
        }
        records_.push_back(r);
        if (p->NextEntryOffset == 0) {
            break;
        }
        offset += p->NextEntryOffset;
    }
    std::sort(next_process_apps_.begin(), next_process_apps_.end(),
              [](const ProcessApp& a, const ProcessApp& b) { return a.pid < b.pid; });
    process_apps_.swap(next_process_apps_);

    const double capacity = last_tick_ ? static_cast<double>(now - last_tick_) * 10000.0 * cpu_count_ : 0.0;
    last_tick_ = now;
    stats_.Update(records_, cpu_total, capacity, top_n, s);
    FillNames(s.top_memory, s.top_memory_count);
    FillNames(s.top_cpu, s.top_cpu_count);
}

void DetailSampler::SampleGpu(std::vector<GpuProcessUsage>& usage, bool valid, int top_n, Snapshot& s) {
    size_t cursor = 0;
    for (GpuProcessUsage& u : usage) {
        u.app = AppOfPid(u.pid, cursor);
    }
    gpu_groups_.Reserve(usage.capacity());  // 容量够时什么也不做
    s.top_gpu_count = gpu_groups_.Build(usage, top_n, s.top_gpu);
    s.top_gpu_valid = valid;
    FillNames(s.top_gpu, s.top_gpu_count);
}

void DetailSampler::SampleIo(std::vector<IoProcessUsage>& disk, std::vector<IoProcessUsage>& net, int top_n,
                             Snapshot& s) {
    io_groups_.Reserve(std::max(disk.capacity(), net.capacity()));
    size_t cursor = 0;
    for (IoProcessUsage& u : disk) {
        u.app = AppOfPid(u.pid, cursor);
    }
    s.top_disk_count = io_groups_.Build(disk, top_n, s.top_disk);
    FillNames(s.top_disk, s.top_disk_count);
    cursor = 0;
    for (IoProcessUsage& u : net) {
        u.app = AppOfPid(u.pid, cursor);
    }
    s.top_net_count = io_groups_.Build(net, top_n, s.top_net);
    FillNames(s.top_net, s.top_net_count);
}

uint32_t DetailSampler::AppOfPid(uint32_t pid, size_t& cursor) const {
    // 两边都按 pid 排好，归并一遍就能给每个进程找到它的程序
    while (cursor < process_apps_.size() && process_apps_[cursor].pid < pid) {
        ++cursor;
    }
    return cursor < process_apps_.size() && process_apps_[cursor].pid == pid ? process_apps_[cursor].app
                                                                              : kHiddenGroup;
}

uint32_t DetailSampler::AppFor(uint64_t pid, const wchar_t* image, size_t image_length) {
    // 按可执行文件的完整路径归组：同名但不同位置的程序分开，和任务管理器一致
    wchar_t path[MAX_PATH * 2];
    DWORD size = static_cast<DWORD>(std::size(path));
    path[0] = L'\0';
    unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid)));
    if (!process || !QueryFullProcessImageNameW(process.get(), 0, path, &size)) {
        path[0] = L'\0';  // System、Registry 等没有路径，按映像名归组
    }
    std::wstring key = path[0] ? std::wstring(path) : L"name:" + std::wstring(image, image_length);
    CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
    const auto found = app_index_.find(key);
    if (found != app_index_.end()) {
        return found->second;
    }
    const auto id = static_cast<uint32_t>(apps_.size());
    App& app = apps_.emplace_back();
    app.path = path;
    const size_t n = std::min(image_length, std::size(app.image) - 1);
    wmemcpy(app.image, image, n);
    app.image[n] = L'\0';
    app_index_.emplace(std::move(key), id);
    return id;
}

void DetailSampler::Resolve(App& app) {
    app.resolved = true;
    wcscpy_s(app.name, app.image);
    if (app.path.empty()) {
        return;
    }
    // 友好名称取文件说明（任务管理器“进程”页的叫法）。中文界面取系统显示语言的本地化版本，
    // 例如 svchost.exe → “Windows 服务主机进程”；英文界面取文件自带的中性版本（Windows 组件是英文）
    DWORD handle = 0;
    const DWORD flags = english_names_ ? FILE_VER_GET_NEUTRAL : FILE_VER_GET_LOCALISED;
    const DWORD size = GetFileVersionInfoSizeExW(flags, app.path.c_str(), &handle);
    if (size == 0) {
        return;
    }
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoExW(flags, app.path.c_str(), 0, size, data.data())) {
        return;
    }
    struct Translation {
        WORD language;
        WORD code_page;
    };
    Translation* translations = nullptr;
    UINT bytes = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations),
                        &bytes) ||
        bytes < sizeof(Translation)) {
        return;
    }
    wchar_t query[64];
    _snwprintf_s(query, _TRUNCATE, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[0].language,
                 translations[0].code_page);
    wchar_t* description = nullptr;
    UINT length = 0;
    if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&description), &length) && description &&
        length > 1) {
        wchar_t trimmed[64];
        wcsncpy_s(trimmed, description, _TRUNCATE);
        size_t end = wcslen(trimmed);
        while (end > 0 && iswspace(trimmed[end - 1])) {
            trimmed[--end] = L'\0';
        }
        if (end > 0) {
            wcscpy_s(app.name, trimmed);
        }
    }
}

void DetailSampler::FillNames(std::array<ProcessEntry, kMaxTopProcesses>& list, int count) {
    for (int i = 0; i < count; ++i) {
        ProcessEntry& e = list[static_cast<size_t>(i)];
        if (e.app_id >= apps_.size()) {
            continue;
        }
        for (int c = 0; c < e.child_count; ++c) {
            auto& child = e.children[static_cast<size_t>(c)];
            child.create_time = 0;
            for (const auto& record : records_) {
                if (record.pid == child.pid) { child.create_time = record.create_time; break; }
            }
        }
        // 名称和图标只给上榜的程序取：读版本资源和图标都要读文件，不必对几百个进程都做
        App& app = apps_[e.app_id];
        if (!app.resolved) {
            Resolve(app);
        }
        if (!icons_.Valid(app.icon, app.icon_version)) {
            app.icon = icons_.Extract(app.path.c_str(), app.icon_version);
        }
        icons_.Touch(app.icon);
        wcscpy_s(e.name, app.name);
        wcscpy_s(e.image, app.image);
        e.icon = app.icon;
        e.icon_version = app.icon_version;
    }
}

}  // namespace pitot::sampler