#include "vox/dsp/whisper.hpp"

#include "vox/dsp/math.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vox::dsp {
namespace {

constexpr float kPreEmphasis = 0.9F;

std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t p = 1;
    while (p < n) {
        p <<= 1U;
    }
    return p;
}

} // namespace

void Whisper::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    const auto windowLength = static_cast<std::size_t>(0.03 * sampleRate);
    hop_ = static_cast<std::size_t>(0.005 * sampleRate);
    history_.assign(nextPowerOfTwo(windowLength + 8), 0.0F);
    historyMask_ = history_.size() - 1;
    window_.resize(windowLength);
    for (std::size_t i = 0; i < windowLength; ++i) {
        window_[i] = 0.5F - 0.5F * std::cos(kTwoPi * static_cast<float>(i) /
                                            static_cast<float>(windowLength - 1));
    }
    frame_.assign(windowLength, 0.0F);
    // Gaussian lag window (100 Hz bandwidth expansion): keeps the model's
    // peaks finite so the reflection coefficients never approach +-1.
    for (int lag = 0; lag <= kOrder; ++lag) {
        const double x = kTwoPiD * 100.0 * static_cast<double>(lag) / sampleRate;
        lagWindow_[static_cast<std::size_t>(lag)] = std::exp(-0.5 * x * x);
    }
    levelCoeff_ = onePoleCoefficient(40.0F, sampleRate);
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(1.0F);
    reset();
}

void Whisper::reset() noexcept {
    std::fill(history_.begin(), history_.end(), 0.0F);
    written_ = 0;
    sinceHop_ = 0;
    reflection_.fill(0.0F);
    targetReflection_.fill(0.0F);
    reflectionStep_.fill(0.0F);
    lattice_.fill(0.0F);
    excitationGain_ = 0.0F;
    targetGain_ = 0.0F;
    gainStep_ = 0.0F;
    preEmphasisState_ = 0.0F;
    deEmphasisState_ = 0.0F;
    inputPower_ = 0.0F;
    outputPower_ = 0.0F;
    levelGain_ = 1.0F;
}

void Whisper::analyze() noexcept {
    const std::size_t n = frame_.size();
    for (std::size_t i = 0; i < n; ++i) {
        frame_[i] = history_[(written_ - n + i) & historyMask_] * window_[i];
    }
    for (int lag = 0; lag <= kOrder; ++lag) {
        double sum = 0.0;
        for (auto i = static_cast<std::size_t>(lag); i < n; ++i) {
            sum += static_cast<double>(frame_[i]) *
                   static_cast<double>(frame_[i - static_cast<std::size_t>(lag)]);
        }
        autocorr_[static_cast<std::size_t>(lag)] = sum * lagWindow_[static_cast<std::size_t>(lag)];
    }
    autocorr_[0] *= 1.0 + 1e-4; // white-noise correction (-40 dB floor)
    if (autocorr_[0] < 1e-10) {
        targetReflection_.fill(0.0F);
        targetGain_ = 0.0F;
    } else {
        // Levinson-Durbin, keeping the reflection coefficients for the lattice.
        std::array<double, kOrder + 1> a{};
        std::array<double, kOrder + 1> previous{};
        a[0] = 1.0;
        double error = autocorr_[0];
        for (int i = 1; i <= kOrder; ++i) {
            double acc = autocorr_[static_cast<std::size_t>(i)];
            for (int j = 1; j < i; ++j) {
                acc += a[static_cast<std::size_t>(j)] * autocorr_[static_cast<std::size_t>(i - j)];
            }
            const double k = std::clamp(-acc / error, -0.995, 0.995);
            previous = a;
            for (int j = 1; j < i; ++j) {
                a[static_cast<std::size_t>(j)] = previous[static_cast<std::size_t>(j)] +
                                                 k * previous[static_cast<std::size_t>(i - j)];
            }
            a[static_cast<std::size_t>(i)] = k;
            targetReflection_[static_cast<std::size_t>(i - 1)] = static_cast<float>(k);
            error *= (1.0 - k * k);
        }
        // Residual power per sample, corrected for the Hann window's energy (3/8).
        targetGain_ =
            static_cast<float>(std::sqrt(std::max(error, 0.0) / (0.375 * static_cast<double>(n))));
    }
    const auto steps = static_cast<float>(hop_);
    for (std::size_t i = 0; i < reflection_.size(); ++i) {
        reflectionStep_[i] = (targetReflection_[i] - reflection_[i]) / steps;
    }
    gainStep_ = (targetGain_ - excitationGain_) / steps;
}

void Whisper::process(std::span<float> block) noexcept {
    // White noise in [-1, 1) has power 1/3; scale to unit power.
    constexpr float kUnitPower = std::numbers::sqrt3_v<float>;
    for (float& sample : block) {
        const float emphasized = sample - kPreEmphasis * preEmphasisState_;
        preEmphasisState_ = sample;
        history_[written_ & historyMask_] = emphasized;
        ++written_;
        if (++sinceHop_ >= hop_ && written_ >= frame_.size()) {
            sinceHop_ = 0;
            analyze();
        }
        for (std::size_t i = 0; i < reflection_.size(); ++i) {
            reflection_[i] += reflectionStep_[i];
        }
        excitationGain_ = std::max(0.0F, excitationGain_ + gainStep_);

        // All-pole lattice synthesis driven by noise.
        float f = noise_.nextBipolar() * kUnitPower * excitationGain_;
        for (int i = kOrder - 1; i >= 0; --i) {
            const auto idx = static_cast<std::size_t>(i);
            f -= reflection_[idx] * lattice_[idx];
            lattice_[idx + 1] = lattice_[idx] + reflection_[idx] * f;
        }
        lattice_[0] = f;
        deEmphasisState_ = f + kPreEmphasis * deEmphasisState_;
        // Follow the input loudness: the all-pole model's power gain is only
        // approximately right, so a slow level loop matches it exactly.
        const float raw = deEmphasisState_;
        inputPower_ = sample * sample + levelCoeff_ * (inputPower_ - sample * sample);
        outputPower_ = raw * raw + levelCoeff_ * (outputPower_ - raw * raw);
        const float wanted = outputPower_ > 1.0e-12F ? std::sqrt(inputPower_ / outputPower_) : 1.0F;
        levelGain_ = std::clamp(wanted, 0.0F, 1000.0F) +
                     0.999F * (levelGain_ - std::clamp(wanted, 0.0F, 1000.0F));
        const float wet = raw * levelGain_;
        const float m = mix_.next();
        sample = sample + m * (wet - sample);
    }
}

} // namespace vox::dsp
