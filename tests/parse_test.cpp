#include "sensors/parse.h"

#include <cwchar>

#include "test.h"

using namespace pitot::sensors;

TEST_CASE("parse: PhysicalDisk instance names") {
    CHECK(ParseDiskInstance(L"0 c:") == 0u);
    CHECK(ParseDiskInstance(L"1 D: E:") == 1u);
    CHECK(ParseDiskInstance(L"12") == 12u);
    CHECK(!ParseDiskInstance(L"_Total"));
    CHECK(!ParseDiskInstance(L""));
    CHECK(!ParseDiskInstance(nullptr));
    CHECK(!ParseDiskInstance(L"3x"));
}

TEST_CASE("parse: model names are trimmed") {
    wchar_t out[48];
    CleanModelName("  BIWIN NV7400 512GB    ", out, 48);
    CHECK(wcscmp(out, L"BIWIN NV7400 512GB") == 0);
    CleanModelName("aigo   NVMe SSD\tP3500", out, 48);
    CHECK(wcscmp(out, L"aigo NVMe SSD P3500") == 0);
    CleanModelName("", out, 48);
    CHECK(out[0] == L'\0');
    CleanModelName(nullptr, out, 48);
    CHECK(out[0] == L'\0');
    wchar_t tiny[4];
    CleanModelName("ABCDEF", tiny, 4);
    CHECK(wcscmp(tiny, L"ABC") == 0);
}

TEST_CASE("parse: GPU counter instance names") {
    auto g = ParseGpuInstance(L"pid_1234_luid_0x00000000_0x0000E145_phys_0_eng_0_engtype_3D");
    CHECK(g && g->pid == 1234u && g->luid_high == 0u && g->luid_low == 0xE145u);
    g = ParseGpuInstance(L"pid_8_luid_0x00000001_0x0000e145_phys_0");
    CHECK(g && g->pid == 8u && g->luid_high == 1u && g->luid_low == 0xE145u);
    CHECK(!ParseGpuInstance(L"luid_0x00000000_0x0000e145_phys_0"));
    CHECK(!ParseGpuInstance(L"pid__luid_0x0_0x1"));
    CHECK(!ParseGpuInstance(L"pid_12_luid_0x_0x1"));
    CHECK(!ParseGpuInstance(L"pid_12_luid_0x1"));
    CHECK(!ParseGpuInstance(nullptr));
}

TEST_CASE("parse: node utilization") {
    CHECK(NodeUtilization(0, 5'000'000, 10'000'000) == 50.0);
    CHECK(NodeUtilization(100, 100, 10'000'000) == 0.0);
    CHECK(NodeUtilization(0, 20'000'000, 10'000'000) == 100.0);  // 截到 100
    CHECK(!NodeUtilization(10, 5, 10'000'000));                   // 倒退
    CHECK(!NodeUtilization(0, 5, 0));
}
