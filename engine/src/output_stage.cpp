#include "vox/engine/output_stage.hpp"

#include <algorithm>
#include <cmath>

namespace vox::engine {
namespace {

constexpr double kMaxTrim = 0.005; // +-0.5 %, far beyond real clock differences

/// One operating point of the PI loop on the fill error (in seconds).
struct LoopGains {
    double proportional; ///< 1/s
    double integral;     ///< 1/s^2
    double errorSeconds; ///< smoothing of the measured error
    double trimSeconds;  ///< smoothing of the resulting trim
};

/// A critically damped loop with natural frequency `wn` (rad/s).
constexpr LoopGains gainsFor(double wn, double errorSeconds, double trimSeconds) {
    return {2.0 * wn, wn * wn, errorSeconds, trimSeconds};
}

// Acquisition, for the first seconds after a start or a refill: fast enough
// to absorb a 300 ppm clock difference within the 2 ms safety margin, at the
// cost of some ratio noise.
constexpr LoopGains kAcquire = gainsFor(0.4, 0.1, 0.05);
constexpr double kAcquireSeconds = 20.0;
// Tracking: slow and heavily smoothed so the producer's scheduling jitter
// does not reach the conversion ratio as phase noise. A 1 ms error moves the
// ratio by 200 ppm (0.35 cent); the smoothing poles (1 and 2 rad/s) sit well
// above the crossover (about 0.2 rad/s), leaving about 60 degrees of margin.
constexpr LoopGains kTrack = gainsFor(0.1, 1.0, 0.5);

// A device that has not read for this long is stalled; do not steer by it.
constexpr double kStalledSeconds = 0.1;

} // namespace

void OutputStage::prepare(double engineRate, double deviceRate, std::size_t targetFrames,
                          std::size_t maxEngineBlock) {
    engineRate_ = engineRate;
    deviceRate_ = deviceRate;
    nominalRatio_ = deviceRate / engineRate;
    target_ = std::max<std::size_t>(targetFrames, 16);
    resampler_.prepare(dsp::ResamplerQuality::Medium, nominalRatio_);
    resamplerDelay_ = dsp::measureHoldBack(resampler_);
    const auto maxConverted =
        static_cast<std::size_t>(
            std::ceil(static_cast<double>(maxEngineBlock) * nominalRatio_ * (1.0 + kMaxTrim))) +
        64;
    converted_.assign(maxConverted, 0.0F);
    mono_.assign(4096, 0.0F);
    ring_ = std::make_unique<SpscRingBuffer<float>>(
        std::max<std::size_t>(16 * (target_ + maxConverted), 16384));
    lastPushTime_ = -1.0;
    burstFrames_ = 0;
    lastBurst_.store(0, std::memory_order_relaxed);
    smoothedError_ = 0.0;
    haveError_ = false;
    seenRefills_ = refills_.load(std::memory_order_relaxed);
    controlledSeconds_ = 0.0;
    integralTrim_ = 0.0;
    trim_ = 1.0;
    lastPullTime_.store(-1.0, std::memory_order_relaxed);
    refilling_.store(true, std::memory_order_relaxed);
}

void OutputStage::control(double now) noexcept {
    const bool newCallback = now != lastPushTime_;
    const double dt = lastPushTime_ < 0.0 ? 0.0 : now - lastPushTime_;
    lastPushTime_ = now;
    if (synchronous_ || !newCallback || refilling_.load(std::memory_order_relaxed)) {
        return;
    }
    // A refill jumps the fill: restart the measurement filter from the new
    // value. The integral keeps its estimate of the clock difference.
    const std::uint64_t refills = refills_.load(std::memory_order_relaxed);
    if (refills != seenRefills_) {
        seenRefills_ = refills;
        haveError_ = false;
        controlledSeconds_ = 0.0;
    }
    const double lastPull = lastPullTime_.load(std::memory_order_relaxed);
    const double sincePull = now - lastPull;
    if (lastPull < 0.0 || sincePull < 0.0 || sincePull > kStalledSeconds || dt <= 0.0 ||
        dt > kStalledSeconds) {
        return;
    }
    const double continuousFill =
        static_cast<double>(ring_->availableToRead()) - sincePull * deviceRate_;
    const double error = (continuousFill - static_cast<double>(target_)) / deviceRate_;
    if (!haveError_) {
        smoothedError_ = error;
        haveError_ = true;
    }
    controlledSeconds_ += dt;
    const LoopGains& g = controlledSeconds_ < kAcquireSeconds ? kAcquire : kTrack;
    smoothedError_ += std::min(1.0, dt / g.errorSeconds) * (error - smoothedError_);
    // The integral is kept in trim units so switching gains does not bump it.
    integralTrim_ =
        std::clamp(integralTrim_ + g.integral * smoothedError_ * dt, -kMaxTrim, kMaxTrim);
    // Too full: produce fewer device frames per engine frame.
    const double wanted = std::clamp(1.0 - g.proportional * smoothedError_ - integralTrim_,
                                     1.0 - kMaxTrim, 1.0 + kMaxTrim);
    trim_ += std::min(1.0, dt / g.trimSeconds) * (wanted - trim_);
}

void OutputStage::push(std::span<const float> engineAudio, double now) noexcept {
    if (now != lastPushTime_) {
        burstFrames_ = 0;
    }
    control(now);
    resampler_.setRatio(nominalRatio_ * trim_);
    std::size_t offset = 0;
    while (offset < engineAudio.size()) {
        const auto counts = resampler_.process(engineAudio.subspan(offset), converted_);
        if (counts.consumed == 0 && counts.produced == 0) {
            break;
        }
        offset += counts.consumed;
        const std::size_t written =
            ring_->write(std::span<const float>(converted_).first(counts.produced));
        if (written < counts.produced) {
            overruns_.fetch_add(1, std::memory_order_relaxed);
        }
        burstFrames_ += written;
    }
    lastBurst_.store(burstFrames_, std::memory_order_relaxed);
}

void OutputStage::pull(float* interleaved, std::size_t frames, std::uint32_t channels,
                       double now) noexcept {
    const std::uint32_t ch = std::max<std::uint32_t>(1, channels);
    if (!ring_) {
        std::fill(interleaved, interleaved + frames * ch, 0.0F);
        return;
    }
    lastPullTime_.store(now, std::memory_order_relaxed);
    if (refilling_.load(std::memory_order_relaxed)) {
        const std::size_t needed =
            synchronous_ ? frames : target_ + frames + lastBurst_.load(std::memory_order_relaxed);
        if (ring_->availableToRead() >= needed) {
            refills_.fetch_add(1, std::memory_order_relaxed);
            refilling_.store(false, std::memory_order_relaxed);
        } else {
            std::fill(interleaved, interleaved + frames * ch, 0.0F);
            return;
        }
    }
    std::size_t done = 0;
    while (done < frames) {
        const std::size_t n = std::min(frames - done, mono_.size());
        const std::size_t got = ring_->read(std::span<float>(mono_).first(n));
        for (std::size_t i = 0; i < got; ++i) {
            for (std::uint32_t c = 0; c < ch; ++c) {
                interleaved[(done + i) * ch + c] = mono_[i];
            }
        }
        if (got < n) {
            std::fill(interleaved + (done + got) * ch, interleaved + frames * ch, 0.0F);
            underruns_.fetch_add(1, std::memory_order_relaxed);
            refilling_.store(true, std::memory_order_relaxed);
            return;
        }
        done += got;
    }
}

} // namespace vox::engine
