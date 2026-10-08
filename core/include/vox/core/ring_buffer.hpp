#pragma once

#include <vox/core/spsc_queue.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

namespace vox {

/// Single-producer single-consumer ring buffer of samples.
///
/// Memory is allocated once in the constructor; write() and read() are
/// wait-free and allocation-free. Used between the capture callback and each
/// playback callback, which run on different device threads.
template <class T>
class SpscRingBuffer {
public:
    /// Capacity is rounded up to a power of two.
    explicit SpscRingBuffer(std::size_t minCapacity)
        : buffer_(roundUpPowerOfTwo(minCapacity))
        , mask_(buffer_.size() - 1) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;
    ~SpscRingBuffer() = default;

    [[nodiscard]] std::size_t capacity() const noexcept { return buffer_.size(); }

    /// Producer: number of elements that can be written right now.
    [[nodiscard]] std::size_t availableToWrite() const noexcept {
        return buffer_.size() - (writeIndex_.load(std::memory_order_relaxed) -
                                 readIndex_.load(std::memory_order_acquire));
    }

    /// Consumer: number of elements that can be read right now.
    [[nodiscard]] std::size_t availableToRead() const noexcept {
        return writeIndex_.load(std::memory_order_acquire) -
               readIndex_.load(std::memory_order_relaxed);
    }

    /// Producer: writes as many elements as fit; returns how many were written.
    std::size_t write(std::span<const T> data) noexcept {
        const std::size_t w = writeIndex_.load(std::memory_order_relaxed);
        const std::size_t r = readIndex_.load(std::memory_order_acquire);
        const std::size_t count = std::min(data.size(), buffer_.size() - (w - r));
        copyIn(w, data.first(count));
        writeIndex_.store(w + count, std::memory_order_release);
        return count;
    }

    /// Consumer: reads up to out.size() elements; returns how many were read.
    std::size_t read(std::span<T> out) noexcept {
        const std::size_t r = readIndex_.load(std::memory_order_relaxed);
        const std::size_t w = writeIndex_.load(std::memory_order_acquire);
        const std::size_t count = std::min(out.size(), w - r);
        copyOut(r, out.first(count));
        readIndex_.store(r + count, std::memory_order_release);
        return count;
    }

    /// Consumer: drops up to `count` elements; returns how many were dropped.
    std::size_t discard(std::size_t count) noexcept {
        const std::size_t r = readIndex_.load(std::memory_order_relaxed);
        const std::size_t w = writeIndex_.load(std::memory_order_acquire);
        const std::size_t n = std::min(count, w - r);
        readIndex_.store(r + n, std::memory_order_release);
        return n;
    }

private:
    static std::size_t roundUpPowerOfTwo(std::size_t n) {
        std::size_t p = 2;
        while (p < n) {
            p <<= 1U;
        }
        return p;
    }

    void copyIn(std::size_t start, std::span<const T> data) noexcept {
        const std::size_t first = std::min(data.size(), buffer_.size() - (start & mask_));
        std::copy_n(data.begin(), first,
                    buffer_.begin() + static_cast<std::ptrdiff_t>(start & mask_));
        std::copy(data.begin() + static_cast<std::ptrdiff_t>(first), data.end(), buffer_.begin());
    }

    void copyOut(std::size_t start, std::span<T> out) noexcept {
        const std::size_t first = std::min(out.size(), buffer_.size() - (start & mask_));
        const auto begin = buffer_.begin() + static_cast<std::ptrdiff_t>(start & mask_);
        std::copy_n(begin, first, out.begin());
        std::copy_n(buffer_.begin(), out.size() - first,
                    out.begin() + static_cast<std::ptrdiff_t>(first));
    }

    std::vector<T> buffer_;
    std::size_t mask_;
    alignas(kCacheLineSize) std::atomic<std::size_t> writeIndex_{0};
    alignas(kCacheLineSize) std::atomic<std::size_t> readIndex_{0};
};

} // namespace vox
