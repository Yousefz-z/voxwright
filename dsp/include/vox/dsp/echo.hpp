#pragma once

#include "vox/dsp/biquad.hpp"
#include "vox/dsp/delay_line.hpp"
#include "vox/dsp/one_pole.hpp"
#include "vox/dsp/smoothed_value.hpp"

#include <span>

namespace vox::dsp {

/// Feedback delay with damping in the loop. Delay-time changes glide through
/// a one-pole smoother to avoid jumps (a short pitch bend instead of a click).
class Echo {
public:
    void prepare(double sampleRate, float maxSeconds = 2.0F);
    void reset() noexcept;

    void setTimeMs(float ms) noexcept;
    void setFeedback(float fb) noexcept { feedback_ = std::clamp(fb, 0.0F, 0.95F); }
    void setDampingHz(float hz) noexcept;
    void setMix(float mix) noexcept { mix_.setTarget(mix); }

    void process(std::span<float> block) noexcept;

private:
    double sampleRate_ = 48000.0;
    DelayLine delay_;
    OnePoleLowpass timeSmoother_;
    OnePoleLowpass damping_;
    Biquad lowCut_;
    float targetDelay_ = 12000.0F;
    float feedback_ = 0.35F;
    SmoothedValue mix_;
};

/// Feedback delay network reverb: 8 delay lines mixed by a Hadamard matrix,
/// per-line decay set from RT60, damping, slow delay modulation against
/// metallic ringing, input diffusion, and predelay.
class Reverb {
public:
    void prepare(double sampleRate);
    void reset() noexcept;

    void setSize(float size) noexcept;         ///< 0..1 (room to hall)
    void setDecaySeconds(float rt60) noexcept; ///< RT60
    void setDampingHz(float hz) noexcept;
    void setPredelayMs(float ms) noexcept;
    void setMix(float mix) noexcept { mix_.setTarget(mix); }

    void process(std::span<float> block) noexcept;

private:
    static constexpr std::size_t kLines = 8;
    void updateGains() noexcept;

    double sampleRate_ = 48000.0;
    std::array<DelayLine, kLines> lines_{};
    std::array<float, kLines> lengths_{};
    std::array<float, kLines> gains_{};
    std::array<OnePoleLowpass, kLines> damping_{};
    std::array<float, kLines> state_{};
    std::array<DelayLine, 4> diffusers_{};
    std::array<float, 4> diffuserLengths_{};
    DelayLine predelay_;
    float predelaySamples_ = 0.0F;
    float size_ = 0.5F;
    float rt60_ = 1.5F;
    float dampingHz_ = 6000.0F;
    double modPhase_ = 0.0;
    SmoothedValue mix_;
};

} // namespace vox::dsp
