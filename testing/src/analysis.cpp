#include "vox/testing/analysis.hpp"

#include <signalsmith-linear/fft.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>

namespace vox::testing {
namespace {

std::vector<double> hann(std::size_t n) {
    std::vector<double> w(n);
    for (std::size_t i = 0; i < n; ++i) {
        w[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                    static_cast<double>(n));
    }
    return w;
}

/// YIN cumulative mean normalized difference over x[0, window + maxLag).
F0Estimate yin(std::span<const float> x, std::size_t window, std::size_t minLag, std::size_t maxLag,
               double sampleRate) {
    std::vector<double> d(maxLag + 2, 0.0);
    for (std::size_t tau = 1; tau <= maxLag + 1; ++tau) {
        double sum = 0.0;
        for (std::size_t j = 0; j < window; ++j) {
            const double diff = static_cast<double>(x[j]) - static_cast<double>(x[j + tau]);
            sum += diff * diff;
        }
        d[tau] = sum;
    }
    std::vector<double> cmnd(maxLag + 2, 1.0);
    double running = 0.0;
    for (std::size_t tau = 1; tau <= maxLag + 1; ++tau) {
        running += d[tau];
        cmnd[tau] = running > 0.0 ? d[tau] * static_cast<double>(tau) / running : 1.0;
    }
    constexpr double kThreshold = 0.15;
    std::size_t best = 0;
    for (std::size_t tau = minLag; tau <= maxLag; ++tau) {
        if (cmnd[tau] < kThreshold) {
            while (tau + 1 <= maxLag && cmnd[tau + 1] < cmnd[tau]) {
                ++tau;
            }
            best = tau;
            break;
        }
    }
    if (best == 0) {
        best = static_cast<std::size_t>(
            std::min_element(cmnd.begin() + static_cast<std::ptrdiff_t>(minLag),
                             cmnd.begin() + static_cast<std::ptrdiff_t>(maxLag + 1)) -
            cmnd.begin());
        if (cmnd[best] > 0.35) {
            return {0.0, cmnd[best]};
        }
    }
    // Parabolic interpolation on the raw difference function.
    double shift = 0.0;
    if (best > 1 && best <= maxLag) {
        const double a = d[best - 1];
        const double b = d[best];
        const double c = d[best + 1];
        const double denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1e-20) {
            shift = 0.5 * (a - c) / denom;
        }
    }
    return {sampleRate / (static_cast<double>(best) + shift), cmnd[best]};
}

} // namespace

double rms(std::span<const float> x) {
    if (x.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const float v : x) {
        sum += static_cast<double>(v) * static_cast<double>(v);
    }
    return std::sqrt(sum / static_cast<double>(x.size()));
}

float peakAbs(std::span<const float> x) {
    float m = 0.0F;
    for (const float v : x) {
        m = std::max(m, std::abs(v));
    }
    return m;
}

double toDb(double linear) {
    return 20.0 * std::log10(std::max(linear, 1e-12));
}

bool allFinite(std::span<const float> x) {
    return std::all_of(x.begin(), x.end(), [](float v) { return std::isfinite(v); });
}

double centsBetween(double hz, double referenceHz) {
    return 1200.0 * std::log2(hz / referenceHz);
}

F0Estimate estimateF0(std::span<const float> x, double sampleRate, double minHz, double maxHz) {
    const auto maxLag = static_cast<std::size_t>(sampleRate / minHz);
    const auto minLag = static_cast<std::size_t>(sampleRate / maxHz);
    if (x.size() < 2 * maxLag + 4) {
        return {};
    }
    const std::size_t window = x.size() - maxLag - 2;
    return yin(x, window, minLag, maxLag, sampleRate);
}

