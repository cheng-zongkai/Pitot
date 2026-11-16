#include "sampler/cpu_info.h"

#include <windows.h>

#include <intrin.h>

#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <vector>

#include "sampler/demo_mode.h"

namespace pitot::sampler {
namespace {

// 从后往前去掉的后缀（不区分大小写），以及去掉的前缀
constexpr const wchar_t* kSuffixes[] = {L" Processor", L"-Core", L" with Radeon Graphics", L" w/ Radeon Graphics"};
constexpr const wchar_t* kPrefixes[] = {L"AMD ", L"Intel(R) Core(TM) ", L"Intel(R) ", L"Intel "};

bool EndsWithI(const wchar_t* s, size_t len, const wchar_t* suffix) {
    const size_t n = wcslen(suffix);
    return len >= n && _wcsnicmp(s + len - n, suffix, n) == 0;
}

}  // namespace

void ReadCpuBrand(wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (DemoMode()) {
        wcsncpy_s(out, size, L"AMD Ryzen 7 5800X 8-Core Processor", _TRUNCATE);
        return;
    }
    int regs[12]{};
    __cpuid(regs, 0x80000002);
    __cpuid(regs + 4, 0x80000003);
    __cpuid(regs + 8, 0x80000004);
    char brand[49]{};
    memcpy(brand, regs, sizeof(regs));
    size_t i = 0;
    for (; i + 1 < size && brand[i]; ++i) {
        out[i] = static_cast<unsigned char>(brand[i]);
    }
    out[i] = L'\0';
}

void ShortCpuName(const wchar_t* brand, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    wchar_t buf[64];
    // 合并连续空白，去掉首尾空白
    size_t len = 0;
    bool space = true;
    for (const wchar_t* p = brand; *p && len + 1 < std::size(buf); ++p) {
        if (iswspace(*p)) {
            if (!space) {
                buf[len++] = L' ';
            }
            space = true;
        } else {
            buf[len++] = *p;
            space = false;
        }
    }
    while (len > 0 && buf[len - 1] == L' ') {
        --len;
    }
    buf[len] = L'\0';

    // 后缀：" Processor"，然后 "-Core" 连同前面的核心数（"6-Core"、"Six-Core"）
    bool changed = true;
    while (changed) {
        changed = false;
        for (const wchar_t* suffix : kSuffixes) {
            if (EndsWithI(buf, len, suffix)) {
                len -= wcslen(suffix);
                if (_wcsicmp(suffix, L"-Core") == 0) {
                    while (len > 0 && buf[len - 1] != L' ') {
                        --len;
                    }
                }
                while (len > 0 && buf[len - 1] == L' ') {
                    --len;
                }
                buf[len] = L'\0';
                changed = true;
            }
        }
    }

    const wchar_t* start = buf;
    for (const wchar_t* prefix : kPrefixes) {
        const size_t n = wcslen(prefix);
        if (_wcsnicmp(start, prefix, n) == 0) {
            start += n;
            break;
        }
    }
    wcsncpy_s(out, size, start, _TRUNCATE);
}

int ParseProcessorInstance(const wchar_t* name) {
    if (!name) {
        return -1;
    }
    int group = 0;
    int number = 0;
    const wchar_t* p = name;
    if (!iswdigit(*p)) {
        return -1;
    }
    while (iswdigit(*p)) {
        group = group * 10 + (*p++ - L'0');
        if (group > 64) {
            return -1;
        }
    }
    if (*p++ != L',' || !iswdigit(*p)) {
        return -1;
    }
    while (iswdigit(*p)) {
        number = number * 10 + (*p++ - L'0');
        if (number >= 64) {
            return -1;
        }
    }
    return *p == L'\0' ? group * 64 + number : -1;
}

bool ReadCpuTopology(CpuTopology& out) {
    out = CpuTopology{};
    if (DemoMode()) {
        // 演示模式：8 核 16 线程，和 FillDemoMetrics 的 16 个逻辑处理器对上
        for (int c = 0; c < 8; ++c) {
            CpuCore& core = out.cores[static_cast<size_t>(c)];
            core.threads = {2 * c, 2 * c + 1, 0, 0};
            core.thread_count = 2;
        }
        out.core_count = 8;
        out.thread_count = 16;
        return true;
    }
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return false;
    }
    std::vector<BYTE> buffer(size);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
                                          reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()),
                                          &size)) {
        return false;
    }
    for (DWORD offset = 0; offset < size;) {
        const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        offset += info->Size;
        if (info->Relationship != RelationProcessorCore || out.core_count >= kMaxCpus) {
            continue;
        }
        CpuCore& core = out.cores[static_cast<size_t>(out.core_count)];
        for (WORD g = 0; g < info->Processor.GroupCount; ++g) {
            const GROUP_AFFINITY& affinity = info->Processor.GroupMask[g];
            for (int bit = 0; bit < 64; ++bit) {
                if ((affinity.Mask >> bit) & 1) {
                    const int index = affinity.Group * 64 + bit;
                    if (index < kMaxCpus && core.thread_count < static_cast<int>(core.threads.size())) {
                        core.threads[static_cast<size_t>(core.thread_count++)] = index;
                        ++out.thread_count;
                    }
                }
            }
        }
        if (core.thread_count > 0) {
            ++out.core_count;
        }
    }
    return out.core_count > 0;
}

}  // namespace pitot::sampler
