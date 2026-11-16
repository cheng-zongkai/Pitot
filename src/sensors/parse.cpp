#include "sensors/parse.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace pitot::sensors {
namespace {

// 读 0x 开头的十六进制数，返回读完后的位置；失败返回空
const wchar_t* ReadHex(const wchar_t* p, uint32_t& value) {
    if (p[0] != L'0' || (p[1] != L'x' && p[1] != L'X')) {
        return nullptr;
    }
    p += 2;
    const wchar_t* start = p;
    uint64_t v = 0;
    while (iswxdigit(*p)) {
        v = v * 16 + static_cast<uint64_t>(iswdigit(*p) ? *p - L'0' : towlower(*p) - L'a' + 10);
        if (v > 0xFFFFFFFFull) {
            return nullptr;
        }
        ++p;
    }
    if (p == start) {
        return nullptr;
    }
    value = static_cast<uint32_t>(v);
    return p;
}

bool StartsWith(const wchar_t* s, const wchar_t* prefix) { return wcsncmp(s, prefix, wcslen(prefix)) == 0; }

}  // namespace

std::optional<uint32_t> ParseDiskInstance(const wchar_t* name) {
    if (!name || !iswdigit(name[0])) {
        return std::nullopt;
    }
    uint64_t n = 0;
    const wchar_t* p = name;
    while (iswdigit(*p)) {
        n = n * 10 + static_cast<uint64_t>(*p - L'0');
        if (n > 0xFFFF) {
            return std::nullopt;
        }
        ++p;
    }
    if (*p != L'\0' && *p != L' ') {
        return std::nullopt;
    }
    return static_cast<uint32_t>(n);
}

void CleanModelName(const char* in, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    size_t n = 0;
    bool pending_space = false;
    for (const char* p = in ? in : ""; *p && n + 1 < size; ++p) {
        const auto c = static_cast<unsigned char>(*p);
        if (c == ' ' || c == '\t') {
            pending_space = n > 0;
            continue;
        }
        if (c < 0x20 || c >= 0x7F) {
            continue;  // 型号字段是 ASCII，其他字节当噪声丢掉
        }
        if (pending_space && n + 2 < size) {
            out[n++] = L' ';
        }
        pending_space = false;
        out[n++] = static_cast<wchar_t>(c);
    }
    out[n] = L'\0';
}

std::optional<GpuInstance> ParseGpuInstance(const wchar_t* name) {
    if (!name || !StartsWith(name, L"pid_")) {
        return std::nullopt;
    }
    const wchar_t* p = name + 4;
    uint64_t pid = 0;
    const wchar_t* digits = p;
    while (iswdigit(*p)) {
        pid = pid * 10 + static_cast<uint64_t>(*p - L'0');
        if (pid > 0xFFFFFFFFull) {
            return std::nullopt;
        }
        ++p;
    }
    if (p == digits || !StartsWith(p, L"_luid_")) {
        return std::nullopt;
    }
    GpuInstance g;
    g.pid = static_cast<uint32_t>(pid);
    p = ReadHex(p + 6, g.luid_high);
    if (!p || *p != L'_') {
        return std::nullopt;
    }
    p = ReadHex(p + 1, g.luid_low);
    if (!p || (*p != L'\0' && *p != L'_')) {
        return std::nullopt;
    }
    return g;
}

std::optional<double> NodeUtilization(int64_t previous, int64_t current, int64_t elapsed_100ns) {
    if (elapsed_100ns <= 0 || current < previous) {
        return std::nullopt;
    }
    const double percent = static_cast<double>(current - previous) / static_cast<double>(elapsed_100ns) * 100.0;
    return std::clamp(percent, 0.0, 100.0);
}

}  // namespace pitot::sensors
