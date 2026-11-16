#include "sampler/triple_buffer.h"

#include <atomic>
#include <cstdint>
#include <thread>

#include "test.h"

using pitot::sampler::TripleBuffer;

TEST_CASE("triple buffer: nothing to acquire before first publish") {
    TripleBuffer<int> buffer;
    CHECK(!buffer.Acquire());
}

TEST_CASE("triple buffer: acquire returns published value once") {
    TripleBuffer<int> buffer;
    buffer.WriteBuffer() = 7;
    buffer.Publish();
    CHECK(buffer.Acquire());
    CHECK(buffer.ReadBuffer() == 7);
    CHECK(!buffer.Acquire());
    CHECK(buffer.ReadBuffer() == 7);
}

TEST_CASE("triple buffer: reader gets latest when writer is ahead") {
    TripleBuffer<int> buffer;
    for (int i = 1; i <= 5; ++i) {
        buffer.WriteBuffer() = i;
        buffer.Publish();
    }
    CHECK(buffer.Acquire());
    CHECK(buffer.ReadBuffer() == 5);
}

TEST_CASE("triple buffer: concurrent reader never sees a torn or older value") {
    // 每份数据的两个字段互为取反；读到不一致就说明读到了写了一半的缓冲
    struct Item {
        uint64_t sequence;
        uint64_t check;
    };
    TripleBuffer<Item> buffer;
    constexpr uint64_t kCount = 200'000;
    std::atomic<bool> done{false};

    std::thread writer([&] {
        for (uint64_t i = 1; i <= kCount; ++i) {
            Item& item = buffer.WriteBuffer();
            item.sequence = i;
            item.check = ~i;
            buffer.Publish();
        }
        done = true;
    });

    bool torn = false;
    bool went_back = false;
    uint64_t last = 0;
    for (;;) {
        // 先读 done 再 Acquire：done 为真时最后一次发布一定已经可见，Acquire 失败就说明全部读完了
        const bool finished = done.load();
        if (buffer.Acquire()) {
            const Item& item = buffer.ReadBuffer();
            torn |= item.check != ~item.sequence;
            went_back |= item.sequence < last;
            last = item.sequence;
        } else if (finished) {
            break;
        }
    }
    writer.join();

    CHECK(!torn);
    CHECK(!went_back);
    CHECK(last == kCount);
}
