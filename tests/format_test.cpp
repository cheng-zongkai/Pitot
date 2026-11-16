#include "ui/format.h"

#include <cmath>
#include <cwchar>
#include <iterator>
#include <limits>

#include "test.h"

namespace {

using pitot::ui::FormatPercent;
using pitot::ui::FormatRate;

constexpr double kKB = 1024.0;
constexpr double kMB = 1024.0 * 1024.0;

bool Percent(std::optional<double> v, const wchar_t* expected) {
    wchar_t buf[16];
    FormatPercent(v, buf);
    return wcscmp(buf, expected) == 0;
}

bool Rate(std::optional<double> v, const wchar_t* expected) {
    wchar_t buf[24];
    FormatRate(v, buf);
    return wcscmp(buf, expected) == 0;
}

}  // namespace

TEST_CASE("format: percent rounds to integer") {
    CHECK(Percent(23.4, L"23%"));
    CHECK(Percent(0.0, L"0%"));
    CHECK(Percent(99.6, L"100%"));
}

TEST_CASE("format: percent clamps to 0-100") {
    CHECK(Percent(150.0, L"100%"));
    CHECK(Percent(-3.0, L"0%"));
}

TEST_CASE("format: percent unavailable") {
    CHECK(Percent(std::nullopt, L"—"));
    CHECK(Percent(std::numeric_limits<double>::quiet_NaN(), L"—"));
}

TEST_CASE("format: rate below 1 KB/s is 0 KB/s, never B/s") {
    CHECK(Rate(0.0, L"0 KB/s"));
    CHECK(Rate(512.0, L"0 KB/s"));
    CHECK(Rate(1000.0, L"0 KB/s"));  // 0.98 KB：不到 1 KB 都记 0
    CHECK(Rate(1023.0, L"0 KB/s"));
    CHECK(Rate(-5.0, L"0 KB/s"));
    CHECK(Rate(1024.0, L"1.0 KB/s"));
}

TEST_CASE("format: rate switches unit before 4 digits") {
    CHECK(Rate(999.4 * kKB, L"999 KB/s"));
    CHECK(Rate(999.6 * kKB, L"1.0 MB/s"));
}

TEST_CASE("format: rate uses one decimal below 10") {
    CHECK(Rate(2.4 * kKB, L"2.4 KB/s"));
    CHECK(Rate(9.94 * kKB, L"9.9 KB/s"));
    CHECK(Rate(9.96 * kKB, L"10 KB/s"));
    CHECK(Rate(180.0 * kKB, L"180 KB/s"));
    CHECK(Rate(2.4 * kMB, L"2.4 MB/s"));
}

TEST_CASE("format: rate unavailable") {
    CHECK(Rate(std::nullopt, L"—"));
    CHECK(Rate(std::numeric_limits<double>::infinity(), L"—"));
}

TEST_CASE("format: flyout network rates show bytes and direction") {
    using pitot::ui::FormatNetworkRate;
    using pitot::ui::NetworkDirection;
    wchar_t out[24];
    FormatNetworkRate(0.0, NetworkDirection::Download, out);
    CHECK(wcscmp(out, L"↓ 0 B/s") == 0);
    FormatNetworkRate(0.6, NetworkDirection::Download, out);
    CHECK(wcscmp(out, L"↓ 1 B/s") == 0);
    FormatNetworkRate(512.0, NetworkDirection::Upload, out);
    CHECK(wcscmp(out, L"↑ 512 B/s") == 0);
    FormatNetworkRate(1023.0, NetworkDirection::Download, out);
    CHECK(wcscmp(out, L"↓ 1023 B/s") == 0);
    FormatNetworkRate(1024.0, NetworkDirection::Upload, out);
    CHECK(wcscmp(out, L"↑ 1.0 KB/s") == 0);
    FormatNetworkRate(2.4 * kMB, NetworkDirection::Download, out);
    CHECK(wcscmp(out, L"↓ 2.4 MB/s") == 0);
    FormatNetworkRate(-5.0, NetworkDirection::Upload, out);
    CHECK(wcscmp(out, L"↑ 0 B/s") == 0);
    FormatNetworkRate(std::nullopt, NetworkDirection::Download, out);
    CHECK(wcscmp(out, L"↓ —") == 0);
    FormatNetworkRate(std::numeric_limits<double>::infinity(), NetworkDirection::Upload, out);
    CHECK(wcscmp(out, L"↑ —") == 0);
    wchar_t small[4];
    FormatNetworkRate(512.0, NetworkDirection::Download, small);
    CHECK(wcslen(small) < std::size(small));
}

TEST_CASE("format: detailed percent keeps one decimal") {
    wchar_t buf[16];
    pitot::ui::FormatPercentDetailed(23.44, buf);
    CHECK(wcscmp(buf, L"23.4%") == 0);
    pitot::ui::FormatPercentDetailed(120.0, buf);
    CHECK(wcscmp(buf, L"100.0%") == 0);
    pitot::ui::FormatPercentDetailed(std::nullopt, buf);
    CHECK(wcscmp(buf, L"—") == 0);
}

