#pragma once

#include "vox/dsp/envelope.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace vox::dsp {

/// Feed-forward compressor with soft knee and RMS detection.
class Compressor {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setThresholdDb(float db) noexcept { thresholdDb_ = db; }
    void setRatio(float ratio) noexcept { ratio_ = std::max(ratio, 1.0F); }
    void setKneeDb(float db) noexcept { kneeDb_ = std::max(db, 0.0F); }
    void setTimes(float attackMs, float releaseMs) noexcept;
    void setMakeupDb(float db) noexcept { makeupDb_ = db; }
    void process(std::span<float> block) noexcept;

    /// Static gain computer, exposed for tests: gain change in dB for an
    /// input level in dB.
    [[nodiscard]] float gainReductionDb(float inputDb) const noexcept;
    [[nodiscard]] float currentReductionDb() const noexcept { return reductionDb_; }

private:
    double sampleRate_ = 48000.0;
    EnvelopeFollower detector_;
    float thresholdDb_ = -18.0F;
    float ratio_ = 3.0F;
    float kneeDb_ = 6.0F;
    float makeupDb_ = 0.0F;
    float attack_ = 0.0F;
    float release_ = 0.0F;
    float reductionDb_ = 0.0F;
};

/// Look-ahead brickwall peak limiter. The gain envelope is a running minimum
/// of the required gain followed by a moving average of the same length, so
/// the gain is already down when a peak arrives; a final clamp guarantees no
/// sample ever exceeds the ceiling.
class Limiter {
public:
    void prepare(double sampleRate, float lookaheadMs = 1.5F);
    void reset() noexcept;
    void setCeilingDb(float db) noexcept;
    void setReleaseMs(float ms) noexcept;
    void process(std::span<float> block) noexcept;

    [[nodiscard]] std::size_t latencySamples() const noexcept { return lookahead_ - 1; }
    [[nodiscard]] float currentGainDb() const noexcept;

private:
    double sampleRate_ = 48000.0;
    std::size_t lookahead_ = 72;
    std::vector<float> delay_;
    std::vector<float> required_;
    std::vector<std::size_t> minQueue_; // monotonic deque of indices into required_
    std::size_t queueHead_ = 0;
    std::size_t queueTail_ = 0;
    std::vector<float> average_;
    double averageSum_ = 0.0;
    std::size_t index_ = 0;
    std::size_t recompute_ = 0;
    float ceiling_ = 0.97F;
    float release_ = 0.0F;
    float held_ = 1.0F;
    float lastGain_ = 1.0F;
};

/// Noise gate with hysteresis, hold, and a configurable floor ("range").
class NoiseGate {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setThresholdDb(float db) noexcept { thresholdDb_ = db; }
    void setHysteresisDb(float db) noexcept { hysteresisDb_ = std::max(db, 0.0F); }
    void setRangeDb(float db) noexcept { rangeDb_ = std::min(db, 0.0F); }
    void setTimes(float attackMs, float holdMs, float releaseMs) noexcept;
    void process(std::span<float> block) noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] float currentGain() const noexcept { return gain_; }

private:
    double sampleRate_ = 48000.0;
    EnvelopeFollower detector_;
    float thresholdDb_ = -50.0F;
    float hysteresisDb_ = 6.0F;
    float rangeDb_ = -80.0F;
    float attack_ = 0.0F;
    float release_ = 0.0F;
    int holdSamples_ = 0;
    int holdCounter_ = 0;
    bool open_ = false;
    float gain_ = 0.0F;
};

} // namespace vox::dsp
