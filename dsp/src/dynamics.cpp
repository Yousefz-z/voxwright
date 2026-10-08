#include "vox/dsp/dynamics.hpp"

#include "vox/dsp/math.hpp"

#include <algorithm>
#include <cmath>

namespace vox::dsp {

// ---------------------------------------------------------------- Compressor

void Compressor::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    detector_.prepare(sampleRate);
    detector_.setMode(EnvelopeFollower::Mode::Rms);
    // Equal times give the true mean square; dynamics come from the gain smoothing.
    detector_.setTimes(10.0F, 10.0F);
    setTimes(5.0F, 80.0F);
    reset();
}

void Compressor::reset() noexcept {
    detector_.reset();
    reductionDb_ = 0.0F;
}

void Compressor::setTimes(float attackMs, float releaseMs) noexcept {
    attack_ = onePoleCoefficient(attackMs, sampleRate_);
    release_ = onePoleCoefficient(releaseMs, sampleRate_);
}

float Compressor::gainReductionDb(float inputDb) const noexcept {
    const float over = inputDb - thresholdDb_;
    const float slope = 1.0F / ratio_ - 1.0F;
    if (kneeDb_ > 0.0F && std::abs(over) <= kneeDb_ * 0.5F) {
        const float x = over + kneeDb_ * 0.5F;
        return slope * x * x / (2.0F * kneeDb_);
    }
    return over > 0.0F ? slope * over : 0.0F;
}

void Compressor::process(std::span<float> block) noexcept {
    const float makeup = dbToGain(makeupDb_);
    for (float& sample : block) {
        const float level = detector_.processSample(sample);
        const float target = gainReductionDb(gainToDb(level) + 3.01F); // RMS to peak-ish
        const float coeff = target < reductionDb_ ? attack_ : release_;
        reductionDb_ = target + coeff * (reductionDb_ - target);
        sample *= dbToGain(reductionDb_) * makeup;
    }
}

// ---------------------------------------------------------------- Limiter

void Limiter::prepare(double sampleRate, float lookaheadMs) {
    sampleRate_ = sampleRate;
    lookahead_ =
        std::max<std::size_t>(2, static_cast<std::size_t>(std::lround(
                                     static_cast<double>(lookaheadMs) * 0.001 * sampleRate)));
    delay_.assign(lookahead_, 0.0F);
    required_.assign(lookahead_, 1.0F);
    minQueue_.assign(lookahead_ + 1, 0);
    average_.assign(lookahead_, 1.0F);
    release_ = onePoleCoefficient(80.0F, sampleRate);
    reset();
}

void Limiter::reset() noexcept {
    std::fill(delay_.begin(), delay_.end(), 0.0F);
    std::fill(required_.begin(), required_.end(), 1.0F);
    std::fill(average_.begin(), average_.end(), 1.0F);
    averageSum_ = static_cast<double>(lookahead_);
    queueHead_ = 0;
    queueTail_ = 0;
    index_ = 0;
    recompute_ = 0;
    held_ = 1.0F;
    lastGain_ = 1.0F;
}

void Limiter::setCeilingDb(float db) noexcept {
    ceiling_ = dbToGain(std::min(db, 0.0F));
}

void Limiter::setReleaseMs(float ms) noexcept {
    release_ = onePoleCoefficient(std::max(ms, 1.0F), sampleRate_);
}

float Limiter::currentGainDb() const noexcept {
    return gainToDb(lastGain_);
}

void Limiter::process(std::span<float> block) noexcept {
    const std::size_t n = lookahead_;
    const std::size_t qCap = minQueue_.size();
    for (float& sample : block) {
        const float peak = std::abs(sample);
        const float need = peak > ceiling_ ? ceiling_ / peak : 1.0F;

        // Running minimum of `need` over the last n samples (monotonic deque).
        const std::size_t slot = index_ % n;
        required_[slot] = need;
        while (queueHead_ != queueTail_) {
            const std::size_t back = minQueue_[(queueTail_ + qCap - 1) % qCap];
            if (required_[back % n] < need) {
                break;
            }
            queueTail_ = (queueTail_ + qCap - 1) % qCap;
        }
        minQueue_[queueTail_] = index_;
        queueTail_ = (queueTail_ + 1) % qCap;
        while (minQueue_[queueHead_] + n <= index_) {
            queueHead_ = (queueHead_ + 1) % qCap;
        }
        const float windowMin = required_[minQueue_[queueHead_] % n];

        // Release: rise slowly, fall immediately. Never above windowMin.
        held_ = windowMin < held_ ? windowMin : windowMin + release_ * (held_ - windowMin);

        // Moving average over n samples of the held envelope.
        averageSum_ += static_cast<double>(held_) - static_cast<double>(average_[slot]);
        average_[slot] = held_;
        if (++recompute_ >= 4096) { // bound floating-point drift
            recompute_ = 0;
            averageSum_ = 0.0;
            for (const float v : average_) {
                averageSum_ += static_cast<double>(v);
            }
        }
        const auto gain = static_cast<float>(averageSum_ / static_cast<double>(n));

        // Output the sample from n - 1 samples ago.
        const std::size_t outSlot = (index_ + 1) % n;
        const float delayed = delay_[outSlot];
        delay_[slot] = sample;
        lastGain_ = gain;
        sample = std::clamp(delayed * gain, -ceiling_, ceiling_);
        ++index_;
    }
}

// ---------------------------------------------------------------- NoiseGate

void NoiseGate::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    detector_.prepare(sampleRate);
    detector_.setMode(EnvelopeFollower::Mode::Peak);
    detector_.setTimes(0.5F, 40.0F);
    setTimes(1.0F, 80.0F, 120.0F);
    reset();
}

void NoiseGate::reset() noexcept {
    detector_.reset();
    open_ = false;
    gain_ = dbToGain(rangeDb_);
    holdCounter_ = 0;
}

void NoiseGate::setTimes(float attackMs, float holdMs, float releaseMs) noexcept {
    attack_ = onePoleCoefficient(attackMs, sampleRate_);
    release_ = onePoleCoefficient(releaseMs, sampleRate_);
    holdSamples_ = static_cast<int>(static_cast<double>(holdMs) * 0.001 * sampleRate_);
}

void NoiseGate::process(std::span<float> block) noexcept {
    const float openLevel = dbToGain(thresholdDb_);
    const float closeLevel = dbToGain(thresholdDb_ - hysteresisDb_);
    const float floor = dbToGain(rangeDb_);
    for (float& sample : block) {
        const float level = detector_.processSample(sample);
        if (level >= openLevel) {
            open_ = true;
            holdCounter_ = holdSamples_;
        } else if (open_ && level < closeLevel) {
            if (holdCounter_ > 0) {
                --holdCounter_;
            } else {
                open_ = false;
            }
        }
        const float target = open_ ? 1.0F : floor;
        const float coeff = target > gain_ ? attack_ : release_;
        gain_ = target + coeff * (gain_ - target);
        sample *= gain_;
    }
}

} // namespace vox::dsp
