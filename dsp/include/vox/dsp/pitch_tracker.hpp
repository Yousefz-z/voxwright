#pragma once

#include "vox/dsp/biquad.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::dsp {

/// Streaming fundamental-frequency tracker for speech.
///
/// Runs the YIN cumulative-mean-normalized difference function on a 4x
/// decimated copy of the input every ~2.7 ms, then refines the best lag on
/// the full-rate signal with parabolic interpolation, so the period estimate
/// has sub-sample precision. A median of the last three voiced estimates
/// removes isolated octave errors. All memory is allocated in prepare().
class PitchTracker {
public:
    struct Config {
        double sampleRate = 48000.0;
        float minHz = 75.0F;
        float maxHz = 800.0F;
    };

    void prepare(const Config& config);
    void reset() noexcept;

    /// Feeds new input samples; analysis runs internally every hop.
    void push(std::span<const float> input) noexcept;

    /// Latest period estimate in full-rate samples (fractional). Holds the
    /// last voiced value while unvoiced.
    [[nodiscard]] double period() const noexcept { return period_; }
    [[nodiscard]] double frequencyHz() const noexcept { return config_.sampleRate / period_; }
    [[nodiscard]] bool voiced() const noexcept { return voiced_; }
    /// YIN aperiodicity of the last analysis (0 = perfectly periodic).
    [[nodiscard]] float aperiodicity() const noexcept { return aperiodicity_; }

    [[nodiscard]] double maxPeriod() const noexcept {
        return config_.sampleRate / static_cast<double>(config_.minHz);
    }
    [[nodiscard]] double minPeriod() const noexcept {
        return config_.sampleRate / static_cast<double>(config_.maxHz);
    }

private:
    void analyze() noexcept;
    [[nodiscard]] float decimated(std::size_t ago) const noexcept;
    [[nodiscard]] float fullRate(std::size_t ago) const noexcept;
    [[nodiscard]] double refineFullRate(double coarseLag) const noexcept;

    Config config_{};
    std::size_t factor_ = 4;
    std::array<Biquad, 2> antiAlias_{};
    std::size_t phase_ = 0;

    std::vector<float> dec_; // decimated history ring
    std::size_t decMask_ = 0;
    std::uint64_t decCount_ = 0;
    std::vector<float> full_; // full-rate history ring
    std::size_t fullMask_ = 0;
    std::uint64_t fullCount_ = 0;

    std::size_t minLag_ = 0;
    std::size_t maxLag_ = 0;
    std::size_t window_ = 0;
    std::size_t hop_ = 32;
    std::size_t sinceHop_ = 0;
    std::vector<float> frame_; // contiguous copy of the analysis frame
    std::vector<float> diff_;
    std::vector<float> cmnd_;

    std::array<double, 3> recent_{};
    std::size_t recentCount_ = 0;
    int unvoicedRun_ = 0;
    double period_ = 400.0;
    bool voiced_ = false;
    float aperiodicity_ = 1.0F;
};

} // namespace vox::dsp
