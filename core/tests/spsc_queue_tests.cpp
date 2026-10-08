#include <vox/core/spsc_queue.hpp>
#include <vox/testing/alloc_trap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <thread>

TEST_CASE("SpscQueue preserves order and reports full and empty", "[core][spsc]") {
    vox::SpscQueue<int, 8> q;
    CHECK(vox::SpscQueue<int, 8>::capacity() == 7);
    for (int i = 0; i < 7; ++i) {
        REQUIRE(q.tryPush(i));
    }
    CHECK_FALSE(q.tryPush(99));
    for (int i = 0; i < 7; ++i) {
        const auto v = q.tryPop();
        REQUIRE(v.has_value());
        CHECK(v == i);
    }
    CHECK_FALSE(q.tryPop().has_value());
}

TEST_CASE("SpscQueue push and pop do not allocate", "[core][spsc][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    vox::SpscQueue<std::uint64_t, 64> q;
    const vox::testing::AllocationTrap trap;
    for (std::uint64_t i = 0; i < 1000; ++i) {
        static_cast<void>(q.tryPush(i));
        static_cast<void>(q.tryPop());
    }
    CHECK(trap.allocations() == 0);
    CHECK(trap.deallocations() == 0);
}

TEST_CASE("SpscQueue transfers a million items between threads in order", "[core][spsc][threads]") {
    vox::SpscQueue<std::uint64_t, 1024> q;
    constexpr std::uint64_t kCount = 1'000'000;
    std::thread producer([&q] {
        for (std::uint64_t i = 0; i < kCount;) {
            if (q.tryPush(i)) {
                ++i;
            }
        }
    });
    std::uint64_t expected = 0;
    bool inOrder = true;
    while (expected < kCount) {
        if (const auto v = q.tryPop()) {
            inOrder = inOrder && (*v == expected);
            ++expected;
        }
    }
    producer.join();
    CHECK(inOrder);
}
