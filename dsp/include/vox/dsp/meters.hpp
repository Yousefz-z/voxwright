#pragma once

#include "vox/dsp/biquad.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace vox::dsp {

/// Peak (instant attack, 20 dB/s fall) and RMS (300 ms) level meter for UI
/// display. Values are linear amplitudes.
class LevelMeter {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void process(std::span<const float> block) noexcept;
    [[nodiscard]] float peak() const noexcept { return peak_; }
    [[nodiscard]] float rms() const noexcept;

private:
    float peak_ = 0.0F;
    float meanSquare_ = 0.0F;
    float peakFall_ = 0.0F;
    float rmsCoeff_ = 0.0F;
};

/// ITU-R BS.1770-4 K-weighting filter (two biquads), valid at any rate.
class KWeighting {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    [[nodiscard]] float processSample(float x) noexcept {
        return highpass_.processSample(shelf_.processSample(x));
    }

private:
    Biquad shelf_;
    Biquad highpass_;
};

/// Integrated loudness (LUFS) of a mono signal per ITU-R BS.1770-4 with the
/// absolute (-70 LUFS) and relative (-10 LU) gates. Returns -inf for silence.
[[nodiscard]] double integratedLoudness(std::span<const float> mono, double sampleRate);

/// Detects acoustic feedback (howl) on a signal: a single spectral peak that
/// dominates its neighbourhood, stays at the same frequency, and is loud,
/// for longer than speech or singing normally holds a pure tone.
class FeedbackDetector {
public:
    FeedbackDetector();
    ~FeedbackDetector();
    FeedbackDetector(const FeedbackDetector&) = delete;
    FeedbackDetector& operator=(const FeedbackDetector&) = delete;
    FeedbackDetector(FeedbackDetector&&) noexcept;
    FeedbackDetector& operator=(FeedbackDetector&&) noexcept;

    void prepare(double sampleRate);
    void reset() noexcept;
    void process(std::span<const float> block) noexcept;

    [[nodiscard]] bool feedbackDetected() const noexcept { return detected_; }
    [[nodiscard]] float peakFrequencyHz() const noexcept { return peakHz_; }
    void acknowledge() noexcept;

private:
    void analyze() noexcept;

    static constexpr std::size_t kFftSize = 2048;
    struct Fft;
    std::unique_ptr<Fft> fft_;
    double sampleRate_ = 48000.0;
    std::vector<float> frame_;
    std::vector<float> window_;
    std::vector<float> power_;
    std::size_t fill_ = 0;
    std::size_t lastBin_ = 0;
    int stableFrames_ = 0;
    int requiredFrames_ = 20;
    bool detected_ = false;
    float peakHz_ = 0.0F;
};

} // namespace vox::dsp
