#pragma once

// D-016：极简测试框架。TEST_CASE 注册用例，CHECK 失败时记录但继续执行，
// 这样一次运行能看到同一用例里的所有失败。

#include <cstdio>
#include <vector>

namespace pitot::test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failure_count() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void report_failure(const char* file, int line, const char* expr) {
    std::printf("    FAIL %s:%d: %s\n", file, line, expr);
    ++failure_count();
}

}  // namespace pitot::test

#define PITOT_TEST_CAT_(a, b) a##b
#define PITOT_TEST_CAT(a, b) PITOT_TEST_CAT_(a, b)

#define TEST_CASE(name)                                                                         \
    static void PITOT_TEST_CAT(pitot_test_fn_, __LINE__)();                                     \
    static const ::pitot::test::Registrar PITOT_TEST_CAT(pitot_test_reg_, __LINE__){            \
        name, &PITOT_TEST_CAT(pitot_test_fn_, __LINE__)};                                       \
    static void PITOT_TEST_CAT(pitot_test_fn_, __LINE__)()

#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) ::pitot::test::report_failure(__FILE__, __LINE__, #expr); \
    } while (0)
