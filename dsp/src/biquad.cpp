#include "vox/dsp/biquad.hpp"

#include "vox/dsp/math.hpp"

#include <algorithm>
#include <cmath>
#include <complex>

namespace vox::dsp {

BiquadCoefficients designBiquad(BiquadType type, double sampleRate, double freqHz, double q,
                                double gainDb) noexcept {
    const double f = std::clamp(freqHz, 1.0, 0.49 * sampleRate);
    const double w0 = kTwoPiD * f / sampleRate;
    const double cosW = std::cos(w0);
    const double sinW = std::sin(w0);
    const double alpha = sinW / (2.0 * std::max(q, 1.0e-3));
    const double a = std::pow(10.0, gainDb / 40.0);
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a0 = 1.0;
    double a1 = 0.0;
    double a2 = 0.0;
    switch (type) {
    case BiquadType::Lowpass:
        b0 = (1.0 - cosW) / 2.0;
        b1 = 1.0 - cosW;
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha;
        break;
    case BiquadType::Highpass:
        b0 = (1.0 + cosW) / 2.0;
        b1 = -(1.0 + cosW);
        b2 = b0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha;
        break;
    case BiquadType::Bandpass: // constant 0 dB peak gain
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha;
        break;
    case BiquadType::Notch:
        b0 = 1.0;
        b1 = -2.0 * cosW;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha;
        break;
    case BiquadType::Peak:
        b0 = 1.0 + alpha * a;
        b1 = -2.0 * cosW;
        b2 = 1.0 - alpha * a;
        a0 = 1.0 + alpha / a;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha / a;
        break;
    case BiquadType::LowShelf: {
        const double s = 2.0 * std::sqrt(a) * alpha;
        b0 = a * ((a + 1.0) - (a - 1.0) * cosW + s);
        b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosW);
        b2 = a * ((a + 1.0) - (a - 1.0) * cosW - s);
        a0 = (a + 1.0) + (a - 1.0) * cosW + s;
        a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosW);
        a2 = (a + 1.0) + (a - 1.0) * cosW - s;
        break;
    }
    case BiquadType::HighShelf: {
        const double s = 2.0 * std::sqrt(a) * alpha;
        b0 = a * ((a + 1.0) + (a - 1.0) * cosW + s);
        b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW);
        b2 = a * ((a + 1.0) + (a - 1.0) * cosW - s);
        a0 = (a + 1.0) - (a - 1.0) * cosW + s;
        a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosW);
        a2 = (a + 1.0) - (a - 1.0) * cosW - s;
        break;
    }
    case BiquadType::Allpass:
        b0 = 1.0 - alpha;
        b1 = -2.0 * cosW;
        b2 = 1.0 + alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosW;
        a2 = 1.0 - alpha;
        break;
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

double biquadMagnitudeDb(const BiquadCoefficients& c, double sampleRate, double freqHz) noexcept {
    const double w = kTwoPiD * freqHz / sampleRate;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const std::complex<double> h = (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
    return 20.0 * std::log10(std::max(std::abs(h), 1e-15));
}

} // namespace vox::dsp
