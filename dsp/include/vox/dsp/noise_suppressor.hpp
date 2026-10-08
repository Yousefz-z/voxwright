#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace vox::dsp {

/// RNNoise speech denoiser (48 kHz, 10 ms frames) with a strength control
/// that blends toward the equally delayed dry signal. Latency is two
/// frames (960 samples, 20 ms): one to collect a frame, and one inside
/// RNNoise, whose output frame is the overlap-add of a window centered on
/// the boundary with the previous frame. The latency is the same at every
/// strength, including 0, so changing it never shifts the signal in time.
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

    /// 0 = bypass, 1 = full suppression. Changes ramp over 20 ms.
    void setStrength(float strength) noexcept { strength_ = strength; }
    void process(std::span<float> block) noexcept;

    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 2 * kFrameSize; }
    /// RNNoise's voice activity probability for the last frame (0..1).
    [[nodiscard]] float voiceProbability() const noexcept { return voiceProbability_; }

private:
    struct State;
    std::unique_ptr<State> state_;
    std::vector<float> inFrame_;
    std::vector<float> outFrame_;
    std::vector<float> dryFrame_;      ///< Input frame k - 1, played with output frame k.
    std::vector<float> previousInput_; ///< Input frame k - 1 while frame k is collected.
    std::size_t fill_ = 0;
    float strength_ = 1.0F;
    float currentStrength_ = 1.0F;
    float strengthStep_ = 1.0F;
    float voiceProbability_ = 0.0F;
};

} // namespace vox::dsp
