#include <vox/core/object_channel.hpp>
#include <vox/testing/alloc_trap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace {

struct Tracked {
    explicit Tracked(int* counter)
        : counter_(counter) {}
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    Tracked(Tracked&&) = delete;
    Tracked& operator=(Tracked&&) = delete;
    ~Tracked() { ++*counter_; }

private:
    int* counter_;
};

} // namespace

TEST_CASE("ObjectChannel destroys retired objects on the control side only",
          "[core][channel][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    int destroyed = 0;
    vox::ObjectChannel<Tracked, 8> channel;
    REQUIRE(channel.send(std::make_unique<Tracked>(&destroyed)) == nullptr);

    Tracked* adopted = nullptr;
    bool retired = false;
    std::size_t allocations = 0;
    std::size_t deallocations = 0;
    {
        const vox::testing::AllocationTrap trap; // pretend to be the audio thread
        adopted = channel.receive();
        retired = channel.retire(adopted);
        allocations = trap.allocations();
        deallocations = trap.deallocations();
    }
    REQUIRE(adopted != nullptr);
    REQUIRE(retired);
    CHECK(allocations == 0);
    CHECK(deallocations == 0);
    CHECK(destroyed == 0);
    channel.collectGarbage();
    CHECK(destroyed == 1);
}

TEST_CASE("ObjectChannel returns the object when the queue is full", "[core][channel]") {
    int destroyed = 0;
    vox::ObjectChannel<Tracked, 2> channel; // one usable slot
    REQUIRE(channel.send(std::make_unique<Tracked>(&destroyed)) == nullptr);
    auto rejected = channel.send(std::make_unique<Tracked>(&destroyed));
    CHECK(rejected != nullptr);
    rejected.reset();
    CHECK(destroyed == 1);
}
