#include "vox/dsp/ring_modulator.hpp"

#include "vox/dsp/math.hpp"

#include <cmath>

namespace vox::dsp {

void RingModulator::prepare(double sampleRate) {
    carrier_.prepare(sampleRate);
    carrier_.setWaveform(Waveform::Sine);
    lfo_.prepare(sampleRate);
    frequency_.prepare(sampleRate, 30.0F);
    frequency_.setImmediate(60.0F);
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(1.0F);
    reset();
}

void RingModulator::reset() noexcept {
    carrier_.reset();
    lfo_.reset();
}

void RingModulator::setLfo(float rateHz, float depthHz) noexcept {
    lfo_.setFrequency(rateHz);
    lfoDepth_ = depthHz;
}

void RingModulator::process(std::span<float> block) noexcept {
    for (float& sample : block) {
        carrier_.setFrequency(std::max(0.0F, frequency_.next() + lfo_.next() * lfoDepth_));
        const float wet = sample * carrier_.next();
        const float m = mix_.next();
        sample = sample + m * (wet - sample);
    }
}

// Two chains of second-order allpass sections whose outputs differ by 90
// degrees over roughly 20 Hz to 20 kHz (coefficients by Olli Niemitalo).
void FrequencyShifter::AllpassChain::reset() noexcept {
    x1.fill(0.0F);
    x2.fill(0.0F);
    y1.fill(0.0F);
    y2.fill(0.0F);
}

float FrequencyShifter::AllpassChain::process(float x) noexcept {
    for (std::size_t i = 0; i < coeff.size(); ++i) {
        const float y = coeff[i] * (x + y2[i]) - x2[i];
        x2[i] = x1[i];
        x1[i] = x;
        y2[i] = y1[i];
        y1[i] = y;
        x = y;
    }
    return x;
}

void FrequencyShifter::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    constexpr std::array<double, 4> kA{0.6923878, 0.9360654322959, 0.9882295226860,
                                       0.9987488452737};
    constexpr std::array<double, 4> kB{0.4021921162426, 0.8561710882420, 0.9722909545651,
                                       0.9952884791278};
    for (std::size_t i = 0; i < 4; ++i) {
        pathA_.coeff[i] = static_cast<float>(kA[i] * kA[i]);
        pathB_.coeff[i] = static_cast<float>(kB[i] * kB[i]);
    }
    shift_.prepare(sampleRate, 30.0F);
    shift_.setImmediate(0.0F);
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(1.0F);
    reset();
}

void FrequencyShifter::reset() noexcept {
    pathA_.reset();
    pathB_.reset();
    delayedA_ = 0.0F;
    phase_ = 0.0;
}

void FrequencyShifter::process(std::span<float> block) noexcept {
    for (float& sample : block) {
        // Path A delayed by one sample is 90 degrees behind path B.
        const float i = delayedA_;
        delayedA_ = pathA_.process(sample);
        const float q = pathB_.process(sample);
        phase_ += static_cast<double>(shift_.next()) / sampleRate_;
        phase_ -= std::floor(phase_);
        const double angle = kTwoPiD * phase_;
        const auto wet = static_cast<float>(static_cast<double>(i) * std::cos(angle) +
                                            static_cast<double>(q) * std::sin(angle));
        const float m = mix_.next();
        sample = sample + m * (wet - sample);
    }
}

} // namespace vox::dsp
