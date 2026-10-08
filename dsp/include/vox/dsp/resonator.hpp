#pragma once

#include "vox/dsp/delay_line.hpp"
#include "vox/dsp/one_pole.hpp"
#include "vox/dsp/smoothed_value.hpp"

#include <span>

namespace vox::dsp {

/// Tuned feedback comb filter with damping: metallic, tube, or "inside a
/// pipe" resonance at a chosen pitch.
class CombResonator {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setFrequency(float hz) noexcept;
    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, -0.98F, 0.98F); }
    void setDampingHz(float hz) noexcept;
    void setMix(float mix) noexcept { mix_.setTarget(mix); }
    void process(std::span<float> block) noexcept;

private:
    double sampleRate_ = 48000.0;
    DelayLine delay_;
    OnePoleLowpass damping_;
    OnePoleLowpass delaySmoother_;
    float targetDelay_ = 200.0F;
    float feedback_ = 0.7F;
    SmoothedValue mix_;
};

} // namespace vox::dsp
