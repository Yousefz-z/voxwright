#include "vox/dsp/modulation.hpp"

#include "vox/dsp/math.hpp"

#include <cmath>

namespace vox::dsp {

// ---------------------------------------------------------------- Chorus

void Chorus::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    delay_.prepare(static_cast<std::size_t>(0.06 * sampleRate));
    for (Lfo& lfo : lfos_) {
        lfo.prepare(sampleRate);
        lfo.setShape(LfoShape::Sine);
    }
    setRate(0.8F);
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(0.5F);
    reset();
}

void Chorus::reset() noexcept {
    delay_.reset();
    for (std::size_t i = 0; i < lfos_.size(); ++i) {
        lfos_[i].reset(static_cast<double>(i) * 0.25);
    }
}

void Chorus::setRate(float hz) noexcept {
    for (std::size_t i = 0; i < lfos_.size(); ++i) {
        // Slightly different rates keep the voices from moving in lockstep.
        lfos_[i].setFrequency(hz * (1.0F + 0.13F * static_cast<float>(i)));
    }
}

void Chorus::process(std::span<float> block) noexcept {
    const auto msToSamples = static_cast<float>(sampleRate_ * 0.001);
    const float gain = 1.0F / static_cast<float>(voices_);
    for (float& sample : block) {
        delay_.push(sample);
        float wet = 0.0F;
        for (int v = 0; v < voices_; ++v) {
            const float mod = lfos_[static_cast<std::size_t>(v)].next();
            const float d = (delayMs_ + depthMs_ * 0.5F * (1.0F + mod)) * msToSamples;
            wet += delay_.read(d);
        }
        const float m = mix_.next();
        sample = sample * (1.0F - 0.5F * m) + wet * gain * m;
    }
}

// ---------------------------------------------------------------- Flanger

void Flanger::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    delay_.prepare(static_cast<std::size_t>(0.03 * sampleRate));
    lfo_.prepare(sampleRate);
    lfo_.setShape(LfoShape::Triangle);
    lfo_.setFrequency(0.25F);
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(0.5F);
    reset();
}

void Flanger::reset() noexcept {
    delay_.reset();
    lfo_.reset();
    last_ = 0.0F;
}

void Flanger::process(std::span<float> block) noexcept {
    const auto msToSamples = static_cast<float>(sampleRate_ * 0.001);
    for (float& sample : block) {
        const float mod = 0.5F * (1.0F + lfo_.next());
        const float d = std::max(1.0F, (delayMs_ + depthMs_ * mod) * msToSamples);
        delay_.push(sample + feedback_ * last_);
        last_ = delay_.read(d);
        const float m = mix_.next();
        sample = sample * (1.0F - 0.5F * m) + last_ * 0.5F * m;
    }
}

// ---------------------------------------------------------------- Phaser

void Phaser::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    lfo_.prepare(sampleRate);
    lfo_.setShape(LfoShape::Sine);
    lfo_.setFrequency(0.4F);
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(0.5F);
    reset();
}

void Phaser::reset() noexcept {
    lfo_.reset();
    state_.fill(0.0F);
    last_ = 0.0F;
}

void Phaser::setRange(float lowHz, float highHz) noexcept {
    lowHz_ = std::clamp(lowHz, 20.0F, 18000.0F);
    highHz_ = std::clamp(highHz, lowHz_, 18000.0F);
}

void Phaser::process(std::span<float> block) noexcept {
    const float ratio = highHz_ / lowHz_;
    for (float& sample : block) {
        const float sweep = 0.5F * (1.0F + lfo_.next());
        const float fc = lowHz_ * std::pow(ratio, sweep);
        const float t = std::tan(kPi * std::min(fc, 0.45F * static_cast<float>(sampleRate_)) /
                                 static_cast<float>(sampleRate_));
        const float a = (t - 1.0F) / (t + 1.0F);
        float x = sample + feedback_ * last_;
        for (int s = 0; s < stages_; ++s) {
            // First-order allpass: y = a*x + z; z = x - a*y.
            float& z = state_[static_cast<std::size_t>(s)];
            const float y = a * x + z;
            z = x - a * y;
            x = y;
        }
        last_ = x;
        const float m = mix_.next();
        sample = sample * (1.0F - 0.5F * m) + x * 0.5F * m;
    }
}

// ---------------------------------------------------------------- Tremolo

void Tremolo::prepare(double sampleRate) {
    lfo_.prepare(sampleRate);
    lfo_.setFrequency(6.0F);
    depth_.prepare(sampleRate, 30.0F);
    depth_.setImmediate(0.5F);
    // 2 ms smoothing turns square chopping into click-free gating.
    smoothing_ = onePoleCoefficient(2.0F, sampleRate);
    reset();
}

void Tremolo::process(std::span<float> block) noexcept {
    for (float& sample : block) {
        const float depth = depth_.next();
        const float target = 1.0F - depth * 0.5F * (1.0F + lfo_.next());
        smoothedGain_ = target + smoothing_ * (smoothedGain_ - target);
        sample *= smoothedGain_;
    }
}

// ---------------------------------------------------------------- Vibrato

void Vibrato::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    lfo_.prepare(sampleRate);
    lfo_.setShape(LfoShape::Sine);
    delay_.prepare(static_cast<std::size_t>(0.03 * sampleRate));
    updateAmplitude();
    reset();
}

void Vibrato::reset() noexcept {
    delay_.reset();
    lfo_.reset();
}

void Vibrato::setRate(float hz) noexcept {
    rate_ = std::clamp(hz, 0.1F, 20.0F);
    updateAmplitude();
}

void Vibrato::setDepthCents(float cents) noexcept {
    cents_ = std::clamp(cents, 0.0F, 200.0F);
    updateAmplitude();
}

void Vibrato::updateAmplitude() noexcept {
    lfo_.setFrequency(rate_);
    // A delay modulated by A*sin(2 pi f t) changes pitch by a factor of up
    // to 1 + 2 pi f A / fs.
    const float ratio = std::exp2(cents_ / 1200.0F) - 1.0F;
    amplitude_ = ratio * static_cast<float>(sampleRate_) / (kTwoPi * rate_);
    const float maxAmplitude = 0.012F * static_cast<float>(sampleRate_);
    amplitude_ = std::min(amplitude_, maxAmplitude);
    baseDelay_ = maxAmplitude + 4.0F;
}

void Vibrato::process(std::span<float> block) noexcept {
    for (float& sample : block) {
        delay_.push(sample);
        sample = delay_.read(baseDelay_ + amplitude_ * lfo_.next());
    }
}

} // namespace vox::dsp
