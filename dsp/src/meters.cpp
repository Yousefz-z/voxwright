#include "vox/dsp/meters.hpp"

#include "vox/dsp/math.hpp"

#include <signalsmith-linear/fft.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace vox::dsp {

// ---------------------------------------------------------------- LevelMeter

void LevelMeter::prepare(double sampleRate) {
    peakFall_ = static_cast<float>(std::pow(10.0, -20.0 / 20.0 / sampleRate)); // 20 dB per second
    rmsCoeff_ = onePoleCoefficient(300.0F, sampleRate);
    reset();
}

void LevelMeter::reset() noexcept {
    peak_ = 0.0F;
    meanSquare_ = 0.0F;
}

void LevelMeter::process(std::span<const float> block) noexcept {
    for (const float x : block) {
        const float a = std::abs(x);
        peak_ = a > peak_ ? a : peak_ * peakFall_;
        meanSquare_ = x * x + rmsCoeff_ * (meanSquare_ - x * x);
    }
}

float LevelMeter::rms() const noexcept {
    return std::sqrt(meanSquare_);
}

// ---------------------------------------------------------------- KWeighting

void KWeighting::prepare(double sampleRate) {
    // Pre-filter (high shelf) and RLB high-pass from BS.1770, re-derived for
    // arbitrary rates by the bilinear transform (same as libebur128).
    {
        constexpr double kF0 = 1681.974450955533;
        constexpr double kGain = 3.999843853973347;
        constexpr double kQ = 0.7071752369554196;
        const double k = std::tan(kPiD * kF0 / sampleRate);
        const double vh = std::pow(10.0, kGain / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / kQ + k * k;
        BiquadCoefficients c;
        c.b0 = (vh + vb * k / kQ + k * k) / a0;
        c.b1 = 2.0 * (k * k - vh) / a0;
        c.b2 = (vh - vb * k / kQ + k * k) / a0;
        c.a1 = 2.0 * (k * k - 1.0) / a0;
        c.a2 = (1.0 - k / kQ + k * k) / a0;
        shelf_.setCoefficients(c);
    }
    {
        constexpr double kF0 = 38.13547087602444;
        constexpr double kQ = 0.5003270373238773;
        const double k = std::tan(kPiD * kF0 / sampleRate);
        const double a0 = 1.0 + k / kQ + k * k;
        BiquadCoefficients c;
        c.b0 = 1.0;
        c.b1 = -2.0;
        c.b2 = 1.0;
        c.a1 = 2.0 * (k * k - 1.0) / a0;
        c.a2 = (1.0 - k / kQ + k * k) / a0;
        highpass_.setCoefficients(c);
    }
    reset();
}

void KWeighting::reset() noexcept {
    shelf_.reset();
    highpass_.reset();
}

double integratedLoudness(std::span<const float> mono, double sampleRate) {
    KWeighting weighting;
    weighting.prepare(sampleRate);
    std::vector<double> squared(mono.size());
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const auto y = static_cast<double>(weighting.processSample(mono[i]));
        squared[i] = y * y;
    }
    // 400 ms gating blocks with 75 % overlap.
    const auto block = static_cast<std::size_t>(0.4 * sampleRate);
    const std::size_t step = block / 4;
    if (mono.size() < block) {
        return -std::numeric_limits<double>::infinity();
    }
    std::vector<double> blockPower;
    for (std::size_t start = 0; start + block <= mono.size(); start += step) {
        double sum = 0.0;
        for (std::size_t i = start; i < start + block; ++i) {
            sum += squared[i];
        }
        blockPower.push_back(sum / static_cast<double>(block));
    }
    auto loudness = [](double power) {
        return -0.691 + 10.0 * std::log10(std::max(power, 1e-30));
    };
    auto gatedMean = [&](double thresholdLufs) {
        double sum = 0.0;
        std::size_t count = 0;
        for (const double p : blockPower) {
            if (loudness(p) > thresholdLufs) {
                sum += p;
                ++count;
            }
        }
        return count > 0 ? sum / static_cast<double>(count) : 0.0;
    };
    const double absoluteGated = gatedMean(-70.0);
    if (absoluteGated <= 0.0) {
        return -std::numeric_limits<double>::infinity();
    }
    const double relativeGated = gatedMean(loudness(absoluteGated) - 10.0);
    return relativeGated > 0.0 ? loudness(relativeGated) : -std::numeric_limits<double>::infinity();
}

