#pragma once

#include "vox/dsp/biquad.hpp"
#include "vox/dsp/oversampler.hpp"
#include "vox/dsp/smoothed_value.hpp"

#include <span>

namespace vox::dsp {

/// Waveshaping distortion with 2x oversampling for the smooth curves, plus
/// deliberately aliasing lo-fi modes (bit and sample-rate reduction).
class Distortion {
public:
    enum class Mode { Soft, Hard, Fold, Tube, Rectify, Crush };

    void prepare(double sampleRate);
    void reset() noexcept;

    void setMode(Mode mode) noexcept { mode_ = mode; }
    void setDriveDb(float db) noexcept { drive_.setTarget(db); }
    /// Post-shaper low-pass to tame fizz.
    void setToneHz(float hz) noexcept;
    void setMix(float mix) noexcept { mix_.setTarget(mix); }
    void setOutputDb(float db) noexcept { output_.setTarget(db); }
    /// Crush mode only.
    void setBits(float bits) noexcept { bits_ = bits; }
    void setDownsample(float factor) noexcept { downsample_ = factor; }

    void process(std::span<float> block) noexcept;

private:
    [[nodiscard]] float shape(float x) const noexcept;

    double sampleRate_ = 48000.0;
    Mode mode_ = Mode::Soft;
    Oversampler2x oversampler_;
    Biquad tone_;
    Biquad dcBlock_;
    SmoothedValue drive_;
    SmoothedValue mix_;
    SmoothedValue output_;
    float bits_ = 8.0F;
    float downsample_ = 1.0F;
    float crushPhase_ = 0.0F;
    float crushHeld_ = 0.0F;
};

} // namespace vox::dsp