TEST_CASE("format: detailed rate keeps 3 significant digits") {
    wchar_t buf[24];
    pitot::ui::FormatRateDetailed(512.0, buf);
    CHECK(wcscmp(buf, L"0.50 KB/s") == 0);  // 不用 B/s
    pitot::ui::FormatRateDetailed(0.0, buf);
    CHECK(wcscmp(buf, L"0 KB/s") == 0);
    pitot::ui::FormatRateDetailed(6.712 * kKB, buf);
    CHECK(wcscmp(buf, L"6.71 KB/s") == 0);
    pitot::ui::FormatRateDetailed(9.996 * kKB, buf);
    CHECK(wcscmp(buf, L"10.0 KB/s") == 0);
    pitot::ui::FormatRateDetailed(18.04 * kKB, buf);
    CHECK(wcscmp(buf, L"18.0 KB/s") == 0);
    pitot::ui::FormatRateDetailed(180.4 * kKB, buf);
    CHECK(wcscmp(buf, L"180 KB/s") == 0);
    pitot::ui::FormatRateDetailed(2.4 * kMB, buf);
    CHECK(wcscmp(buf, L"2.40 MB/s") == 0);
}

TEST_CASE("format: bytes") {
    wchar_t buf[24];
    pitot::ui::FormatBytes(15.9 * 1024.0 * kMB, buf);
    CHECK(wcscmp(buf, L"15.9 GB") == 0);
    pitot::ui::FormatBytes(800.0, buf);
    CHECK(wcscmp(buf, L"800 B") == 0);
    pitot::ui::FormatBytes(std::nullopt, buf);
    CHECK(wcscmp(buf, L"—") == 0);
}

TEST_CASE("format: small buffer truncates safely") {
    wchar_t buf[4];
    FormatRate(180.0 * kKB, buf);
    CHECK(wcslen(buf) < 4);
    FormatPercent(100.0, buf, 2);
    CHECK(wcslen(buf) < 2);
}

TEST_CASE("format: bytes with 3 significant digits") {
    auto check = [](double v, const wchar_t* expected) {
        wchar_t buf[24];
        pitot::ui::FormatBytesDetailed(v, buf);
        return wcscmp(buf, expected) == 0;
    };
    CHECK(check(512.0, L"512 B"));
    CHECK(check(53.2 * kMB, L"53.2 MB"));
    CHECK(check(812.0 * kMB, L"812 MB"));
    CHECK(check(3.84 * 1024 * kMB, L"3.84 GB"));
    CHECK(check(999.6 * kMB, L"0.98 GB"));
}

TEST_CASE("format: rate split into number and unit") {
    wchar_t number[16];
    const wchar_t* unit = nullptr;
    pitot::ui::FormatRateParts(180.0 * 1024, number, unit);
    CHECK(wcscmp(number, L"180") == 0 && wcscmp(unit, L"KB/s") == 0);
    pitot::ui::FormatRateParts(2.4 * 1024 * 1024, number, unit);
    CHECK(wcscmp(number, L"2.4") == 0 && wcscmp(unit, L"MB/s") == 0);
    pitot::ui::FormatRateParts(512.0, number, unit);  // 不到 1 KB/s
    CHECK(wcscmp(number, L"0") == 0 && wcscmp(unit, L"KB/s") == 0);
    pitot::ui::FormatRateParts(std::nullopt, number, unit);
    CHECK(wcscmp(number, L"—") == 0 && unit[0] == L'\0');
    // 拼起来和 FormatRate 一样
    wchar_t whole[24];
    pitot::ui::FormatRate(2.4 * 1024 * 1024, whole);
    CHECK(wcscmp(whole, L"2.4 MB/s") == 0);
    pitot::ui::FormatRate(std::nullopt, whole);
    CHECK(wcscmp(whole, L"—") == 0);
}

TEST_CASE("format: network speed units for the strip") {
    using pitot::config::NetUnit;
    wchar_t number[16];
    const wchar_t* unit = L"";
    // 1 MB/s = 8.39 Mbps
    pitot::ui::FormatNetRateParts(1024.0 * 1024.0, NetUnit::Bits, number, unit);
    CHECK(wcscmp(number, L"8.4") == 0);
    CHECK(wcscmp(unit, L"Mbps") == 0);
    pitot::ui::FormatNetRateParts(100.0, NetUnit::Bits, number, unit);
    CHECK(wcscmp(number, L"0") == 0);
    CHECK(wcscmp(unit, L"Kbps") == 0);
    pitot::ui::FormatNetRateParts(3.0 * 1024 * 1024, NetUnit::Kilo, number, unit);
    CHECK(wcscmp(number, L"3072") == 0);
    CHECK(wcscmp(unit, L"KB/s") == 0);
    pitot::ui::FormatNetRateParts(20.0 * 1024, NetUnit::Mega, number, unit);
    CHECK(wcscmp(number, L"0.02") == 0);
    CHECK(wcscmp(unit, L"MB/s") == 0);
    pitot::ui::FormatNetRateParts(std::nullopt, NetUnit::Mega, number, unit);
    CHECK(unit[0] == L'\0');
    wchar_t text[16];
    pitot::ui::FormatNetRateShort(2.4 * 1024 * 1024, NetUnit::Auto, text);
    CHECK(wcscmp(text, L"2.4M") == 0);
    pitot::ui::FormatNetRateShort(2.5e6, NetUnit::Bits, text);
    CHECK(wcscmp(text, L"20Mb") == 0);
}
