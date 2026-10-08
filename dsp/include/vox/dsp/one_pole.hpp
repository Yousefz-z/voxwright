#pragma once

#include "vox/dsp/math.hpp"

#include <cmath>

namespace vox::dsp {

/// One-pole lowpass (6 dB/octave). Also used as a parameter smoother.
class OnePoleLowpass {
public:
    void setCutoff(double sampleRate, double cutoffHz) noexcept {
        coeff_ = static_cast<float>(std::exp(-kTwoPiD * cutoffHz / sampleRate));
    }
    void setCoefficient(float coeff) noexcept { coeff_ = coeff; }
    void reset(float value = 0.0F) noexcept { state_ = value; }
    [[nodiscard]] float processSample(float x) noexcept {
        state_ = x + coeff_ * (state_ - x);
        return state_;
    }
    [[nodiscard]] float state() const noexcept { return state_; }

private:
    float coeff_ = 0.0F;
    float state_ = 0.0F;
};

/// DC blocking filter: y[n] = x[n] - x[n-1] + R y[n-1], corner around 10 Hz.
class DcBlocker {
public:
    void prepare(double sampleRate) noexcept {
        r_ = static_cast<float>(1.0 - kTwoPiD * 10.0 / sampleRate);
    }
    void reset() noexcept {
        x1_ = 0.0F;
        y1_ = 0.0F;
    }
    [[nodiscard]] float processSample(float x) noexcept {
        const float y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    float r_ = 0.9987F;
    float x1_ = 0.0F;
    float y1_ = 0.0F;
};

} // namespace vox::dsp
