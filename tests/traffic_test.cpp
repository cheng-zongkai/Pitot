// 今日 / 本月流量（D-041）

#include "sampler/traffic.h"

#include <windows.h>

#include <cstring>
#include <memory>
#include <string>

#include "test.h"

namespace {

using namespace pitot::sampler;

constexpr GUID kWlan = {0x43c3fd6d, 0xc565, 0x4b16, {0xb1, 0xc6, 0x7b, 0xe5, 0x1a, 0x72, 0x7e, 0x8a}};

}  // namespace

TEST_CASE("traffic: dates move across months, years and leap days") {
    CHECK(AddDays(MakeDate(2026, 11, 4), 1) == 20261105u);
    CHECK(AddDays(MakeDate(2026, 11, 30), 1) == 20261201u);
    CHECK(AddDays(MakeDate(2026, 12, 31), 1) == 20270101u);
    CHECK(AddDays(MakeDate(2028, 2, 28), 1) == 20280229u);
    CHECK(AddDays(MakeDate(2027, 2, 28), 1) == 20270301u);
    CHECK(AddDays(MakeDate(2026, 1, 1), -1) == 20251231u);
    CHECK(AddDays(MakeDate(2026, 11, 4), -61) == 20260904u);
}

TEST_CASE("traffic: period starts on the reset day") {
    CHECK(PeriodStart(20261104, 1) == 20261101u);
    CHECK(PeriodStart(20261104, 4) == 20261104u);
    CHECK(PeriodStart(20261104, 15) == 20261015u);
    CHECK(PeriodStart(20260110, 15) == 20251215u);
}

TEST_CASE("traffic: add and sum by day and adapter") {
    auto book = std::make_unique<TrafficBook>();
    CHECK(!book->dirty());
    book->Add(20261103, kAutoTrafficKey, 100, 10);
    book->Add(20261104, kAutoTrafficKey, 200, 20);
    book->Add(20261104, kAutoTrafficKey, 300, 30);
    book->Add(20261104, kWlan, 5, 6);
    book->Add(20261104, kWlan, 0, 0);  // 没流量不建记录
    CHECK(book->count() == 3);
    CHECK(book->dirty());
    const TrafficTotals today = book->Sum(kAutoTrafficKey, 20261104, 20261104);
    CHECK(today.rx == 500u && today.tx == 50u);
    const TrafficTotals month = book->Sum(kAutoTrafficKey, PeriodStart(20261104, 1), 20261104);
    CHECK(month.rx == 600u && month.tx == 60u);
    CHECK(book->Sum(kWlan, 20261101, 20261104).tx == 6u);
}

TEST_CASE("traffic: prune and full table drop the oldest day") {
    auto book = std::make_unique<TrafficBook>();
    for (int i = 0; i < TrafficBook::kMaxRecords; ++i) {
        GUID key{};
        key.Data1 = static_cast<unsigned long>(i % 8 + 1);
        CHECK(book->Add(AddDays(20260101, i / 8), key, 1, 1));
    }
    CHECK(book->count() == TrafficBook::kMaxRecords);
    // 满了：最老一天的 8 条让位
    CHECK(book->Add(20270101, kAutoTrafficKey, 1, 1));
    CHECK(book->count() == TrafficBook::kMaxRecords - 8 + 1);
    CHECK(book->Sum(GUID{1}, 20260101, 20260101).rx == 0u);
    book->Prune(20270101);
    CHECK(book->count() == 1);
}

TEST_CASE("traffic: text round-trip skips bad lines") {
    auto book = std::make_unique<TrafficBook>();
    book->Add(20261104, kAutoTrafficKey, 6800000000ull, 412000000ull);
    book->Add(20261104, kWlan, 1, 2);
    std::string text = "# header\r\n";
    book->ForEachLine([&](const char* line) {
        text += line;
        text += "\r\n";
        return true;
    });
    text += "garbage line\r\n20261104 {not-a-guid} 1 2\r\n2026 auto 1\r\n";
    CHECK(text.find("20261104 auto 6800000000 412000000") != std::string::npos);
    CHECK(text.find("{43C3FD6D-C565-4B16-B1C6-7BE51A727E8A}") != std::string::npos);

    auto back = std::make_unique<TrafficBook>();
    back->Parse(text.data(), text.size());
    CHECK(back->count() == 2);
    CHECK(!back->dirty());
    CHECK(back->Sum(kAutoTrafficKey, 20261104, 20261104).rx == 6800000000ull);
    CHECK(back->Sum(kWlan, 20261104, 20261104).tx == 2u);
}

TEST_CASE("traffic: save and load a file") {
    wchar_t dir[MAX_PATH];
    wchar_t path[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    GetTempFileNameW(dir, L"ptr", 0, path);
    auto book = std::make_unique<TrafficBook>();
    book->Add(20261104, kWlan, 123, 456);
    CHECK(book->Save(path));
    CHECK(!book->dirty());
    auto back = std::make_unique<TrafficBook>();
    CHECK(back->Load(path));
    CHECK(back->Sum(kWlan, 20261104, 20261104).tx == 456u);
    DeleteFileW(path);
    CHECK(!back->Load(path));
    CHECK(back->count() == 0);
}
