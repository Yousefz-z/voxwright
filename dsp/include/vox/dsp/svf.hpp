#pragma once

#include "vox/dsp/math.hpp"

#include <algorithm>
#include <cmath>

namespace vox::dsp {

/// Topology-preserving-transform state variable filter (trapezoidal
/// integration). Stays stable and click-free under fast cutoff modulation,
/// which is why vocoder bands, wah, and swept filters use it.
class StateVariableFilter {
public:
    struct Outputs {
        float low;
        float band;
        float high;
    };

    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void reset() noexcept {
        ic1_ = 0.0F;
        ic2_ = 0.0F;
    }

    void setParameters(float cutoffHz, float q) noexcept {
        const double fc = std::clamp(static_cast<double>(cutoffHz), 10.0, 0.48 * sampleRate_);
        const auto g = static_cast<float>(std::tan(kPiD * fc / sampleRate_));
        k_ = 1.0F / std::max(q, 0.05F);
        a1_ = 1.0F / (1.0F + g * (g + k_));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    [[nodiscard]] Outputs processSample(float x) noexcept {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0F * v1 - ic1_;
        ic2_ = 2.0F * v2 - ic2_;
        return {v2, v1, x - k_ * v1 - v2};
    }

    /// Band output normalized to 0 dB at the centre frequency.
    [[nodiscard]] float processBandNormalized(float x) noexcept {
        return processSample(x).band * k_;
    }

private:
    double sampleRate_ = 48000.0;
    float k_ = 1.0F;
    float a1_ = 1.0F;
    float a2_ = 0.0F;
    float a3_ = 0.0F;
    float ic1_ = 0.0F;
    float ic2_ = 0.0F;
};

} // namespace vox::dsp
