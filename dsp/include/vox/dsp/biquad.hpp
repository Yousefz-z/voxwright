#pragma once

#include <span>

namespace vox::dsp {

enum class BiquadType { Lowpass, Highpass, Bandpass, Notch, Peak, LowShelf, HighShelf, Allpass };

/// Normalized biquad coefficients (a0 = 1).
struct BiquadCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

/// RBJ audio EQ cookbook designs. `gainDb` is used by Peak and shelf types.
[[nodiscard]] BiquadCoefficients designBiquad(BiquadType type, double sampleRate, double freqHz,
                                              double q, double gainDb = 0.0) noexcept;

/// Magnitude response in dB of a coefficient set at `freqHz`.
[[nodiscard]] double biquadMagnitudeDb(const BiquadCoefficients& c, double sampleRate,
                                       double freqHz) noexcept;

/// Transposed direct form II biquad with double-precision state.
class Biquad {
public:
    void setCoefficients(const BiquadCoefficients& c) noexcept { c_ = c; }
    [[nodiscard]] const BiquadCoefficients& coefficients() const noexcept { return c_; }
    void reset() noexcept {
        z1_ = 0.0;
        z2_ = 0.0;
    }

    [[nodiscard]] float processSample(float x) noexcept {
        const auto in = static_cast<double>(x);
        const double y = c_.b0 * in + z1_;
        z1_ = c_.b1 * in - c_.a1 * y + z2_;
        z2_ = c_.b2 * in - c_.a2 * y;
        return static_cast<float>(y);
    }

    void process(std::span<float> block) noexcept {
        for (float& s : block) {
            s = processSample(s);
        }
    }

private:
    BiquadCoefficients c_{};
    double z1_ = 0.0;
    double z2_ = 0.0;
};

} // namespace vox::dsp
