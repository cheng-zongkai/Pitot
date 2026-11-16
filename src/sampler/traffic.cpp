#include "sampler/traffic.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <vector>

#include "common/unique_handle.h"
#include "config/config.h"

namespace pitot::sampler {
namespace {

constexpr char kHeader[] = "# Pitot traffic v1: date adapter received sent";

bool SameKey(const GUID& a, const GUID& b) noexcept { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

// 公历日期和“从 1970-01-01 起的天数”互换（Howard Hinnant 的 days_from_civil / civil_from_days）
int64_t DaysFromCivil(int64_t y, int64_t m, int64_t d) noexcept {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int64_t yoe = y - era * 400;
    const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void CivilFromDays(int64_t z, int& year, int& month, int& day) noexcept {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const int64_t doe = z - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year = static_cast<int>(yoe + era * 400 + (month <= 2));
}

void Split(uint32_t date, int& y, int& m, int& d) noexcept {
    y = static_cast<int>(date / 10000);
    m = static_cast<int>(date / 100 % 100);
    d = static_cast<int>(date % 100);
}

bool ParseU64(const char*& p, const char* end, uint64_t& out) noexcept {
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    if (p == end || *p < '0' || *p > '9') {
        return false;
    }
    uint64_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + static_cast<uint64_t>(*p - '0');
        ++p;
    }
    out = v;
    return true;
}

bool ParseKey(const char*& p, const char* end, GUID& out) noexcept {
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    wchar_t word[48];
    size_t n = 0;
    while (p < end && *p != ' ' && *p != '\t' && n + 1 < std::size(word)) {
        word[n++] = static_cast<wchar_t>(static_cast<unsigned char>(*p++));
    }
    word[n] = L'\0';
    if (wcscmp(word, L"auto") == 0) {
        out = kAutoTrafficKey;
        return true;
    }
    const auto guid = config::ParseGuid(word);
    if (!guid) {
        return false;
    }
    out = *guid;
    return true;
}

}  // namespace

uint32_t MakeDate(int year, int month, int day) noexcept {
    return static_cast<uint32_t>(year * 10000 + month * 100 + day);
}

uint32_t AddDays(uint32_t date, int days) noexcept {
    int y = 0;
    int m = 0;
    int d = 0;
    Split(date, y, m, d);
    CivilFromDays(DaysFromCivil(y, m, d) + days, y, m, d);
    return MakeDate(y, m, d);
}

uint32_t PeriodStart(uint32_t date, int reset_day) noexcept {
    int y = 0;
    int m = 0;
    int d = 0;
    Split(date, y, m, d);
    if (d >= reset_day) {
        return MakeDate(y, m, reset_day);
    }
    return m == 1 ? MakeDate(y - 1, 12, reset_day) : MakeDate(y, m - 1, reset_day);
}

bool TrafficBook::Add(uint32_t date, const GUID& key, uint64_t rx, uint64_t tx) noexcept {
    if (rx == 0 && tx == 0) {
        return true;
    }
    dirty_ = true;
    for (int i = count_ - 1; i >= 0; --i) {
        Record& r = records_[i];
        if (r.date == date && SameKey(r.key, key)) {
            r.rx += rx;
            r.tx += tx;
            return true;
        }
    }
    if (count_ == kMaxRecords) {
        RemoveOldestDate();
    }
    if (count_ == kMaxRecords) {
        return false;
    }
    records_[count_++] = {date, key, rx, tx};
    return true;
}

TrafficTotals TrafficBook::Sum(const GUID& key, uint32_t from, uint32_t to) const noexcept {
    TrafficTotals t;
    for (int i = 0; i < count_; ++i) {
        const Record& r = records_[i];
        if (r.date >= from && r.date <= to && SameKey(r.key, key)) {
            t.rx += r.rx;
            t.tx += r.tx;
        }
    }
    return t;
}

void TrafficBook::Prune(uint32_t oldest) noexcept {
    int kept = 0;
    for (int i = 0; i < count_; ++i) {
        if (records_[i].date >= oldest) {
            records_[kept++] = records_[i];
        }
    }
    if (kept != count_) {
        count_ = kept;
        dirty_ = true;
    }
}

void TrafficBook::RemoveOldestDate() noexcept {
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < count_; ++i) {
        oldest = records_[i].date < oldest ? records_[i].date : oldest;
    }
    Prune(oldest + 1);
}

void TrafficBook::FormatLine(const Record& r, char* out, size_t size) noexcept {
    char key[48] = "auto";
    if (!SameKey(r.key, kAutoTrafficKey)) {
        wchar_t wide[40];
        config::FormatGuid(r.key, wide, std::size(wide));
        for (size_t i = 0; i < std::size(key) - 1 && wide[i]; ++i) {
            key[i] = static_cast<char>(wide[i]);
            key[i + 1] = '\0';
        }
    }
    _snprintf_s(out, size, _TRUNCATE, "%08u %s %llu %llu", r.date, key, static_cast<unsigned long long>(r.rx),
                static_cast<unsigned long long>(r.tx));
}

void TrafficBook::Parse(const char* text, size_t length) noexcept {
    count_ = 0;
    dirty_ = false;
    const char* p = text;
    const char* const end = text + length;
    while (p < end) {
        const char* line_end = static_cast<const char*>(std::memchr(p, '\n', static_cast<size_t>(end - p)));
        if (!line_end) {
            line_end = end;
        }
        const char* q = p;
        uint64_t date = 0;
        GUID key{};
        uint64_t rx = 0;
        uint64_t tx = 0;
        // 认不出的行（注释、写坏的行）跳过
        if (*q != '#' && ParseU64(q, line_end, date) && date >= 19700101 && date <= 99991231 &&
            ParseKey(q, line_end, key) && ParseU64(q, line_end, rx) && ParseU64(q, line_end, tx) &&
            count_ < kMaxRecords) {
            records_[count_++] = {static_cast<uint32_t>(date), key, rx, tx};
        }
        p = line_end + 1;
    }
}

bool TrafficBook::Load(const wchar_t* path) noexcept {
    count_ = 0;
    unique_file_handle file(
        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        return false;
    }
    LARGE_INTEGER size{};
    // 正常不到 40 KB；太大说明不是我们的文件
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart > 1 << 20) {
        return false;
    }
    try {
        std::vector<char> text(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!text.empty() && !ReadFile(file.get(), text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) {
            return false;
        }
        Parse(text.data(), read);
    } catch (...) {
        return false;
    }
    return true;
}

bool TrafficBook::Save(const wchar_t* path) noexcept {
    wchar_t temp[MAX_PATH];
    if (_snwprintf_s(temp, _TRUNCATE, L"%s.tmp", path) < 0) {
        return false;
    }
    {
        unique_file_handle file(
            CreateFileW(temp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) {
            return false;
        }
        auto write = [&file](const char* line) {
            char buf[112];
            const int n = _snprintf_s(buf, _TRUNCATE, "%s\r\n", line);
            DWORD written = 0;
            return n > 0 && WriteFile(file.get(), buf, static_cast<DWORD>(n), &written, nullptr) &&
                   written == static_cast<DWORD>(n);
        };
        if (!write(kHeader) || !ForEachLine(write)) {
            return false;
        }
    }
    if (!MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return false;
    }
    dirty_ = false;
    return true;
}

}  // namespace pitot::sampler
