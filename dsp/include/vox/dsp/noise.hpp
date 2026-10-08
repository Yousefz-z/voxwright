#pragma once

#include <cstdint>

namespace vox::dsp {

/// Small fast PRNG (xorshift32) for audio-rate noise on the audio thread.
class FastRandom {
public:
    explicit FastRandom(std::uint32_t seed = 0x9E3779B9U)
        : state_(seed == 0 ? 1U : seed) {}

    void seed(std::uint32_t s) noexcept { state_ = s == 0 ? 1U : s; }

    [[nodiscard]] std::uint32_t nextU32() noexcept {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 17U;
        state_ ^= state_ << 5U;
        return state_;
    }

    /// Uniform in [-1, 1).
    [[nodiscard]] float nextBipolar() noexcept {
        return static_cast<float>(nextU32() >> 8U) * (2.0F / 16777216.0F) - 1.0F;
    }

    /// Uniform in [0, 1).
    [[nodiscard]] float nextUnipolar() noexcept {
        return static_cast<float>(nextU32() >> 8U) * (1.0F / 16777216.0F);
    }

private:
    std::uint32_t state_;
};

/// Pink noise (-3 dB/octave) using Paul Kellet's economy filter.
class PinkNoise {
public:
    explicit PinkNoise(std::uint32_t seed = 12345U)
        : rng_(seed) {}

    [[nodiscard]] float next() noexcept {
        const float white = rng_.nextBipolar();
        b0_ = 0.99765F * b0_ + white * 0.0990460F;
        b1_ = 0.96300F * b1_ + white * 0.2965164F;
        b2_ = 0.57000F * b2_ + white * 1.0526913F;
        return (b0_ + b1_ + b2_ + white * 0.1848F) * 0.2F;
    }

private:
    FastRandom rng_;
    float b0_ = 0.0F;
    float b1_ = 0.0F;
    float b2_ = 0.0F;
};

/// Brown (red) noise: integrated white noise with leakage.
class BrownNoise {
public:
    explicit BrownNoise(std::uint32_t seed = 777U)
        : rng_(seed) {}

    [[nodiscard]] float next() noexcept {
        state_ = 0.995F * state_ + 0.05F * rng_.nextBipolar();
        return state_ * 2.5F;
    }

private:
    FastRandom rng_;
    float state_ = 0.0F;
};

} // namespace vox::dsp