// ---------------------------------------------------------------- FeedbackDetector

struct FeedbackDetector::Fft {
    signalsmith::linear::RealFFT<float> fft{kFftSize};
    std::vector<std::complex<float>> spectrum = std::vector<std::complex<float>>(kFftSize / 2);
};

FeedbackDetector::FeedbackDetector() = default;
FeedbackDetector::~FeedbackDetector() = default;
FeedbackDetector::FeedbackDetector(FeedbackDetector&&) noexcept = default;
FeedbackDetector& FeedbackDetector::operator=(FeedbackDetector&&) noexcept = default;

void FeedbackDetector::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    fft_ = std::make_unique<Fft>();
    frame_.assign(kFftSize, 0.0F);
    window_.resize(kFftSize);
    for (std::size_t i = 0; i < kFftSize; ++i) {
        window_[i] =
            0.5F - 0.5F * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(kFftSize));
    }
    power_.assign(kFftSize / 2, 0.0F);
    // Half-overlapped frames: about 21 ms each at 48 kHz; 0.45 s of steady howl.
    const double frameSeconds = static_cast<double>(kFftSize) / 2.0 / sampleRate;
    requiredFrames_ = static_cast<int>(std::ceil(0.45 / frameSeconds));
    reset();
}

void FeedbackDetector::reset() noexcept {
    std::fill(frame_.begin(), frame_.end(), 0.0F);
    fill_ = 0;
    stableFrames_ = 0;
    lastBin_ = 0;
    detected_ = false;
    peakHz_ = 0.0F;
}

void FeedbackDetector::acknowledge() noexcept {
    detected_ = false;
    stableFrames_ = 0;
}

void FeedbackDetector::process(std::span<const float> block) noexcept {
    for (const float x : block) {
        frame_[fill_++] = x;
        if (fill_ == kFftSize) {
            analyze();
            // Keep the second half for 50 % overlap.
            std::copy(frame_.begin() + kFftSize / 2, frame_.end(), frame_.begin());
            fill_ = kFftSize / 2;
        }
    }
}

void FeedbackDetector::analyze() noexcept {
    // Windowed copy into the power buffer's storage would alias; use spectrum.
    std::array<float, kFftSize> windowed{};
    double energy = 0.0;
    for (std::size_t i = 0; i < kFftSize; ++i) {
        windowed[i] = frame_[i] * window_[i];
        energy += static_cast<double>(frame_[i]) * static_cast<double>(frame_[i]);
    }
    fft_->fft.fft(windowed.data(), fft_->spectrum.data());
    const double binHz = sampleRate_ / static_cast<double>(kFftSize);
    const auto lo = static_cast<std::size_t>(150.0 / binHz);
    const auto hi = std::min(kFftSize / 2 - 4, static_cast<std::size_t>(8000.0 / binHz));
    std::size_t peakBin = lo;
    float peakPower = 0.0F;
    double total = 0.0;
    for (std::size_t k = lo; k <= hi; ++k) {
        power_[k] = std::norm(fft_->spectrum[k]);
        total += static_cast<double>(power_[k]);
        if (power_[k] > peakPower) {
            peakPower = power_[k];
            peakBin = k;
        }
    }
    // Energy of the peak (with its window leakage) versus everything else.
    const auto peakEnergy =
        static_cast<double>(power_[peakBin - 1] + power_[peakBin] + power_[peakBin + 1]);
    const double rest = std::max(total - peakEnergy, 1e-20);
    const double dominanceDb = 10.0 * std::log10(peakEnergy / rest);
    const double levelDb =
        10.0 * std::log10(std::max(energy / static_cast<double>(kFftSize), 1e-20));
    const bool sameBin = peakBin + 1 >= lastBin_ && peakBin <= lastBin_ + 1;
    if (dominanceDb > 10.0 && levelDb > -30.0 && sameBin) {
        if (++stableFrames_ >= requiredFrames_) {
            detected_ = true;
            peakHz_ = static_cast<float>(static_cast<double>(peakBin) * binHz);
        }
    } else {
        stableFrames_ = 0;
    }
    lastBin_ = peakBin;
}

} // namespace vox::dsp
