#include "vox/dsp/multimode_filter.hpp"

#include <algorithm>
#include <cmath>

namespace vox::dsp {

void MultimodeFilter::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    svf_.prepare(sampleRate);
    lfo_.prepare(sampleRate);
    envelope_.prepare(sampleRate);
    envelope_.setTimes(5.0F, 120.0F);
    cutoff_.prepare(sampleRate, 20.0F);
    cutoff_.setImmediate(1000.0F);
    mix_.prepare(sampleRate, 20.0F);
    mix_.setImmediate(1.0F);
    reset();
}

void MultimodeFilter::reset() noexcept {
    svf_.reset();
    lfo_.reset();
    envelope_.reset();
    counter_ = 0;
}

void MultimodeFilter::setLfo(float rateHz, float depthOctaves, LfoShape shape) noexcept {
    lfo_.setFrequency(rateHz);
    lfo_.setShape(shape);
    lfoDepth_ = depthOctaves;
}

void MultimodeFilter::setEnvelope(float amountOctaves, float attackMs, float releaseMs) noexcept {
    envAmount_ = amountOctaves;
    envelope_.setTimes(attackMs, releaseMs);
}

void MultimodeFilter::process(std::span<float> block) noexcept {
    constexpr int kUpdateInterval = 8;
    for (float& sample : block) {
        const float env = envelope_.processSample(sample);
        const float lfo = lfo_.next();
        const float base = cutoff_.next();
        if (counter_-- <= 0) {
            counter_ = kUpdateInterval;
            // Envelope in [0, ~1] for speech peaks; map to octaves.
            const float octaves = lfo * lfoDepth_ + std::min(env * 4.0F, 1.0F) * envAmount_;
            svf_.setParameters(base * std::exp2(octaves), q_);
        }
        const auto out = svf_.processSample(sample);
        float wet = 0.0F;
        switch (mode_) {
        case Mode::Lowpass:
            wet = out.low;
            break;
        case Mode::Highpass:
            wet = out.high;
            break;
        case Mode::Bandpass:
            wet = out.band;
            break;
        case Mode::Notch:
            wet = out.low + out.high;
            break;
        }
        const float m = mix_.next();
        sample = sample + m * (wet - sample);
    }
}

} // namespace vox::dsp
