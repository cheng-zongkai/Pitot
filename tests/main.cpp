#include <cstring>
#include <exception>

#include "test.h"

// 用法：pitot_tests [名称片段]，只跑名称里包含该片段的用例。
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    int failed_cases = 0;

    for (const auto& c : pitot::test::registry()) {
        if (filter && !std::strstr(c.name, filter)) {
            continue;
        }
        ++ran;
        const int before = pitot::test::failure_count();
        try {
            c.fn();
        } catch (const std::exception& e) {
            std::printf("    FAIL uncaught exception: %s\n", e.what());
            ++pitot::test::failure_count();
        } catch (...) {
            std::printf("    FAIL uncaught unknown exception\n");
            ++pitot::test::failure_count();
        }
        const bool ok = pitot::test::failure_count() == before;
        if (!ok) {
            ++failed_cases;
        }
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
    }

    std::printf("\n%d cases, %d failed\n", ran, failed_cases);
    return (failed_cases == 0 && ran > 0) ? 0 : 1;
}
