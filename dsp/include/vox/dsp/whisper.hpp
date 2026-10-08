#pragma once

#include "vox/dsp/noise.hpp"
#include "vox/dsp/smoothed_value.hpp"

#include <array>
#include <span>
#include <vector>

namespace vox::dsp {

/// Turns voiced speech into a whisper: a running LPC analysis (order 32,
/// 30 ms window, updated every 5 ms) captures the vocal-tract envelope, and
/// white noise drives the matching all-pole lattice filter, so the formants
/// stay but the pitch disappears. Causal, no added latency.
class Whisper {
public:
    static constexpr int kOrder = 32;

    void prepare(double sampleRate);
    void reset() noexcept;
    void setMix(float mix) noexcept { mix_.setTarget(mix); }
    void process(std::span<float> block) noexcept;

private:
    void analyze() noexcept;

    double sampleRate_ = 48000.0;
    std::vector<float> history_; // pre-emphasized input ring
    std::size_t historyMask_ = 0;
    std::size_t written_ = 0;
    std::vector<float> window_;
    std::vector<float> frame_;
    std::size_t hop_ = 240;
    std::size_t sinceHop_ = 0;
    std::array<double, kOrder + 1> autocorr_{};
    std::array<double, kOrder + 1> lagWindow_{};
    std::array<float, kOrder> reflection_{};       // current, interpolated per sample
    std::array<float, kOrder> targetReflection_{}; // from the latest analysis
    std::array<float, kOrder> reflectionStep_{};
    std::array<float, kOrder + 1> lattice_{};
    float excitationGain_ = 0.0F;
    float targetGain_ = 0.0F;
    float gainStep_ = 0.0F;
    float preEmphasisState_ = 0.0F;
    float deEmphasisState_ = 0.0F;
    float inputPower_ = 0.0F;  // smoothed power of the dry input
    float outputPower_ = 0.0F; // smoothed power of the raw whisper
    float levelCoeff_ = 0.0F;
    float gainFallCoeff_ = 0.0F;
    float gainRiseCoeff_ = 0.0F;
    float levelGain_ = 1.0F;
    FastRandom noise_{0xC0FFEEU};
    SmoothedValue mix_;
};

} // namespace vox::dsp
