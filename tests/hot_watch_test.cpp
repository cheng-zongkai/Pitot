// 高温通知的判断（D-079）

#include "test.h"
#include "ui/hot_watch.h"

using pitot::ui::HotWatch;

TEST_CASE("hot watch: notifies once after 10 seconds above the threshold") {
    HotWatch w;
    CHECK(!w.Update(0, 85.0, 80));
    CHECK(!w.Update(9'000, 86.0, 80));
    CHECK(w.Update(10'000, 86.0, 80));
    // 一直热着也不再通知
    CHECK(!w.Update(20'000, 90.0, 80));
    CHECK(!w.Update(2 * HotWatch::kCooldownMs, 90.0, 80));
}

TEST_CASE("hot watch: a short dip resets the 10 seconds but does not rearm") {
    HotWatch w;
    CHECK(!w.Update(0, 85.0, 80));
    CHECK(!w.Update(5'000, 79.0, 80));  // 掉到阈值下，重新计时
    CHECK(!w.Update(6'000, 85.0, 80));
    CHECK(!w.Update(15'000, 85.0, 80));
    CHECK(w.Update(16'000, 85.0, 80));
    // 只降到 79 ℃ 不算真的凉下来
    CHECK(!w.Update(17'000, 79.0, 80));
    CHECK(!w.Update(HotWatch::kCooldownMs + 40'000, 85.0, 80));
}

TEST_CASE("hot watch: rearms after cooling down, with a cooldown between notices") {
    HotWatch w;
    w.Update(0, 85.0, 80);
    CHECK(w.Update(10'000, 85.0, 80));
    CHECK(!w.Update(11'000, 70.0, 80));  // 凉下来，重新武装
    w.Update(12'000, 85.0, 80);
    CHECK(!w.Update(30'000, 85.0, 80));  // 还在冷却期
    CHECK(w.Update(10'000 + HotWatch::kCooldownMs, 85.0, 80));
}

TEST_CASE("hot watch: missing readings do not count") {
    HotWatch w;
    CHECK(!w.Update(0, std::nullopt, 80));
    CHECK(!w.Update(20'000, std::nullopt, 80));
    CHECK(!w.Update(21'000, 81.0, 80));
    CHECK(w.Update(31'000, 81.0, 80));
}
