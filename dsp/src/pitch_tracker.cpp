#include "vox/dsp/pitch_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace vox::dsp {
namespace {

std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t p = 1;
    while (p < n) {
        p <<= 1U;
    }
    return p;
}

constexpr float kVoicedThreshold = 0.20F;   // enter voiced below this aperiodicity
constexpr float kUnvoicedThreshold = 0.35F; // leave voiced above this
constexpr float kDipThreshold = 0.15F;      // YIN absolute threshold
constexpr float kSilenceRms = 3.0e-4F;      // about -70 dBFS

} // namespace

void PitchTracker::prepare(const Config& config) {
    config_ = config;
    factor_ = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::lround(config.sampleRate / 12000.0)));
    const double decRate = config.sampleRate / static_cast<double>(factor_);
    // Fourth-order Butterworth anti-aliasing at 80 % of the decimated Nyquist.
    const double cutoff = 0.4 * decRate;
    antiAlias_[0].setCoefficients(
        designBiquad(BiquadType::Lowpass, config.sampleRate, cutoff, 0.5412));
    antiAlias_[1].setCoefficients(
        designBiquad(BiquadType::Lowpass, config.sampleRate, cutoff, 1.3066));

    maxLag_ = static_cast<std::size_t>(std::ceil(decRate / static_cast<double>(config.minHz)));
    minLag_ = std::max<std::size_t>(
        2, static_cast<std::size_t>(decRate / static_cast<double>(config.maxHz)));
    window_ = maxLag_;
    hop_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(decRate * 0.0027)));

    dec_.assign(nextPowerOfTwo(window_ + maxLag_ + 8), 0.0F);
    decMask_ = dec_.size() - 1;
    full_.assign(nextPowerOfTwo((window_ + maxLag_ + 8) * factor_), 0.0F);
    fullMask_ = full_.size() - 1;
    frame_.assign(window_ + maxLag_ + 2, 0.0F);
    diff_.assign(maxLag_ + 2, 0.0F);
    cmnd_.assign(maxLag_ + 2, 1.0F);
    reset();
}

void PitchTracker::reset() noexcept {
    for (Biquad& b : antiAlias_) {
        b.reset();
    }
    std::fill(dec_.begin(), dec_.end(), 0.0F);
    std::fill(full_.begin(), full_.end(), 0.0F);
    decCount_ = 0;
    fullCount_ = 0;
    phase_ = 0;
    sinceHop_ = 0;
    recentCount_ = 0;
    unvoicedRun_ = 0;
    voiced_ = false;
    aperiodicity_ = 1.0F;
    period_ = config_.sampleRate / 150.0;
}

float PitchTracker::decimated(std::size_t ago) const noexcept {
    return dec_[(decCount_ - 1 - ago) & decMask_];
}

float PitchTracker::fullRate(std::size_t ago) const noexcept {
    return full_[(fullCount_ - 1 - ago) & fullMask_];
}

void PitchTracker::push(std::span<const float> input) noexcept {
    for (const float x : input) {
        full_[fullCount_ & fullMask_] = x;
        ++fullCount_;
        const float filtered = antiAlias_[1].processSample(antiAlias_[0].processSample(x));
        if (++phase_ < factor_) {
            continue;
        }
        phase_ = 0;
        dec_[decCount_ & decMask_] = filtered;
        ++decCount_;
        if (++sinceHop_ >= hop_) {
            sinceHop_ = 0;
            if (decCount_ >= window_ + maxLag_ + 2) {
                analyze();
            }
        }
    }
}

