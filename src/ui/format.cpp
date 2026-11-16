#include "ui/format.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <iterator>

#include "ui/strings.h"

namespace pitot::ui {
namespace {

constexpr const auto& kRateUnits = kAllRateUnits;
constexpr const wchar_t* kByteUnits[] = {L"B", L"KB", L"MB", L"GB", L"TB"};

void WriteUnavailable(wchar_t* out, size_t size) { wcsncpy_s(out, size, Str().kUnavailable, _TRUNCATE); }

bool Usable(std::optional<double> v) { return v && std::isfinite(*v); }

// 按 1024 进制选单位：数值四舍五入后会变成 4 位数时就进位，保证最多 3 位整数
template <size_t N>
size_t PickUnit(double& value, const wchar_t* const (&)[N]) {
    size_t unit = 0;
    while (value >= 999.5 && unit + 1 < N) {
        value /= 1024.0;
        ++unit;
    }
    return unit;
}

}  // namespace

void FormatPercent(std::optional<double> percent, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (!Usable(percent)) {
        WriteUnavailable(out, size);
        return;
    }
    _snwprintf_s(out, size, _TRUNCATE, L"%.0f%%", std::clamp(*percent, 0.0, 100.0));
}

void FormatPercentDetailed(std::optional<double> percent, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (!Usable(percent)) {
        WriteUnavailable(out, size);
        return;
    }
    _snwprintf_s(out, size, _TRUNCATE, L"%.1f%%", std::clamp(*percent, 0.0, 100.0));
}

void FormatRateParts(std::optional<double> bytes_per_sec, wchar_t* number, size_t size, const wchar_t*& unit) {
    unit = L"";
    if (size == 0) {
        return;
    }
    if (!Usable(bytes_per_sec)) {
        WriteUnavailable(number, size);
        return;
    }
    const double bytes = std::max(0.0, *bytes_per_sec);
    // 最小单位是 KB/s：不到 1 KB/s 直接记作 0，单位列只有宽度相近的 KB/s、MB/s……（D-036）
    if (bytes < 1024.0) {
        wcsncpy_s(number, size, L"0", _TRUNCATE);
        unit = kRateUnits[0];
        return;
    }
    double value = bytes / 1024.0;
    const size_t u = PickUnit(value, kRateUnits);
    // 最多 3 位：个位数带一位小数（2.4 KB/s），其余取整
    _snwprintf_s(number, size, _TRUNCATE, value < 9.95 ? L"%.1f" : L"%.0f", value);
    unit = kRateUnits[u];
}

void FormatNetRateParts(std::optional<double> bytes_per_sec, config::NetUnit unit, wchar_t* number, size_t size,
                        const wchar_t*& unit_text) {
    unit_text = L"";
    if (size == 0) {
        return;
    }
    if (unit == config::NetUnit::Auto || !Usable(bytes_per_sec)) {
        FormatRateParts(bytes_per_sec, number, size, unit_text);
        return;
    }
    const double bytes = std::max(0.0, *bytes_per_sec);
    switch (unit) {
    case config::NetUnit::Bits: {
        // 网卡和运营商都按 1000 进制标速率，这里也用 1000
        double value = bytes * 8.0 / 1000.0;
        size_t u = 0;
        while (value >= 999.5 && u + 1 < std::size(kAllBitRateUnits)) {
            value /= 1000.0;
            ++u;
        }
        if (bytes * 8.0 < 1000.0) {
            value = 0.0;
        }
        _snwprintf_s(number, size, _TRUNCATE, value > 0.0 && value < 9.95 ? L"%.1f" : L"%.0f", value);
        unit_text = kAllBitRateUnits[u];
        return;
    }
    case config::NetUnit::Kilo: {
        const double value = bytes / 1024.0;
        _snwprintf_s(number, size, _TRUNCATE, value > 0.0 && value < 9.95 ? L"%.1f" : L"%.0f",
                     std::min(value, 99999.0));
        unit_text = kRateUnits[0];
        return;
    }
    case config::NetUnit::Mega: {
        const double value = bytes / (1024.0 * 1024.0);
        _snwprintf_s(number, size, _TRUNCATE, value < 9.995 ? L"%.2f" : value < 99.95 ? L"%.1f" : L"%.0f",
                     std::min(value, 99999.0));
        unit_text = kRateUnits[1];
        return;
    }
    case config::NetUnit::Auto:
        break;
    }
}

void FormatNetRateShort(std::optional<double> bytes_per_sec, config::NetUnit unit, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (!Usable(bytes_per_sec)) {
        WriteUnavailable(out, size);
        return;
    }
    if (unit == config::NetUnit::Auto) {
        FormatRateShort(bytes_per_sec, out, size);
        return;
    }
    wchar_t number[16];
    const wchar_t* unit_text = L"";
    FormatNetRateParts(bytes_per_sec, unit, number, unit_text);
    // 单位只留第一个字母，比特再加个 b：“19Mb”“2400K”“2.40M”
    const wchar_t letter = unit_text[0];
    _snwprintf_s(out, size, _TRUNCATE, unit == config::NetUnit::Bits ? L"%s%cb" : L"%s%c", number, letter);
}

void FormatRate(std::optional<double> bytes_per_sec, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    wchar_t number[16];
    const wchar_t* unit = L"";
    FormatRateParts(bytes_per_sec, number, unit);
    if (unit[0]) {
        _snwprintf_s(out, size, _TRUNCATE, L"%s %s", number, unit);
    } else {
        wcsncpy_s(out, size, number, _TRUNCATE);
    }
}

void FormatNetworkRate(std::optional<double> bytes_per_sec, NetworkDirection direction, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    wchar_t rate[24];
    if (!Usable(bytes_per_sec)) {
        WriteUnavailable(rate, std::size(rate));
    } else {
        const double bytes = std::max(0.0, *bytes_per_sec);
        if (bytes < 1024.0) {
            _snwprintf_s(rate, _TRUNCATE, L"%.0f B/s", bytes);
        } else {
            FormatRate(bytes, rate);
        }
    }
    _snwprintf_s(out, size, _TRUNCATE,
                 direction == NetworkDirection::Download ? Str().kDownloadLine : Str().kUploadLine, rate);
}

void FormatRateDetailed(std::optional<double> bytes_per_sec, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (!Usable(bytes_per_sec)) {
        WriteUnavailable(out, size);
        return;
    }
    const double bytes = std::max(0.0, *bytes_per_sec);
    if (bytes == 0.0) {
        _snwprintf_s(out, size, _TRUNCATE, L"0 %s", kRateUnits[0]);
        return;
    }
    // 最小单位 KB/s（D-036），不到 1 KB/s 时是 “0.50 KB/s” 这样的小数。
    // 保留 3 位有效数字；边界用四舍五入后的位数判断，避免 9.996 显示成 "10.00"
    double value = bytes / 1024.0;
    const size_t unit = PickUnit(value, kRateUnits);
    const int decimals = value < 9.995 ? 2 : value < 99.95 ? 1 : 0;
    _snwprintf_s(out, size, _TRUNCATE, L"%.*f %s", decimals, value, kRateUnits[unit]);
}

void FormatBytesDetailed(std::optional<double> bytes, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (!Usable(bytes)) {
        WriteUnavailable(out, size);
        return;
    }
    double value = std::max(0.0, *bytes);
    const size_t unit = PickUnit(value, kByteUnits);
    const int decimals = unit == 0 ? 0 : value < 9.995 ? 2 : value < 99.95 ? 1 : 0;
    _snwprintf_s(out, size, _TRUNCATE, L"%.*f %s", decimals, value, kByteUnits[unit]);
}

void FormatRateShort(std::optional<double> bytes_per_sec, wchar_t* out, size_t size) {
    static constexpr const wchar_t* kShortUnits[] = {L"B", L"K", L"M", L"G", L"T"};
    if (size == 0) {
        return;
    }
    if (!Usable(bytes_per_sec)) {
        WriteUnavailable(out, size);
        return;
    }
    double value = std::max(0.0, *bytes_per_sec);
    const size_t unit = PickUnit(value, kShortUnits);
    if (unit > 0 && value < 9.95) {
        _snwprintf_s(out, size, _TRUNCATE, L"%.1f%s", value, kShortUnits[unit]);
    } else {
        _snwprintf_s(out, size, _TRUNCATE, L"%.0f%s", value, kShortUnits[unit]);
    }
}

namespace {
bool g_fahrenheit = false;
}  // namespace

void SetFahrenheit(bool fahrenheit) { g_fahrenheit = fahrenheit; }

bool UsesFahrenheit() { return g_fahrenheit; }

double DisplayTemperature(double celsius) { return g_fahrenheit ? celsius * 9.0 / 5.0 + 32.0 : celsius; }

void FormatTemperature(double celsius, int decimals, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (!std::isfinite(celsius)) {
        WriteUnavailable(out, size);
        return;
    }
    _snwprintf_s(out, size, _TRUNCATE, L"%.*f %s", decimals, DisplayTemperature(celsius),
                 g_fahrenheit ? L"°F" : L"°C");
}

void FormatUptime(unsigned long long milliseconds, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    const unsigned long long minutes = milliseconds / 60000;
    const unsigned long long hours = minutes / 60;
    const unsigned long long days = hours / 24;
    if (days > 0) {
        _snwprintf_s(out, size, _TRUNCATE, Str().kUptimeDays, days, hours % 24);
    } else if (hours > 0) {
        _snwprintf_s(out, size, _TRUNCATE, Str().kUptimeHours, hours, minutes % 60);
    } else {
        _snwprintf_s(out, size, _TRUNCATE, Str().kUptimeMinutes, minutes);
    }
}

void FormatBytes(std::optional<double> bytes, wchar_t* out, size_t size) {
    if (size == 0) {
        return;
    }
    if (!Usable(bytes)) {
        WriteUnavailable(out, size);
        return;
    }
    double value = std::max(0.0, *bytes);
    const size_t unit = PickUnit(value, kByteUnits);
    if (unit == 0) {
        _snwprintf_s(out, size, _TRUNCATE, L"%.0f %s", value, kByteUnits[unit]);
    } else {
        _snwprintf_s(out, size, _TRUNCATE, L"%.1f %s", value, kByteUnits[unit]);
    }
}

}  // namespace pitot::ui
