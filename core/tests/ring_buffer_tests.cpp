#include <vox/core/ring_buffer.hpp>
#include <vox/testing/alloc_trap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <numeric>
#include <thread>
#include <vector>

TEST_CASE("Ring buffer wraps around and reports availability", "[core][ring]") {
    vox::SpscRingBuffer<float> rb(5);
    REQUIRE(rb.capacity() == 8);
    const std::vector<float> a{1, 2, 3, 4, 5, 6};
    CHECK(rb.write(a) == 6);
    CHECK(rb.availableToRead() == 6);
    CHECK(rb.availableToWrite() == 2);
    std::vector<float> out(4);
    CHECK(rb.read(out) == 4);
    CHECK(out == std::vector<float>{1, 2, 3, 4});
    const std::vector<float> b{7, 8, 9, 10, 11, 12, 13};
    CHECK(rb.write(b) == 6); // only 6 slots free
    std::vector<float> all(8);
    CHECK(rb.read(all) == 8);
    CHECK(all == std::vector<float>{5, 6, 7, 8, 9, 10, 11, 12});
    CHECK(rb.discard(3) == 0);
}

TEST_CASE("Ring buffer read and write do not allocate", "[core][ring][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    vox::SpscRingBuffer<float> rb(1024);
    std::vector<float> block(128, 0.5F);
    const vox::testing::AllocationTrap trap;
    for (int i = 0; i < 100; ++i) {
        static_cast<void>(rb.write(block));
        static_cast<void>(rb.read(block));
    }
    CHECK(trap.allocations() == 0);
}

TEST_CASE("Ring buffer streams samples between threads without loss", "[core][ring][threads]") {
    vox::SpscRingBuffer<float> rb(256);
    constexpr int kTotal = 200'000;
    std::thread producer([&rb] {
        std::vector<float> chunk(37);
        int next = 0;
        while (next < kTotal) {
            const int n = std::min<int>(37, kTotal - next);
            for (int i = 0; i < n; ++i) {
                chunk[static_cast<std::size_t>(i)] = static_cast<float>(next + i);
            }
            const std::size_t written =
                rb.write(std::span<const float>(chunk).first(static_cast<std::size_t>(n)));
            next += static_cast<int>(written);
        }
    });
    std::vector<float> chunk(53);
    int expected = 0;
    bool ok = true;
    while (expected < kTotal) {
        const std::size_t got = rb.read(chunk);
        for (std::size_t i = 0; i < got; ++i) {
            ok = ok && chunk[i] == static_cast<float>(expected);
            ++expected;
        }
    }
    producer.join();
    CHECK(ok);
}
