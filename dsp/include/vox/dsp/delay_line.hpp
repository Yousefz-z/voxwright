#pragma once

#include "vox/dsp/math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vox::dsp {

/// Circular delay line with fractional (cubic Hermite) reads.
/// Size is fixed in prepare(); write/read never allocate.
class DelayLine {
public:
    void prepare(std::size_t maxDelaySamples) {
        std::size_t size = 4;
        while (size < maxDelaySamples + 4) {
            size <<= 1U;
        }
        buffer_.assign(size, 0.0F);
        mask_ = size - 1;
        write_ = 0;
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0F);
        write_ = 0;
    }

    void push(float x) noexcept {
        buffer_[write_] = x;
        write_ = (write_ + 1) & mask_;
    }

    /// Sample written `delay` samples ago (integer delay, delay >= 1).
    [[nodiscard]] float readInteger(std::size_t delay) const noexcept {
        return buffer_[(write_ - delay) & mask_];
    }

    /// Fractional read, delay >= 1 (one sample older than the last push).
    [[nodiscard]] float read(float delay) const noexcept {
        const float d = std::max(delay, 1.0F);
        const auto whole = static_cast<std::size_t>(d);
        const float frac = d - static_cast<float>(whole);
        // Points around the target: index (write - whole) and its neighbours.
        const float y1 = buffer_[(write_ - whole) & mask_];
        const float y2 = buffer_[(write_ - whole - 1) & mask_];
        if (whole < 2) {
            return y1 + (y2 - y1) * frac; // the newer neighbour does not exist yet
        }
        const float y0 = buffer_[(write_ - whole + 1) & mask_];
        const float y3 = buffer_[(write_ - whole - 2) & mask_];
        return hermite(y0, y1, y2, y3, frac);
    }

    [[nodiscard]] std::size_t maxDelay() const noexcept {
        return buffer_.empty() ? 0 : buffer_.size() - 4;
    }

private:
    std::vector<float> buffer_;
    std::size_t mask_ = 0;
    std::size_t write_ = 0;
};

} // namespace vox::dsp
