#pragma once

#include "vox/dsp/envelope.hpp"
#include "vox/dsp/oscillator.hpp"
#include "vox/dsp/smoothed_value.hpp"
#include "vox/dsp/svf.hpp"

#include <span>

namespace vox::dsp {

/// Resonant state-variable filter whose cutoff can be swept by an LFO and by
/// the input envelope (auto-wah).
class MultimodeFilter {
public:
    enum class Mode { Lowpass, Highpass, Bandpass, Notch };

    void prepare(double sampleRate);
    void reset() noexcept;

    void setMode(Mode mode) noexcept { mode_ = mode; }
    void setCutoff(float hz) noexcept { cutoff_.setTarget(hz); }
    void setResonance(float q) noexcept { q_ = q; }
    void setLfo(float rateHz, float depthOctaves, LfoShape shape) noexcept;
    void setEnvelope(float amountOctaves, float attackMs, float releaseMs) noexcept;
    void setMix(float mix) noexcept { mix_.setTarget(mix); }

    void process(std::span<float> block) noexcept;

private:
    double sampleRate_ = 48000.0;
    StateVariableFilter svf_;
    Lfo lfo_;
    EnvelopeFollower envelope_;
    SmoothedValue cutoff_;
    SmoothedValue mix_;
    Mode mode_ = Mode::Lowpass;
    float q_ = 0.707F;
    float lfoDepth_ = 0.0F;
    float envAmount_ = 0.0F;
    int counter_ = 0;
};

} // namespace vox::dsp
