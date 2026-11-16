#include "sampler/rate.h"

#include "test.h"

using pitot::sampler::CounterDelta;
using pitot::sampler::PerSecond;

TEST_CASE("rate: counter delta") {
    CHECK(CounterDelta(100, 250) == 150);
    CHECK(CounterDelta(100, 100) == 0);
}

TEST_CASE("rate: counter reset counts as zero") {
    CHECK(CounterDelta(1'000'000, 10) == 0);
}

TEST_CASE("rate: per second") {
    CHECK(PerSecond(2048, 1000) == 2048.0);
    CHECK(PerSecond(2048, 2000) == 1024.0);
    CHECK(PerSecond(2048, 0) == 0.0);
}

TEST_CASE("network rate: irregular intervals and byte directions") {
    pitot::sampler::NetworkRate r;
    r.Update(1000, 9000, 100);
    CHECK(!r.rx && !r.tx && r.round_rx == 0);
    r.Update(1350, 9700, 450);
    CHECK(r.rx == 1000.0 && r.tx == 2000.0);
    CHECK(r.round_rx == 350 && r.round_tx == 700);
    r.Update(6350, 19700, 5450);
    CHECK(r.rx == 1000.0 && r.tx == 2000.0);
    r.Update(6350, 19700, 6450);
    CHECK(r.rx == 0.0 && r.tx == 0.0);
}

TEST_CASE("network rate: failed read and reconnect cannot produce a spike") {
    pitot::sampler::NetworkRate r;
    r.Update(0, 0, 1000);
    r.Update(1000, 2000, 2000);
    r.Reset();
    CHECK(!r.rx && !r.tx);
    r.Update(50000, 80000, 9000);
    CHECK(!r.rx && !r.tx && r.round_rx == 0 && r.round_tx == 0);
    r.Update(51000, 82000, 10000);
    CHECK(r.rx == 1000.0 && r.tx == 2000.0);
}

TEST_CASE("network rate: counter rollback and zero duration rebase") {
    pitot::sampler::NetworkRate r;
    r.Update(90000, 50000, 1000);
    r.Update(100, 60000, 2000);
    CHECK(!r.rx && !r.tx && r.round_tx == 0);
    r.Update(1100, 62000, 3000);
    CHECK(r.rx == 1000.0 && r.tx == 2000.0);
    r.Update(1200, 63000, 3000);
    CHECK(!r.rx && !r.tx);
    r.Update(2200, 65000, 4000);
    CHECK(r.rx == 1100.0 && r.tx == 3000.0);
}
