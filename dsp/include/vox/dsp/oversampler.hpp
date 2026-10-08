#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace vox::dsp {

/// 2x oversampling with linear-phase half-band FIR filters (Kaiser windowed
/// sinc, 47 taps, about 90 dB stopband). Used around nonlinear waveshapers
/// so their harmonics do not alias back into the audible band.
class Oversampler2x {
public:
    static constexpr std::size_t kTaps = 47;

    Oversampler2x();

    void reset() noexcept;

    /// Produces two oversampled samples from one input sample.
    void upsample(float x, float& out0, float& out1) noexcept;
    /// Consumes two oversampled samples and returns one output sample.
    [[nodiscard]] float downsample(float in0, float in1) noexcept;

    /// Group delay in base-rate samples.
    [[nodiscard]] static constexpr float latency() noexcept {
        return static_cast<float>(kTaps - 1) / 2.0F; // 23 at 2x = 11.5 + 11.5 base-rate samples
    }

private:
    [[nodiscard]] float filter(std::array<float, 2 * kTaps>& history, std::size_t& pos,
                               float x) noexcept;

    std::array<float, kTaps> coefficients_{};
    std::array<float, 2 * kTaps> upHistory_{};
    std::array<float, 2 * kTaps> downHistory_{};
    std::size_t upPos_ = 0;
    std::size_t downPos_ = 0;
};

} // namespace vox::dsp
