#pragma once

#include "vox/dsp/math.hpp"

#include <cmath>

namespace vox::dsp {

/// Peak or RMS envelope follower with separate attack and release.
class EnvelopeFollower {
public:
    enum class Mode { Peak, Rms };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void setTimes(float attackMs, float releaseMs) noexcept {
        attack_ = onePoleCoefficient(attackMs, sampleRate_);
        release_ = onePoleCoefficient(releaseMs, sampleRate_);
    }
    void setMode(Mode mode) noexcept { mode_ = mode; }
    void reset() noexcept { state_ = 0.0F; }

    [[nodiscard]] float processSample(float x) noexcept {
        const float in = mode_ == Mode::Peak ? std::abs(x) : x * x;
        const float coeff = in > state_ ? attack_ : release_;
        state_ = in + coeff * (state_ - in);
        return mode_ == Mode::Peak ? state_ : std::sqrt(state_);
    }

    [[nodiscard]] float value() const noexcept {
        return mode_ == Mode::Peak ? state_ : std::sqrt(state_);
    }

private:
    double sampleRate_ = 48000.0;
    Mode mode_ = Mode::Peak;
    float attack_ = 0.0F;
    float release_ = 0.0F;
    float state_ = 0.0F;
};

} // namespace vox::dsp
