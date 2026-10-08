#include "vox/dsp/echo.hpp"

#include "vox/dsp/math.hpp"

#include <cmath>

namespace vox::dsp {

// ---------------------------------------------------------------- Echo

void Echo::prepare(double sampleRate, float maxSeconds) {
    sampleRate_ = sampleRate;
    delay_.prepare(static_cast<std::size_t>(static_cast<double>(maxSeconds) * sampleRate) + 8);
    timeSmoother_.setCutoff(sampleRate, 3.0);
    setDampingHz(5000.0F);
    lowCut_.setCoefficients(designBiquad(BiquadType::Highpass, sampleRate, 120.0, 0.707));
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(0.35F);
    reset();
}

void Echo::reset() noexcept {
    delay_.reset();
    timeSmoother_.reset(targetDelay_);
    damping_.reset();
    lowCut_.reset();
}

void Echo::setTimeMs(float ms) noexcept {
    const float maxDelay = static_cast<float>(delay_.maxDelay()) - 4.0F;
    targetDelay_ = std::clamp(ms * 0.001F * static_cast<float>(sampleRate_), 2.0F, maxDelay);
}

void Echo::setDampingHz(float hz) noexcept {
    damping_.setCutoff(sampleRate_, static_cast<double>(std::clamp(hz, 200.0F, 20000.0F)));
}

void Echo::process(std::span<float> block) noexcept {
    for (float& sample : block) {
        const float d = timeSmoother_.processSample(targetDelay_);
        const float delayed = delay_.read(d);
        const float loop = lowCut_.processSample(damping_.processSample(delayed));
        delay_.push(sample + feedback_ * loop);
        const float m = mix_.next();
        sample = sample + m * delayed;
    }
}

// ---------------------------------------------------------------- Reverb

void Reverb::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (DelayLine& line : lines_) {
        line.prepare(static_cast<std::size_t>(0.25 * sampleRate));
    }
    constexpr std::array<double, 4> kDiffuseMs{4.77, 3.59, 2.73, 1.73};
    for (std::size_t i = 0; i < diffusers_.size(); ++i) {
        diffuserLengths_[i] = static_cast<float>(kDiffuseMs[i] * 0.001 * sampleRate);
        diffusers_[i].prepare(static_cast<std::size_t>(diffuserLengths_[i]) + 8);
    }
    predelay_.prepare(static_cast<std::size_t>(0.2 * sampleRate));
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(0.3F);
    setSize(size_);
    setPredelayMs(10.0F);
    reset();
}

void Reverb::reset() noexcept {
    for (DelayLine& line : lines_) {
        line.reset();
    }
    for (DelayLine& d : diffusers_) {
        d.reset();
    }
    for (OnePoleLowpass& f : damping_) {
        f.reset();
    }
    predelay_.reset();
    state_.fill(0.0F);
    modPhase_ = 0.0;
}

void Reverb::setSize(float size) noexcept {
    size_ = std::clamp(size, 0.0F, 1.0F);
    // Mutually prime-ish base lengths (ms), scaled from small room to hall.
    constexpr std::array<double, kLines> kBaseMs{29.7, 37.1, 41.1, 43.7, 53.0, 59.9, 67.7, 73.3};
    const double scale = 0.35 + 1.65 * static_cast<double>(size_);
    for (std::size_t i = 0; i < kLines; ++i) {
        lengths_[i] = static_cast<float>(kBaseMs[i] * scale * 0.001 * sampleRate_);
    }
    updateGains();
}

void Reverb::setDecaySeconds(float rt60) noexcept {
    rt60_ = std::clamp(rt60, 0.1F, 20.0F);
    updateGains();
}

void Reverb::setDampingHz(float hz) noexcept {
    dampingHz_ = std::clamp(hz, 500.0F, 20000.0F);
    for (OnePoleLowpass& f : damping_) {
        f.setCutoff(sampleRate_, static_cast<double>(dampingHz_));
    }
}

void Reverb::setPredelayMs(float ms) noexcept {
    predelaySamples_ = std::clamp(ms * 0.001F * static_cast<float>(sampleRate_), 1.0F,
                                  static_cast<float>(predelay_.maxDelay()) - 4.0F);
}

void Reverb::updateGains() noexcept {
    for (std::size_t i = 0; i < kLines; ++i) {
        // Each pass through a line must lose 60 dB per RT60 seconds.
        gains_[i] =
            static_cast<float>(std::pow(10.0, -3.0 * static_cast<double>(lengths_[i]) /
                                                  (static_cast<double>(rt60_) * sampleRate_)));
    }
    setDampingHz(dampingHz_);
}

void Reverb::process(std::span<float> block) noexcept {
    constexpr float kDiffusion = 0.6F;
    const auto modDepth = static_cast<float>(0.0015 * sampleRate_);
    const double modIncrement = 0.3 / sampleRate_;
    for (float& sample : block) {
        predelay_.push(sample);
        float x = predelay_.read(predelaySamples_);
        // Schroeder allpass diffusers smear the onset into a dense cloud.
        for (std::size_t i = 0; i < diffusers_.size(); ++i) {
            const float delayed = diffusers_[i].read(diffuserLengths_[i]);
            const float v = x + kDiffusion * delayed;
            diffusers_[i].push(v);
            x = delayed - kDiffusion * v;
        }
        modPhase_ += modIncrement;
        modPhase_ -= std::floor(modPhase_);
        const auto mod = static_cast<float>(std::sin(kTwoPiD * modPhase_));
        std::array<float, kLines> out{};
        for (std::size_t i = 0; i < kLines; ++i) {
            float d = lengths_[i];
            if (i < 2) {
                d += (i == 0 ? mod : -mod) * modDepth;
            }
            out[i] = damping_[i].processSample(lines_[i].read(d)) * gains_[i];
        }
        // Fast Walsh-Hadamard transform, normalized (orthogonal mixing).
        for (std::size_t h = 1; h < kLines; h <<= 1U) {
            for (std::size_t i = 0; i < kLines; i += h << 1U) {
                for (std::size_t j = i; j < i + h; ++j) {
                    const float a = out[j];
                    const float b = out[j + h];
                    out[j] = a + b;
                    out[j + h] = a - b;
                }
            }
        }
        constexpr float kNorm = 0.35355339F; // 1 / sqrt(8)
        float wet = 0.0F;
        for (std::size_t i = 0; i < kLines; ++i) {
            const float v = out[i] * kNorm;
            lines_[i].push(v + x);
            wet += (i % 2 == 0 ? v : -v);
        }
        wet *= 0.5F;
        const float m = mix_.next();
        sample = sample * (1.0F - 0.5F * m) + wet * m;
    }
}

} // namespace vox::dsp
