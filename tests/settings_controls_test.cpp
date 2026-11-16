#include "test.h"
#include "ui/settings_controls.h"

TEST_CASE("settings slider: endpoints, rounding and out-of-track pointer") {
    using pitot::ui::SliderValueAt;
    CHECK(SliderValueAt(-50, 100, 200, 6, 32) == 6);
    CHECK(SliderValueAt(100, 100, 200, 6, 32) == 6);
    CHECK(SliderValueAt(150, 100, 200, 6, 32) == 19);
    CHECK(SliderValueAt(200, 100, 200, 6, 32) == 32);
    CHECK(SliderValueAt(300, 100, 200, 6, 32) == 32);
    CHECK(SliderValueAt(125, 100, 200, 30, 120) == 53);
    CHECK(SliderValueAt(100, 100, 100, 6, 32) == 6);
}

TEST_CASE("settings list: all options reachable in a limited viewport") {
    using pitot::ui::ClampFirstOption;
    using pitot::ui::ShowOption;
    CHECK(ClampFirstOption(-3, 31, 8) == 0);
    CHECK(ClampFirstOption(100, 31, 8) == 23);
    CHECK(ShowOption(0, 30, 31, 8) == 23);
    CHECK(ShowOption(23, 0, 31, 8) == 0);
    CHECK(ShowOption(4, 8, 31, 8) == 4);
    CHECK(ShowOption(0, 4, 5, 8) == 0);
    CHECK(ShowOption(0, 0, 0, 0) == 0);
    for (int selected = 0; selected < 500; ++selected) {
        const int first = ShowOption(0, selected, 500, 8);
        CHECK(first <= selected);
        CHECK(selected < first + 8);
        CHECK(first >= 0 && first <= 492);
    }
}
