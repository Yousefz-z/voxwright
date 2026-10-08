#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace vox {

inline constexpr std::size_t kCacheLineSize = 64;

/// Bounded wait-free single-producer single-consumer queue.
///
/// One thread may call tryPush, one other thread may call tryPop. Neither
/// call allocates, locks, or blocks, so both sides are safe on a real-time
/// audio thread. Capacity must be a power of two; one slot stays empty.
template <class T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "SpscQueue capacity must be a power of two");
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>,
                  "SpscQueue elements must be nothrow movable");

public:
    SpscQueue() = default;
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;
    ~SpscQueue() = default;

    /// Producer side. Returns false when full (an rvalue argument is then left
    /// unmoved, so the caller still owns it).
    bool tryPush(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        T copy = value;
        return emplace(std::move(copy));
    }
    bool tryPush(T&& value) noexcept { return emplace(std::move(value)); }

    /// Consumer side. Returns std::nullopt when empty.
    std::optional<T> tryPop() noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == cachedHead_) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (tail == cachedHead_) {
                return std::nullopt;
            }
        }
        std::optional<T> out{std::move(slots_[tail & kMask])};
        tail_.store(tail + 1, std::memory_order_release);
        return out;
    }

    /// Approximate number of queued elements (exact when called from either
    /// endpoint while the other is idle).
    [[nodiscard]] std::size_t sizeApprox() const noexcept {
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity - 1; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    bool emplace(T&& value) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head - cachedTail_ >= Capacity - 1) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if (head - cachedTail_ >= Capacity - 1) {
                return false;
            }
        }
        slots_[head & kMask] = std::move(value);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Producer-owned.
    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0};
    std::size_t cachedTail_ = 0;
    // Consumer-owned.
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0};
    std::size_t cachedHead_ = 0;

    alignas(kCacheLineSize) std::array<T, Capacity> slots_{};
};

} // namespace vox
