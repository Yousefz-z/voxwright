#include "vox/dsp/distortion.hpp"

#include "vox/dsp/math.hpp"

#include <algorithm>
#include <cmath>

namespace vox::dsp {

void Distortion::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    drive_.prepare(sampleRate, 30.0F);
    drive_.setImmediate(12.0F);
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(1.0F);
    output_.prepare(sampleRate, 30.0F);
    output_.setImmediate(-6.0F);
    setToneHz(8000.0F);
    dcBlock_.setCoefficients(designBiquad(BiquadType::Highpass, sampleRate, 20.0, 0.707));
    reset();
}

void Distortion::reset() noexcept {
    oversampler_.reset();
    tone_.reset();
    dcBlock_.reset();
    crushPhase_ = 0.0F;
    crushHeld_ = 0.0F;
}

void Distortion::setToneHz(float hz) noexcept {
    tone_.setCoefficients(designBiquad(BiquadType::Lowpass, sampleRate_,
                                       static_cast<double>(std::clamp(hz, 500.0F, 20000.0F)),
                                       0.707));
}

float Distortion::shape(float x) const noexcept {
    switch (mode_) {
    case Mode::Soft:
        return std::tanh(x);
    case Mode::Hard:
        return std::clamp(x, -1.0F, 1.0F);
    case Mode::Fold: {
        // Triangle-wave folding keeps the output in [-1, 1].
        const float t = (x + 1.0F) * 0.25F;
        return 4.0F * std::abs(t - std::floor(t + 0.5F)) - 1.0F;
    }
    case Mode::Tube:
        // Asymmetric curve adds even harmonics.
        return x >= 0.0F ? std::tanh(x) : std::tanh(0.6F * x) / 0.6F * 0.75F;
    case Mode::Rectify:
        return std::tanh(std::abs(x) * 1.5F) * 1.3F - 0.3F;
    case Mode::Crush:
        return x;
    }
    return x;
}

void Distortion::process(std::span<float> block) noexcept {
    for (float& sample : block) {
        const float drive = dbToGain(drive_.next());
        const float dry = sample;
        float wet = 0.0F;
        if (mode_ == Mode::Crush) {
            // Sample-and-hold rate reduction then quantization; aliasing is the point.
            crushPhase_ += 1.0F;
            if (crushPhase_ >= std::max(downsample_, 1.0F)) {
                crushPhase_ -= std::max(downsample_, 1.0F);
                crushHeld_ = sample * drive;
            }
            const float levels = std::exp2(std::clamp(bits_, 1.0F, 16.0F) - 1.0F);
            wet = std::round(std::clamp(crushHeld_, -1.0F, 1.0F) * levels) / levels;
        } else {
            float a = 0.0F;
            float b = 0.0F;
            oversampler_.upsample(sample * drive, a, b);
            wet = oversampler_.downsample(shape(a), shape(b));
        }
        wet = dcBlock_.processSample(tone_.processSample(wet)) * dbToGain(output_.next());
        const float m = mix_.next();
        sample = dry + m * (wet - dry);
    }
}

} // namespace vox::dsp
