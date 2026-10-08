#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace vox::dsp {

/// RNNoise speech denoiser (48 kHz, 10 ms frames) with a strength control
/// that blends toward the dry signal. Adds one frame (480 samples) of
/// latency because RNNoise works on whole frames.
class NoiseSuppressor {
public:
    static constexpr std::size_t kFrameSize = 480;

    NoiseSuppressor();
    ~NoiseSuppressor();
    NoiseSuppressor(const NoiseSuppressor&) = delete;
    NoiseSuppressor& operator=(const NoiseSuppressor&) = delete;
    NoiseSuppressor(NoiseSuppressor&&) noexcept;
    NoiseSuppressor& operator=(NoiseSuppressor&&) noexcept;

    /// RNNoise is trained for 48 kHz; other rates must be converted first.
    void prepare(double sampleRate);
    void reset() noexcept;

    /// 0 = bypass, 1 = full suppression.
    void setStrength(float strength) noexcept { strength_ = strength; }
    void process(std::span<float> block) noexcept;

    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return kFrameSize; }
    /// RNNoise's voice activity probability for the last frame (0..1).
    [[nodiscard]] float voiceProbability() const noexcept { return voiceProbability_; }

private:
    struct State;
    std::unique_ptr<State> state_;
    std::vector<float> inFrame_;
    std::vector<float> outFrame_;
    std::vector<float> dryFrame_;
    std::size_t fill_ = 0;
    float strength_ = 1.0F;
    float voiceProbability_ = 0.0F;
};

} // namespace vox::dsp
