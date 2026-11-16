// 存储面板的容量条：每块盘的已用 / 总量，以及所有盘各卷容量之和

#include "sampler/snapshot.h"
#include "test.h"

namespace {

using pitot::sampler::DiskReadings;
using pitot::sampler::StorageReadings;

constexpr uint64_t kGB = 1'000'000'000ull;

void SetCapacity(DiskReadings& d, uint64_t total, uint64_t free) {
    d.total_bytes = total;
    d.free_bytes = free;
}

}  // namespace

TEST_CASE("storage: total capacity adds up every disk") {
    StorageReadings s;
    s.count = 2;
    SetCapacity(s.disks[0], 999 * kGB, 528 * kGB);
    SetCapacity(s.disks[1], 2000 * kGB, 1310 * kGB);
    const auto sum = pitot::sampler::TotalStorageCapacity(s);
    CHECK(sum.has_value());
    CHECK(sum->total_bytes == 2999 * kGB);
    CHECK(sum->used_bytes == (999 - 528 + 2000 - 1310) * kGB);
}

TEST_CASE("storage: total capacity skips disks without a reading") {
    StorageReadings s;
    s.count = 4;
    SetCapacity(s.disks[0], 500 * kGB, 100 * kGB);
    // 没有卷的盘、只读到一半的盘、总量为 0 的盘都不参与
    s.disks[1] = {};
    s.disks[2].total_bytes = 300 * kGB;
    SetCapacity(s.disks[3], 0, 0);
    const auto sum = pitot::sampler::TotalStorageCapacity(s);
    CHECK(sum.has_value());
    CHECK(sum->total_bytes == 500 * kGB);
    CHECK(sum->used_bytes == 400 * kGB);
}

TEST_CASE("storage: total capacity is empty without any reading") {
    StorageReadings s;
    CHECK(!pitot::sampler::TotalStorageCapacity(s));
    s.count = 2;
    CHECK(!pitot::sampler::TotalStorageCapacity(s));
    s.disks[1].free_bytes = 5 * kGB;
    CHECK(!pitot::sampler::TotalStorageCapacity(s));
    SetCapacity(s.disks[0], 0, 0);
    CHECK(!pitot::sampler::TotalStorageCapacity(s));
}

TEST_CASE("storage: total capacity ignores slots past the disk count") {
    StorageReadings s;
    s.count = 1;
    SetCapacity(s.disks[0], 256 * kGB, 56 * kGB);
    SetCapacity(s.disks[1], 4000 * kGB, 0);  // 上一轮留下的数据
    const auto sum = pitot::sampler::TotalStorageCapacity(s);
    CHECK(sum.has_value());
    CHECK(sum->total_bytes == 256 * kGB);
    CHECK(sum->used_bytes == 200 * kGB);
}

TEST_CASE("storage: total capacity clamps free space above the total") {
    StorageReadings s;
    s.count = 2;
    SetCapacity(s.disks[0], 100 * kGB, 150 * kGB);
    SetCapacity(s.disks[1], 100 * kGB, 40 * kGB);
    const auto sum = pitot::sampler::TotalStorageCapacity(s);
    CHECK(sum.has_value());
    CHECK(sum->total_bytes == 200 * kGB);
    // 第一块盘按 0 已用算，不能让无符号减法绕回成天文数字
    CHECK(sum->used_bytes == 60 * kGB);
}

TEST_CASE("storage: total capacity fits the largest disk set") {
    StorageReadings s;
    s.count = pitot::sampler::kMaxDisks;
    for (int i = 0; i < s.count; ++i) {
        SetCapacity(s.disks[static_cast<size_t>(i)], 20'000 * kGB, 5'000 * kGB);
    }
    const auto sum = pitot::sampler::TotalStorageCapacity(s);
    CHECK(sum.has_value());
    CHECK(sum->total_bytes == 8 * 20'000 * kGB);
    CHECK(sum->used_bytes == 8 * 15'000 * kGB);
}

TEST_CASE("storage: one disk's capacity is used / total") {
    DiskReadings d;
    CHECK(!pitot::sampler::DiskCapacity(d));
    d.total_bytes = 512 * kGB;
    CHECK(!pitot::sampler::DiskCapacity(d));
    d.free_bytes = 112 * kGB;
    const auto c = pitot::sampler::DiskCapacity(d);
    CHECK(c.has_value());
    CHECK(c->used_bytes == 400 * kGB);
    CHECK(c->total_bytes == 512 * kGB);
    SetCapacity(d, 0, 0);
    CHECK(!pitot::sampler::DiskCapacity(d));
    SetCapacity(d, 100 * kGB, 150 * kGB);
    CHECK(pitot::sampler::DiskCapacity(d)->used_bytes == 0);
}

TEST_CASE("storage: per-disk capacities add up to the total") {
    StorageReadings s;
    s.count = 3;
    SetCapacity(s.disks[0], 512 * kGB, 300 * kGB);
    SetCapacity(s.disks[2], 1000 * kGB, 250 * kGB);
    uint64_t used = 0;
    for (int i = 0; i < s.count; ++i) {
        if (const auto c = pitot::sampler::DiskCapacity(s.disks[static_cast<size_t>(i)])) {
            used += c->used_bytes;
        }
    }
    CHECK(used == pitot::sampler::TotalStorageCapacity(s)->used_bytes);
}
