#include "vox/dsp/noise_suppressor.hpp"

#include <rnnoise.h>

#include <algorithm>

namespace vox::dsp {
namespace {

constexpr float kStrengthRampMs = 20.0F;

} // namespace

struct NoiseSuppressor::State {
    DenoiseState* denoiser = nullptr;
    State()
        : denoiser(rnnoise_create(nullptr)) {}
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;
    ~State() { rnnoise_destroy(denoiser); }
};

NoiseSuppressor::NoiseSuppressor() = default;
NoiseSuppressor::~NoiseSuppressor() = default;
NoiseSuppressor::NoiseSuppressor(NoiseSuppressor&&) noexcept = default;
NoiseSuppressor& NoiseSuppressor::operator=(NoiseSuppressor&&) noexcept = default;

void NoiseSuppressor::prepare(double sampleRate) {
    state_ = std::make_unique<State>();
    strengthStep_ = 1000.0F / (kStrengthRampMs * static_cast<float>(sampleRate));
    currentStrength_ = std::clamp(strength_, 0.0F, 1.0F);
    inFrame_.assign(kFrameSize, 0.0F);
    outFrame_.assign(kFrameSize, 0.0F);
    dryFrame_.assign(kFrameSize, 0.0F);
    previousInput_.assign(kFrameSize, 0.0F);
    fill_ = 0;
}

void NoiseSuppressor::reset() noexcept {
    std::fill(inFrame_.begin(), inFrame_.end(), 0.0F);
    std::fill(outFrame_.begin(), outFrame_.end(), 0.0F);
    std::fill(dryFrame_.begin(), dryFrame_.end(), 0.0F);
    std::fill(previousInput_.begin(), previousInput_.end(), 0.0F);
    fill_ = 0;
}

void NoiseSuppressor::process(std::span<float> block) noexcept {
    // RNNoise expects 16-bit scaled floats.
    constexpr float kScale = 32768.0F;
    const float target = std::clamp(strength_, 0.0F, 1.0F);
    for (float& sample : block) {
        currentStrength_ += std::clamp(target - currentStrength_, -strengthStep_, strengthStep_);
        const float delayedOut = outFrame_[fill_];
        const float delayedDry = dryFrame_[fill_];
        inFrame_[fill_] = sample * kScale;
        sample = delayedDry + currentStrength_ * (delayedOut - delayedDry);
        if (++fill_ == kFrameSize) {
            fill_ = 0;
            // RNNoise's output for frame k lines up with input frame k - 1.
            std::swap(dryFrame_, previousInput_);
            for (std::size_t i = 0; i < kFrameSize; ++i) {
                previousInput_[i] = inFrame_[i] / kScale;
            }
            voiceProbability_ =
                rnnoise_process_frame(state_->denoiser, outFrame_.data(), inFrame_.data());
            for (float& v : outFrame_) {
                v /= kScale;
            }
        }
    }
}

} // namespace vox::dsp
