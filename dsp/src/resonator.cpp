#include "vox/dsp/resonator.hpp"

#include <algorithm>

namespace vox::dsp {

void CombResonator::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    delay_.prepare(static_cast<std::size_t>(sampleRate / 20.0) + 8);
    delaySmoother_.setCutoff(sampleRate, 10.0);
    setDampingHz(6000.0F);
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(0.5F);
    reset();
}

void CombResonator::reset() noexcept {
    delay_.reset();
    damping_.reset();
    delaySmoother_.reset(targetDelay_);
}

void CombResonator::setFrequency(float hz) noexcept {
    const float maxDelay = static_cast<float>(delay_.maxDelay()) - 4.0F;
    targetDelay_ =
        std::clamp(static_cast<float>(sampleRate_) / std::max(hz, 20.0F), 2.0F, maxDelay);
}

void CombResonator::setDampingHz(float hz) noexcept {
    damping_.setCutoff(sampleRate_, static_cast<double>(std::clamp(hz, 200.0F, 20000.0F)));
}

void CombResonator::process(std::span<float> block) noexcept {
    for (float& sample : block) {
        const float d = delaySmoother_.processSample(targetDelay_);
        const float delayed = damping_.processSample(delay_.read(d));
        const float y = sample + feedback_ * delayed;
        delay_.push(y);
        const float m = mix_.next();
        // Normalize the resonant peak gain so high feedback does not explode.
        sample = sample + m * (y * (1.0F - std::abs(feedback_)) - sample);
    }
}

} // namespace vox::dsp
