#pragma once

#include "vox/dsp/oscillator.hpp"
#include "vox/dsp/smoothed_value.hpp"

#include <array>
#include <span>

namespace vox::dsp {

/// Multiplies the voice with a carrier oscillator (classic dalek/robot
/// timbre). The carrier frequency can wobble with an LFO.
class RingModulator {
public:
    void prepare(double sampleRate);
    void reset() noexcept;

    void setFrequency(float hz) noexcept { frequency_.setTarget(hz); }
    void setWaveform(Waveform w) noexcept { carrier_.setWaveform(w); }
    void setLfo(float rateHz, float depthHz) noexcept;
    void setMix(float mix) noexcept { mix_.setTarget(mix); }

    void process(std::span<float> block) noexcept;

private:
    Oscillator carrier_;
    Lfo lfo_;
    SmoothedValue frequency_;
    SmoothedValue mix_;
    float lfoDepth_ = 0.0F;
};

/// Single-sideband frequency shifter: moves every partial by the same number
/// of hertz, which breaks harmonic relationships (metallic, alien timbres).
class FrequencyShifter {
public:
    void prepare(double sampleRate);
    void reset() noexcept;

    void setShiftHz(float hz) noexcept { shift_.setTarget(hz); }
    void setMix(float mix) noexcept { mix_.setTarget(mix); }

    void process(std::span<float> block) noexcept;

private:
    struct AllpassChain {
        std::array<float, 4> coeff{};
        std::array<float, 4> x1{};
        std::array<float, 4> x2{};
        std::array<float, 4> y1{};
        std::array<float, 4> y2{};
        float process(float x) noexcept;
        void reset() noexcept;
    };

    double sampleRate_ = 48000.0;
    AllpassChain pathA_;
    AllpassChain pathB_;
    float delayedA_ = 0.0F;
    double phase_ = 0.0;
    SmoothedValue shift_;
    SmoothedValue mix_;
};

} // namespace vox::dsp