void PitchTracker::analyze() noexcept {
    // Oldest sample first in frame_.
    const std::size_t total = window_ + maxLag_ + 2;
    double energy = 0.0;
    for (std::size_t i = 0; i < total; ++i) {
        frame_[i] = decimated(total - 1 - i);
        energy += static_cast<double>(frame_[i]) * static_cast<double>(frame_[i]);
    }
    const auto rmsLevel = static_cast<float>(std::sqrt(energy / static_cast<double>(total)));
    if (rmsLevel < kSilenceRms) {
        aperiodicity_ = 1.0F;
        voiced_ = false;
        recentCount_ = 0;
        return;
    }

    for (std::size_t tau = 1; tau <= maxLag_ + 1; ++tau) {
        float sum = 0.0F;
        for (std::size_t j = 0; j < window_; ++j) {
            const float d = frame_[j] - frame_[j + tau];
            sum += d * d;
        }
        diff_[tau] = sum;
    }
    float running = 0.0F;
    for (std::size_t tau = 1; tau <= maxLag_ + 1; ++tau) {
        running += diff_[tau];
        cmnd_[tau] = running > 0.0F ? diff_[tau] * static_cast<float>(tau) / running : 1.0F;
    }

    std::size_t best = 0;
    for (std::size_t tau = minLag_; tau <= maxLag_; ++tau) {
        if (cmnd_[tau] < kDipThreshold) {
            while (tau + 1 <= maxLag_ && cmnd_[tau + 1] < cmnd_[tau]) {
                ++tau;
            }
            best = tau;
            break;
        }
    }
    if (best == 0) {
        best = minLag_;
        for (std::size_t tau = minLag_ + 1; tau <= maxLag_; ++tau) {
            if (cmnd_[tau] < cmnd_[best]) {
                best = tau;
            }
        }
    }
    aperiodicity_ = cmnd_[best];

    const float enter = voiced_ ? kUnvoicedThreshold : kVoicedThreshold;
    if (aperiodicity_ > enter) {
        if (++unvoicedRun_ >= 2 || !voiced_) {
            voiced_ = false;
            recentCount_ = 0;
        }
        return;
    }
    unvoicedRun_ = 0;

    double shift = 0.0;
    {
        const auto a = static_cast<double>(diff_[best - 1]);
        const auto b = static_cast<double>(diff_[best]);
        const auto c = static_cast<double>(diff_[best + 1]);
        const double denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1e-12) {
            shift = std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
        }
    }
    const double coarse = (static_cast<double>(best) + shift) * static_cast<double>(factor_);
    const double refined = refineFullRate(coarse);

    recent_[recentCount_ % recent_.size()] = refined;
    ++recentCount_;
    if (recentCount_ >= recent_.size()) {
        std::array<double, 3> sorted = recent_;
        std::sort(sorted.begin(), sorted.end());
        period_ = sorted[1];
    } else {
        period_ = refined;
    }
    voiced_ = true;
}

double PitchTracker::refineFullRate(double coarseLag) const noexcept {
    const auto window = window_ * factor_;
    const auto center = static_cast<std::size_t>(std::lround(coarseLag));
    const std::size_t radius = factor_ + 1;
    const std::size_t lo = center > radius + 1 ? center - radius : 2;
    const std::size_t hi = std::min(center + radius, (maxLag_ + 1) * factor_);
    if (hi <= lo + 2 || window + hi + 2 > full_.size()) {
        return coarseLag;
    }
    // Squared difference d(tau) between the latest window and its lagged copy.
    auto difference = [&](std::size_t tau) {
        double sum = 0.0;
        for (std::size_t j = 0; j < window; ++j) {
            const double d =
                static_cast<double>(fullRate(j)) - static_cast<double>(fullRate(j + tau));
            sum += d * d;
        }
        return sum;
    };
    std::size_t best = lo;
    double bestValue = difference(lo);
    double below = bestValue;
    double above = bestValue;
    double previous = bestValue;
    for (std::size_t tau = lo + 1; tau <= hi; ++tau) {
        const double v = difference(tau);
        if (v < bestValue) {
            bestValue = v;
            best = tau;
            below = previous;
            above = difference(tau + 1);
        }
        previous = v;
    }
    if (best == lo || best == hi) {
        return coarseLag;
    }
    const double denom = below - 2.0 * bestValue + above;
    const double shift =
        std::abs(denom) > 1e-18 ? std::clamp(0.5 * (below - above) / denom, -0.5, 0.5) : 0.0;
    return static_cast<double>(best) + shift;
}

} // namespace vox::dsp
