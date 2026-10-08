#include "vox/dsp/noise_suppressor.hpp"

#include <rnnoise.h>

#include <algorithm>

namespace vox::dsp {

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

void NoiseSuppressor::prepare(double /*sampleRate*/) {
    state_ = std::make_unique<State>();
    inFrame_.assign(kFrameSize, 0.0F);
    outFrame_.assign(kFrameSize, 0.0F);
    dryFrame_.assign(kFrameSize, 0.0F);
    fill_ = 0;
}

void NoiseSuppressor::reset() noexcept {
    std::fill(inFrame_.begin(), inFrame_.end(), 0.0F);
    std::fill(outFrame_.begin(), outFrame_.end(), 0.0F);
    std::fill(dryFrame_.begin(), dryFrame_.end(), 0.0F);
    fill_ = 0;
}

void NoiseSuppressor::process(std::span<float> block) noexcept {
    // RNNoise expects 16-bit scaled floats.
    constexpr float kScale = 32768.0F;
    const float wet = std::clamp(strength_, 0.0F, 1.0F);
    for (float& sample : block) {
        const float delayedOut = outFrame_[fill_];
        const float delayedDry = dryFrame_[fill_];
        inFrame_[fill_] = sample * kScale;
        sample = delayedDry + wet * (delayedOut - delayedDry);
        if (++fill_ == kFrameSize) {
            fill_ = 0;
            std::copy(inFrame_.begin(), inFrame_.end(), dryFrame_.begin());
            voiceProbability_ =
                rnnoise_process_frame(state_->denoiser, outFrame_.data(), inFrame_.data());
            for (std::size_t i = 0; i < kFrameSize; ++i) {
                outFrame_[i] /= kScale;
                dryFrame_[i] /= kScale;
            }
        }
    }
}

} // namespace vox::dsp
