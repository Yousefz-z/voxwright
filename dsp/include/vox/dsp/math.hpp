#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vox::dsp {

inline constexpr float kPi = std::numbers::pi_v<float>;
inline constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
inline constexpr double kPiD = std::numbers::pi;
inline constexpr double kTwoPiD = 2.0 * std::numbers::pi;

[[nodiscard]] inline float dbToGain(float db) noexcept {
    return std::pow(10.0F, db / 20.0F);
}

[[nodiscard]] inline float gainToDb(float gain) noexcept {
    return 20.0F * std::log10(std::max(gain, 1.0e-9F));
}

[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept {
    return std::exp2(semitones / 12.0F);
}

[[nodiscard]] inline float ratioToSemitones(float ratio) noexcept {
    return 12.0F * std::log2(std::max(ratio, 1.0e-9F));
}

/// One-pole smoothing coefficient for a time constant in milliseconds.
[[nodiscard]] inline float onePoleCoefficient(float timeMs, double sampleRate) noexcept {
    if (timeMs <= 0.0F) {
        return 0.0F;
    }
    return static_cast<float>(std::exp(-1.0 / (static_cast<double>(timeMs) * 0.001 * sampleRate)));
}

/// Cubic Hermite (Catmull-Rom) interpolation between y1 and y2, t in [0, 1).
[[nodiscard]] inline float hermite(float y0, float y1, float y2, float y3, float t) noexcept {
    const float c1 = 0.5F * (y2 - y0);
    const float c2 = y0 - 2.5F * y1 + 2.0F * y2 - 0.5F * y3;
    const float c3 = 0.5F * (y3 - y0) + 1.5F * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

/// Equal-power crossfade gains for position t in [0, 1].
struct CrossfadeGains {
    float from;
    float to;
};

[[nodiscard]] inline CrossfadeGains equalPowerCrossfade(float t) noexcept {
    const float c = std::clamp(t, 0.0F, 1.0F) * 0.5F * kPi;
    return {std::cos(c), std::sin(c)};
}

/// Smooth saturating curve with unity slope at 0, used for soft clipping.
[[nodiscard]] inline float softClip(float x) noexcept {
    return std::tanh(x);
}

} // namespace vox::dsp