std::vector<double> f0Track(std::span<const float> x, double sampleRate, double minHz, double maxHz,
                            double frameSeconds, double hopSeconds) {
    const auto frame = static_cast<std::size_t>(frameSeconds * sampleRate);
    const auto hop = static_cast<std::size_t>(hopSeconds * sampleRate);
    const auto maxLag = static_cast<std::size_t>(sampleRate / minHz);
    const auto minLag = static_cast<std::size_t>(sampleRate / maxHz);
    std::vector<double> track;
    const double frameRms = 1e-3;
    for (std::size_t start = 0; start + frame + maxLag + 2 <= x.size(); start += hop) {
        const auto seg = x.subspan(start, frame + maxLag + 2);
        if (rms(seg.first(frame)) < frameRms) {
            track.push_back(0.0);
            continue;
        }
        const F0Estimate e = yin(seg, frame, minLag, maxLag, sampleRate);
        track.push_back(e.aperiodicity < 0.25 ? e.hz : 0.0);
    }
    return track;
}

double medianVoiced(const std::vector<double>& track) {
    std::vector<double> v;
    std::copy_if(track.begin(), track.end(), std::back_inserter(v), [](double f) { return f > 0; });
    if (v.empty()) {
        return 0.0;
    }
    const auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

std::vector<double> powerSpectrumDb(std::span<const float> x, std::size_t fftSize) {
    signalsmith::linear::RealFFT<double> fft(fftSize);
    const auto window = hann(fftSize);
    std::vector<double> frame(fftSize);
    std::vector<std::complex<double>> spec(fftSize / 2);
    std::vector<double> power(fftSize / 2 + 1, 0.0);
    std::size_t frames = 0;
    for (std::size_t start = 0; start + fftSize <= x.size(); start += fftSize / 2) {
        for (std::size_t i = 0; i < fftSize; ++i) {
            frame[i] = static_cast<double>(x[start + i]) * window[i];
        }
        fft.fft(frame.data(), spec.data());
        power[0] += spec[0].real() * spec[0].real();
        power[fftSize / 2] += spec[0].imag() * spec[0].imag(); // packed Nyquist
        for (std::size_t k = 1; k < fftSize / 2; ++k) {
            power[k] += std::norm(spec[k]);
        }
        ++frames;
    }
    for (double& p : power) {
        p = 10.0 *
            std::log10(std::max(p / static_cast<double>(std::max<std::size_t>(frames, 1)), 1e-24));
    }
    return power;
}

double spectralCentroidHz(std::span<const float> x, double sampleRate) {
    constexpr std::size_t kFft = 4096;
    const auto db = powerSpectrumDb(x, kFft);
    double num = 0.0;
    double den = 0.0;
    for (std::size_t k = 0; k < db.size(); ++k) {
        const double p = std::pow(10.0, db[k] / 10.0);
        num += p * static_cast<double>(k) * sampleRate / static_cast<double>(kFft);
        den += p;
    }
    return den > 0.0 ? num / den : 0.0;
}

double energyAbove(std::span<const float> x, double sampleRate, double cutoffHz) {
    constexpr std::size_t kFft = 4096;
    const auto db = powerSpectrumDb(x, kFft);
    double above = 0.0;
    double total = 0.0;
    for (std::size_t k = 0; k < db.size(); ++k) {
        const double p = std::pow(10.0, db[k] / 10.0);
        total += p;
        if (static_cast<double>(k) * sampleRate / static_cast<double>(kFft) >= cutoffHz) {
            above += p;
        }
    }
    return total > 0.0 ? above / total : 0.0;
}

std::vector<double> lpcEnvelopeDb(std::span<const float> x, double sampleRate, int order,
                                  std::size_t bins) {
    if (order < 1 || bins < 2) {
        return std::vector<double>(std::max<std::size_t>(bins, 2), 0.0);
    }
    const auto frame = static_cast<std::size_t>(0.03 * sampleRate);
    const auto window = hann(frame);
    const auto p = static_cast<std::size_t>(order);
    std::vector<double> r(p + 1, 0.0);
    std::vector<double> w(frame);
    for (std::size_t start = 0; start + frame <= x.size(); start += frame / 2) {
        // Pre-emphasis flattens the glottal tilt so the envelope shows formants.
        for (std::size_t i = 0; i < frame; ++i) {
            const double prev = (start + i) > 0 ? static_cast<double>(x[start + i - 1]) : 0.0;
            w[i] = (static_cast<double>(x[start + i]) - 0.97 * prev) * window[i];
        }
        for (std::size_t lag = 0; lag <= p; ++lag) {
            double s = 0.0;
            for (std::size_t i = lag; i < frame; ++i) {
                s += w[i] * w[i - lag];
            }
            r[lag] += s;
        }
    }
    r[0] *= 1.0 + 1e-9; // white noise correction for numerical stability
    if (r[0] <= 0.0) {
        return std::vector<double>(bins, -240.0);
    }
    // Levinson-Durbin recursion.
    std::vector<double> a(p + 1, 0.0);
    std::vector<double> tmp(p + 1, 0.0);
    a.front() = 1.0;
    double err = r[0];
    for (std::size_t i = 1; i <= p; ++i) {
        double acc = r[i];
        for (std::size_t j = 1; j < i; ++j) {
            acc += a[j] * r[i - j];
        }
        const double k = -acc / err;
        tmp = a;
        for (std::size_t j = 1; j < i; ++j) {
            a[j] = tmp[j] + k * tmp[i - j];
        }
        a[i] = k;
        err *= (1.0 - k * k);
        if (err <= 0.0) {
            break;
        }
    }
    std::vector<double> env(bins);
    for (std::size_t b = 0; b < bins; ++b) {
        const double omega =
            std::numbers::pi * static_cast<double>(b) / static_cast<double>(bins - 1);
        std::complex<double> sum = 0.0;
        for (std::size_t j = 0; j <= p; ++j) {
            sum += a[j] * std::polar(1.0, -omega * static_cast<double>(j));
        }
        env[b] = 10.0 * std::log10(err / std::max(std::norm(sum), 1e-30));
    }
    static_cast<void>(sampleRate);
    return env;
}

std::vector<double> envelopePeaksHz(const std::vector<double>& envelopeDb, double sampleRate,
                                    std::size_t maxCount) {
    std::vector<double> peaks;
    const double binHz = sampleRate / 2.0 / static_cast<double>(envelopeDb.size() - 1);
    for (std::size_t i = 1; i + 1 < envelopeDb.size() && peaks.size() < maxCount; ++i) {
        if (envelopeDb[i] > envelopeDb[i - 1] && envelopeDb[i] >= envelopeDb[i + 1]) {
            // Parabolic refinement of the peak position.
            const double a = envelopeDb[i - 1];
            const double b = envelopeDb[i];
            const double c = envelopeDb[i + 1];
            const double denom = a - 2.0 * b + c;
            const double shift = std::abs(denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
            peaks.push_back((static_cast<double>(i) + shift) * binHz);
        }
    }
    return peaks;
}

double envelopeDistanceDb(const std::vector<double>& a, const std::vector<double>& b,
                          double sampleRate, double lowHz, double highHz) {
    const double binHz = sampleRate / 2.0 / static_cast<double>(a.size() - 1);
    const auto lo = static_cast<std::size_t>(lowHz / binHz);
    const auto hi = std::min(a.size() - 1, static_cast<std::size_t>(highHz / binHz));
    double meanA = 0.0;
    double meanB = 0.0;
    for (std::size_t i = lo; i <= hi; ++i) {
        meanA += a[i];
        meanB += b[i];
    }
    const auto count = static_cast<double>(hi - lo + 1);
    meanA /= count;
    meanB /= count;
    double sum = 0.0;
    for (std::size_t i = lo; i <= hi; ++i) {
        const double d = (a[i] - meanA) - (b[i] - meanB);
        sum += d * d;
    }
    return std::sqrt(sum / count);
}

std::size_t estimateLag(std::span<const float> reference, std::span<const float> delayed,
                        std::size_t maxLag) {
    std::size_t bestLag = 0;
    double best = -1e300;
    const std::size_t n = std::min(reference.size(), delayed.size());
    for (std::size_t lag = 0; lag <= maxLag && lag < n; ++lag) {
        double sum = 0.0;
        for (std::size_t i = 0; i + lag < n; ++i) {
            sum += static_cast<double>(reference[i]) * static_cast<double>(delayed[i + lag]);
        }
        if (sum > best) {
            best = sum;
            bestLag = lag;
        }
    }
    return bestLag;
}

} // namespace vox::testing
