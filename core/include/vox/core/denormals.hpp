#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define VOX_DENORMALS_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define VOX_DENORMALS_ARM64 1
#endif

namespace vox {

/// Enables flush-to-zero and denormals-are-zero for the current thread while
/// in scope. Denormal floats in recursive filters can slow processing by two
/// orders of magnitude, which turns into audio dropouts.
class ScopedNoDenormals {
public:
    ScopedNoDenormals() noexcept {
#if defined(VOX_DENORMALS_X86)
        previous_ = _mm_getcsr();
        _mm_setcsr(previous_ | kFtzDaz);
#elif defined(VOX_DENORMALS_ARM64) && !defined(_MSC_VER)
        std::uint64_t fpcr = 0;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        previous_ = fpcr;
        fpcr |= kArmFz;
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    }

    ~ScopedNoDenormals() {
#if defined(VOX_DENORMALS_X86)
        _mm_setcsr(previous_);
#elif defined(VOX_DENORMALS_ARM64) && !defined(_MSC_VER)
        asm volatile("msr fpcr, %0" : : "r"(previous_));
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals(ScopedNoDenormals&&) = delete;
    ScopedNoDenormals& operator=(ScopedNoDenormals&&) = delete;

private:
#if defined(VOX_DENORMALS_X86)
    static constexpr unsigned int kFtzDaz = 0x8040U;
    unsigned int previous_ = 0;
#else
    static constexpr std::uint64_t kArmFz = std::uint64_t{1} << 24U;
    std::uint64_t previous_ = 0;
#endif
};

} // namespace vox
