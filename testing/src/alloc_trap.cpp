#include "vox/testing/alloc_trap.hpp"

#include <cstdlib>
#include <new>

namespace {

// Per-thread counters must be mutable globals: the replacement operator new
// has no other place to keep state.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
thread_local bool tArmed = false;
thread_local std::size_t tAllocations = 0;
thread_local std::size_t tDeallocations = 0;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

#if !defined(VOX_TESTING_NO_ALLOCATION_TRAP)
namespace {

void* allocateOrThrow(std::size_t size) {
    if (tArmed) {
        ++tAllocations;
    }
    if (size == 0) {
        size = 1;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc)
    void* p = std::malloc(size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void* allocateAlignedOrThrow(std::size_t size, std::align_val_t alignment) {
    if (tArmed) {
        ++tAllocations;
    }
    const auto align = static_cast<std::size_t>(alignment);
    const std::size_t rounded = ((size == 0 ? 1 : size) + align - 1) / align * align;
#if defined(_MSC_VER)
    void* p = _aligned_malloc(rounded, align);
#else
    void* p = std::aligned_alloc(align, rounded);
#endif
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void release(void* p) noexcept {
    if (p == nullptr) {
        return;
    }
    if (tArmed) {
        ++tDeallocations;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc)
    std::free(p);
}

void releaseAligned(void* p) noexcept {
    if (p == nullptr) {
        return;
    }
    if (tArmed) {
        ++tDeallocations;
    }
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    // NOLINTNEXTLINE(cppcoreguidelines-no-malloc,hicpp-no-malloc)
    std::free(p);
#endif
}

} // namespace
#endif // VOX_TESTING_NO_ALLOCATION_TRAP

namespace vox::testing {

AllocationTrap::AllocationTrap() noexcept
    : startAllocations_(tAllocations)
    , startDeallocations_(tDeallocations)
    , wasArmed_(tArmed) {
    tArmed = true;
}

AllocationTrap::~AllocationTrap() {
    tArmed = wasArmed_;
}

bool AllocationTrap::available() noexcept {
#if defined(VOX_TESTING_NO_ALLOCATION_TRAP)
    return false;
#else
    return true;
#endif
}

std::size_t AllocationTrap::allocations() const noexcept {
    return tAllocations - startAllocations_;
}

std::size_t AllocationTrap::deallocations() const noexcept {
    return tDeallocations - startDeallocations_;
}

} // namespace vox::testing

#if !defined(VOX_TESTING_NO_ALLOCATION_TRAP)
// Replacement allocation functions ([replacement.functions]).
// NOLINTBEGIN(misc-new-delete-overloads,cert-dcl54-cpp,hicpp-new-delete-operators)
void* operator new(std::size_t size) {
    return allocateOrThrow(size);
}
void* operator new[](std::size_t size) {
    return allocateOrThrow(size);
}
void* operator new(std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    try {
        return allocateOrThrow(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t& /*tag*/) noexcept {
    try {
        return allocateOrThrow(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAlignedOrThrow(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAlignedOrThrow(size, alignment);
}
void operator delete(void* p) noexcept {
    release(p);
}
void operator delete[](void* p) noexcept {
    release(p);
}
void operator delete(void* p, std::size_t /*size*/) noexcept {
    release(p);
}
void operator delete[](void* p, std::size_t /*size*/) noexcept {
    release(p);
}
void operator delete(void* p, std::align_val_t /*alignment*/) noexcept {
    releaseAligned(p);
}
void operator delete[](void* p, std::align_val_t /*alignment*/) noexcept {
    releaseAligned(p);
}
void operator delete(void* p, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
    releaseAligned(p);
}
void operator delete[](void* p, std::size_t /*size*/, std::align_val_t /*alignment*/) noexcept {
    releaseAligned(p);
}
// NOLINTEND(misc-new-delete-overloads,cert-dcl54-cpp,hicpp-new-delete-operators)
#endif // VOX_TESTING_NO_ALLOCATION_TRAP
