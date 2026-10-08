#pragma once

#include "vox/dsp/biquad.hpp"
#include "vox/dsp/smoothed_value.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace vox::dsp {

/// Seven-stage tone shaper: high-pass, low shelf, three peaks, high shelf,
/// low-pass. Frequencies and gains glide (coefficients are recomputed every
/// 32 samples while a parameter is moving), so sweeps do not click.
class Equalizer {
public:
    enum Band : std::size_t {
        LowCut,
        LowShelf,
        Peak1,
        Peak2,
        Peak3,
        HighShelf,
        HighCut,
        BandCount
    };

    void prepare(double sampleRate);
    void reset() noexcept;

    void setFrequency(Band band, float hz) noexcept;
    void setGainDb(Band band, float db) noexcept;
    void setQ(Band band, float q) noexcept;
    /// Cut filters are bypassed until enabled.
    void setEnabled(Band band, bool enabled) noexcept;

    void process(std::span<float> block) noexcept;
    [[nodiscard]] double magnitudeDb(double hz) const noexcept;

private:
    struct BandState {
        Biquad filter;
        SmoothedValue frequency;
        SmoothedValue gainDb;
        float q = 0.707F;
        bool enabled = true;
        bool dirty = true;
    };

    void updateCoefficients(BandState& band, std::size_t index) const noexcept;
    [[nodiscard]] static BiquadType typeOf(std::size_t index) noexcept;

    double sampleRate_ = 48000.0;
    std::array<BandState, BandCount> bands_{};
};

} // namespace vox::dsp
