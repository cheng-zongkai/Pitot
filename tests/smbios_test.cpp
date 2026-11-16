#include "sampler/smbios.h"

#include <vector>

#include "test.h"

namespace {

using pitot::sampler::ParseMemorySpeed;

void PutWord(std::vector<uint8_t>& t, size_t at, uint16_t v) {
    t[at] = static_cast<uint8_t>(v);
    t[at + 1] = static_cast<uint8_t>(v >> 8);
}

// 追加一个 Type 17 结构。length 是格式化区长度；strings 为空时用两个 0 结束
void AddMemoryDevice(std::vector<uint8_t>& t, uint8_t length, uint16_t size_mb, uint16_t configured,
                     uint32_t extended = 0, const char* strings = nullptr) {
    const size_t at = t.size();
    t.resize(at + length, 0);
    t[at] = 17;
    t[at + 1] = length;
    PutWord(t, at + 0x0C, size_mb);
    if (length >= 0x22) {
        PutWord(t, at + 0x20, configured);
    }
    if (length >= 0x5C) {
        PutWord(t, at + 0x58, static_cast<uint16_t>(extended));
        PutWord(t, at + 0x5A, static_cast<uint16_t>(extended >> 16));
    }
    if (strings) {
        for (const char* p = strings; *p; ++p) {
            t.push_back(static_cast<uint8_t>(*p));
        }
        t.push_back(0);
        t.push_back(0);
    } else {
        t.push_back(0);
        t.push_back(0);
    }
}

void AddOther(std::vector<uint8_t>& t, uint8_t type) {
    const size_t at = t.size();
    t.resize(at + 8, 0);
    t[at] = type;
    t[at + 1] = 8;
    t.push_back('x');
    t.push_back(0);
    t.push_back(0);
}

void AddEnd(std::vector<uint8_t>& t) {
    const size_t at = t.size();
    t.resize(at + 4, 0);
    t[at] = 127;
    t[at + 1] = 4;
    t.push_back(0);
    t.push_back(0);
}

}  // namespace

TEST_CASE("smbios: configured speed of populated slots") {
    std::vector<uint8_t> t;
    AddOther(t, 0);
    AddMemoryDevice(t, 0x28, 8192, 3200, 0, "DIMM_A1");
    AddMemoryDevice(t, 0x28, 0, 0);  // 空插槽
    AddMemoryDevice(t, 0x28, 8192, 3200, 0, "DIMM_B1");
    AddEnd(t);
    CHECK(ParseMemorySpeed(t) == 3200u);
}

TEST_CASE("smbios: mixed speeds take the slowest, unknown is skipped") {
    std::vector<uint8_t> t;
    AddMemoryDevice(t, 0x28, 8192, 3600);
    AddMemoryDevice(t, 0x28, 8192, 0);  // 未知
    AddMemoryDevice(t, 0x28, 8192, 3200);
    AddEnd(t);
    CHECK(ParseMemorySpeed(t) == 3200u);
}

TEST_CASE("smbios: extended configured speed") {
    std::vector<uint8_t> t;
    AddMemoryDevice(t, 0x5C, 16384, 0xFFFF, 0x80000000u | 70000u);
    AddEnd(t);
    CHECK(ParseMemorySpeed(t) == 70000u);
}

TEST_CASE("smbios: old or empty tables give nothing") {
    std::vector<uint8_t> t;
    AddMemoryDevice(t, 0x1B, 8192, 0);  // 2.6 以前没有 Configured Memory Speed
    AddEnd(t);
    CHECK(!ParseMemorySpeed(t));
    CHECK(!ParseMemorySpeed({}));
    // 长度字段损坏时不越界
    std::vector<uint8_t> bad = {17, 0xF0, 0, 0, 1, 2};
    CHECK(!ParseMemorySpeed(bad));
}
