#pragma once

#include <cstddef>

namespace vox::testing {

/// Counts heap allocations and deallocations made on the current thread while
/// the trap is alive. Real-time code under test must leave both counts at 0.
///
/// Works by replacing the global operator new/delete in the test binaries.
class AllocationTrap {
public:
    AllocationTrap() noexcept;
    ~AllocationTrap();
    AllocationTrap(const AllocationTrap&) = delete;
    AllocationTrap& operator=(const AllocationTrap&) = delete;
    AllocationTrap(AllocationTrap&&) = delete;
    AllocationTrap& operator=(AllocationTrap&&) = delete;

    /// False in ThreadSanitizer builds, whose runtime owns operator new and
    /// delete; allocation tests skip themselves there.
    [[nodiscard]] static bool available() noexcept;

    [[nodiscard]] std::size_t allocations() const noexcept;
    [[nodiscard]] std::size_t deallocations() const noexcept;

private:
    std::size_t startAllocations_;
    std::size_t startDeallocations_;
    bool wasArmed_;
};

} // namespace vox::testing
