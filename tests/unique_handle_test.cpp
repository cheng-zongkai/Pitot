#include "common/unique_handle.h"

#include <utility>

#include "test.h"

namespace {

bool is_open(HANDLE h) {
    DWORD flags = 0;
    return GetHandleInformation(h, &flags) != FALSE;
}

HANDLE make_event() { return CreateEventW(nullptr, TRUE, FALSE, nullptr); }

}  // namespace

TEST_CASE("unique_handle: default is empty") {
    pitot::unique_handle h;
    CHECK(!h);
    CHECK(h.get() == nullptr);
}

TEST_CASE("unique_handle: closes on destruction") {
    HANDLE raw = nullptr;
    {
        pitot::unique_handle h(make_event());
        raw = h.get();
        CHECK(static_cast<bool>(h));
        CHECK(is_open(raw));
    }
    CHECK(!is_open(raw));
}

TEST_CASE("unique_handle: move transfers ownership") {
    pitot::unique_handle a(make_event());
    HANDLE raw = a.get();
    pitot::unique_handle b(std::move(a));
    CHECK(!a);  // NOLINT(bugprone-use-after-move)
    CHECK(b.get() == raw);

    pitot::unique_handle c;
    c = std::move(b);
    CHECK(!b);  // NOLINT(bugprone-use-after-move)
    CHECK(c.get() == raw);
    CHECK(is_open(raw));
}

TEST_CASE("unique_handle: reset closes the old handle") {
    pitot::unique_handle h(make_event());
    HANDLE first = h.get();
    h.reset(make_event());
    CHECK(!is_open(first));
    CHECK(is_open(h.get()));
}

TEST_CASE("unique_handle: release gives up ownership") {
    pitot::unique_handle h(make_event());
    HANDLE raw = h.release();
    CHECK(!h);
    CHECK(is_open(raw));
    CloseHandle(raw);
}

TEST_CASE("unique_file_handle: INVALID_HANDLE_VALUE is empty") {
    // 用不存在的设备名，立即失败；不存在的盘符会触发网络查找，很慢
    pitot::unique_file_handle h(CreateFileW(L"\\\\.\\PitotDoesNotExist", GENERIC_READ, 0, nullptr,
                                            OPEN_EXISTING, 0, nullptr));
    CHECK(!h);
    CHECK(h.get() == INVALID_HANDLE_VALUE);
}
