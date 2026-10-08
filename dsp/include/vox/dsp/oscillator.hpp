#pragma once

#include "vox/dsp/math.hpp"
#include "vox/dsp/noise.hpp"

#include <cmath>

namespace vox::dsp {

enum class LfoShape { Sine, Triangle, Saw, Square, SampleAndHold, SmoothRandom };

/// Low-frequency oscillator, output in [-1, 1].
class Lfo {
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void setFrequency(float hz) noexcept { increment_ = static_cast<double>(hz) / sampleRate_; }
    void setShape(LfoShape shape) noexcept { shape_ = shape; }
    void reset(double phase = 0.0) noexcept {
        phase_ = phase;
        held_ = 0.0F;
        previous_ = 0.0F;
    }

    [[nodiscard]] float next() noexcept {
        const auto p = static_cast<float>(phase_);
        float out = 0.0F;
        switch (shape_) {
        case LfoShape::Sine:
            out = std::sin(kTwoPi * p);
            break;
        case LfoShape::Triangle:
            out = 1.0F - 4.0F * std::abs(p - 0.5F);
            break;
        case LfoShape::Saw:
            out = 2.0F * p - 1.0F;
            break;
        case LfoShape::Square:
            out = p < 0.5F ? 1.0F : -1.0F;
            break;
        case LfoShape::SampleAndHold:
            out = held_;
            break;
        case LfoShape::SmoothRandom: {
            const float t = 0.5F - 0.5F * std::cos(kPi * p);
            out = previous_ + (held_ - previous_) * t;
            break;
        }
        }
        phase_ += increment_;
        if (phase_ >= 1.0) {
            phase_ -= std::floor(phase_);
            previous_ = held_;
            held_ = rng_.nextBipolar();
        }
        return out;
    }

private:
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    double increment_ = 0.0;
    LfoShape shape_ = LfoShape::Sine;
    FastRandom rng_{0xA5A5A5U};
    float held_ = 0.0F;
    float previous_ = 0.0F;
};

enum class Waveform { Sine, Saw, Square, Pulse25, Triangle };

/// Band-limited audio oscillator (PolyBLEP) for vocoder carriers and synthesis.
class Oscillator {
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void setFrequency(float hz) noexcept {
        increment_ = std::clamp(static_cast<double>(hz) / sampleRate_, 0.0, 0.49);
    }
    void setWaveform(Waveform w) noexcept { waveform_ = w; }
    void reset(double phase = 0.0) noexcept {
        phase_ = phase;
        triState_ = 0.0F;
    }

    [[nodiscard]] float next() noexcept {
        const auto p = static_cast<float>(phase_);
        const auto dt = static_cast<float>(increment_);
        float out = 0.0F;
        switch (waveform_) {
        case Waveform::Sine:
            out = std::sin(kTwoPi * p);
            break;
        case Waveform::Saw:
            out = 2.0F * p - 1.0F - polyBlep(p, dt);
            break;
        case Waveform::Square:
            out = pulse(p, dt, 0.5F);
            break;
        case Waveform::Pulse25:
            out = pulse(p, dt, 0.25F);
            break;
        case Waveform::Triangle: {
            // Leaky-integrated band-limited square.
            const float sq = pulse(p, dt, 0.5F);
            triState_ = dt * 4.0F * sq + (1.0F - dt * 0.05F) * triState_;
            out = triState_;
            break;
        }
        }
        phase_ += increment_;
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
        }
        return out;
    }

private:
    static float polyBlep(float t, float dt) noexcept {
        if (dt <= 0.0F) {
            return 0.0F;
        }
        if (t < dt) {
            const float x = t / dt;
            return x + x - x * x - 1.0F;
        }
        if (t > 1.0F - dt) {
            const float x = (t - 1.0F) / dt;
            return x * x + x + x + 1.0F;
        }
        return 0.0F;
    }

    static float pulse(float p, float dt, float width) noexcept {
        float out = p < width ? 1.0F : -1.0F;
        out += polyBlep(p, dt);
        float q = p - width;
        if (q < 0.0F) {
            q += 1.0F;
        }
        out -= polyBlep(q, dt);
        return out;
    }

    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    double increment_ = 0.0;
    Waveform waveform_ = Waveform::Saw;
    float triState_ = 0.0F;
};

} // namespace vox::dsp
