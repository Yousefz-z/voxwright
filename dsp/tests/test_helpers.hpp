#pragma once

#include <vox/testing/analysis.hpp>
#include <vox/testing/render.hpp>
#include <vox/testing/signals.hpp>

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace vox::dsp::test {

inline constexpr double kFs = 48000.0;

/// Frequency (Hz) of the strongest spectral bin between lo and hi.
inline double strongestHz(std::span<const float> x, double lo, double hi, std::size_t fft = 16384) {
    const auto db = vox::testing::powerSpectrumDb(x, fft);
    const double binHz = kFs / static_cast<double>(fft);
    auto best = static_cast<std::size_t>(lo / binHz);
    for (std::size_t k = best; k < db.size() && static_cast<double>(k) * binHz <= hi; ++k) {
        if (db[k] > db[best]) {
            best = k;
        }
    }
    return static_cast<double>(best) * binHz;
}

/// Level in dB of the strongest bin within +-width Hz of `hz`.
inline double levelAt(std::span<const float> x, double hz, double width = 15.0,
                      std::size_t fft = 16384) {
    const auto db = vox::testing::powerSpectrumDb(x, fft);
    const double binHz = kFs / static_cast<double>(fft);
    double best = -400.0;
    for (auto k = static_cast<std::size_t>((hz - width) / binHz);
         k <= static_cast<std::size_t>((hz + width) / binHz) && k < db.size(); ++k) {
        best = std::max(best, db[k]);
    }
    return best;
}

inline std::vector<float> voice(double f0 = 120.0, double seconds = 1.5) {
    vox::testing::VoiceSpec spec;
    spec.f0Hz = f0;
    spec.seconds = seconds;
    return vox::testing::synthVoice(spec);
}

/// Short-term RMS values over consecutive windows.
inline std::vector<double> windowedRms(std::span<const float> x, std::size_t window) {
    std::vector<double> out;
    for (std::size_t pos = 0; pos + window <= x.size(); pos += window) {
        out.push_back(vox::testing::rms(x.subspan(pos, window)));
    }
    return out;
}

} // namespace vox::dsp::test
