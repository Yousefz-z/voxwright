#include "vox/dsp/oversampler.hpp"

#include "vox/dsp/math.hpp"

#include <cmath>

namespace vox::dsp {
namespace {

double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 50; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

} // namespace

Oversampler2x::Oversampler2x() {
    constexpr double kBeta = 8.6;
    constexpr double kCutoff = 0.25; // of the oversampled rate: base-rate Nyquist
    const double centre = static_cast<double>(kTaps - 1) / 2.0;
    double sum = 0.0;
    std::array<double, kTaps> h{};
    for (std::size_t i = 0; i < kTaps; ++i) {
        const double n = static_cast<double>(i) - centre;
        const double sinc = n == 0.0 ? 2.0 * kCutoff : std::sin(kTwoPiD * kCutoff * n) / (kPiD * n);
        const double r = n / centre;
        const double window =
            besselI0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(kBeta);
        h[i] = sinc * window;
        sum += h[i];
    }
    for (std::size_t i = 0; i < kTaps; ++i) {
        coefficients_[i] = static_cast<float>(h[i] / sum);
    }
}

void Oversampler2x::reset() noexcept {
    upHistory_.fill(0.0F);
    downHistory_.fill(0.0F);
    upPos_ = 0;
    downPos_ = 0;
}

float Oversampler2x::filter(std::array<float, 2 * kTaps>& history, std::size_t& pos,
                            float x) noexcept {
    // Doubled history buffer so the convolution reads a contiguous window.
    history[pos] = x;
    history[pos + kTaps] = x;
    pos = pos == 0 ? kTaps - 1 : pos - 1;
    const float* window = history.data() + pos + 1;
    float acc = 0.0F;
    for (std::size_t i = 0; i < kTaps; ++i) {
        acc += coefficients_[i] * window[i];
    }
    return acc;
}

void Oversampler2x::upsample(float x, float& out0, float& out1) noexcept {
    // Zero stuffing halves the energy; the factor 2 restores unity gain.
    out0 = 2.0F * filter(upHistory_, upPos_, x);
    out1 = 2.0F * filter(upHistory_, upPos_, 0.0F);
}

float Oversampler2x::downsample(float in0, float in1) noexcept {
    const float y = filter(downHistory_, downPos_, in0);
    static_cast<void>(filter(downHistory_, downPos_, in1));
    return y;
}

} // namespace vox::dsp
