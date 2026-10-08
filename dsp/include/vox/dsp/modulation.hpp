#pragma once

#include "vox/dsp/delay_line.hpp"
#include "vox/dsp/oscillator.hpp"
#include "vox/dsp/smoothed_value.hpp"

#include <array>
#include <span>

namespace vox::dsp {

/// Up to four modulated delay voices around a 20 ms base delay.
class Chorus {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setVoices(int voices) noexcept { voices_ = std::clamp(voices, 1, 4); }
    void setRate(float hz) noexcept;
    void setDepthMs(float ms) noexcept { depthMs_ = ms; }
    void setDelayMs(float ms) noexcept { delayMs_ = ms; }
    void setMix(float mix) noexcept { mix_.setTarget(mix); }
    void process(std::span<float> block) noexcept;

private:
    double sampleRate_ = 48000.0;
    DelayLine delay_;
    std::array<Lfo, 4> lfos_{};
    int voices_ = 2;
    float depthMs_ = 3.0F;
    float delayMs_ = 20.0F;
    SmoothedValue mix_;
};

/// Short modulated delay with feedback (jet-plane sweep).
class Flanger {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setRate(float hz) noexcept { lfo_.setFrequency(hz); }
    void setDepthMs(float ms) noexcept { depthMs_ = ms; }
    void setDelayMs(float ms) noexcept { delayMs_ = ms; }
    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, -0.95F, 0.95F); }
    void setMix(float mix) noexcept { mix_.setTarget(mix); }
    void process(std::span<float> block) noexcept;

private:
    double sampleRate_ = 48000.0;
    DelayLine delay_;
    Lfo lfo_;
    float depthMs_ = 2.0F;
    float delayMs_ = 2.5F;
    float feedback_ = 0.5F;
    float last_ = 0.0F;
    SmoothedValue mix_;
};

/// Cascade of first-order allpass stages swept by an LFO.
class Phaser {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setStages(int stages) noexcept { stages_ = std::clamp(stages, 2, kMaxStages); }
    void setRate(float hz) noexcept { lfo_.setFrequency(hz); }
    void setRange(float lowHz, float highHz) noexcept;
    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, -0.9F, 0.9F); }
    void setMix(float mix) noexcept { mix_.setTarget(mix); }
    void process(std::span<float> block) noexcept;

private:
    static constexpr int kMaxStages = 12;
    double sampleRate_ = 48000.0;
    Lfo lfo_;
    std::array<float, kMaxStages> state_{};
    int stages_ = 6;
    float lowHz_ = 300.0F;
    float highHz_ = 3000.0F;
    float feedback_ = 0.4F;
    float last_ = 0.0F;
    SmoothedValue mix_;
};

/// Amplitude modulation (trembling voice, helicopter chop with a square LFO).
class Tremolo {
public:
    void prepare(double sampleRate);
    void reset() noexcept { lfo_.reset(); }
    void setRate(float hz) noexcept { lfo_.setFrequency(hz); }
    void setDepth(float depth) noexcept { depth_.setTarget(std::clamp(depth, 0.0F, 1.0F)); }
    void setShape(LfoShape shape) noexcept { lfo_.setShape(shape); }
    void process(std::span<float> block) noexcept;

private:
    Lfo lfo_;
    SmoothedValue depth_;
    float smoothedGain_ = 1.0F;
    float smoothing_ = 0.0F;
};

/// Pitch vibrato through a modulated delay; depth is given in cents.
class Vibrato {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setRate(float hz) noexcept;
    void setDepthCents(float cents) noexcept;
    void process(std::span<float> block) noexcept;

    [[nodiscard]] std::size_t latencySamples() const noexcept {
        return static_cast<std::size_t>(baseDelay_);
    }

private:
    void updateAmplitude() noexcept;

    double sampleRate_ = 48000.0;
    DelayLine delay_;
    Lfo lfo_;
    float rate_ = 5.0F;
    float cents_ = 20.0F;
    float amplitude_ = 0.0F; // samples
    float baseDelay_ = 0.0F;
};

} // namespace vox::dsp
