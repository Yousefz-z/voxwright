#pragma once

#include "vox/dsp/pitch_tracker.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::dsp {

/// How a PSOLA voice chooses its output pitch.
enum class PitchMode {
    Ratio,    ///< Output f0 = input f0 x ratio.
    Fixed,    ///< Output f0 is constant (monotone robot).
    Quantize, ///< Output f0 snaps to the nearest note of a scale (hard tune).
};

/// Shared analysis half of the pitch-synchronous overlap-add shifter: input
/// history, pitch tracking, and pitch-mark placement. Several synthesis
/// voices can read the same analysis (harmonizer, choir).
class PsolaAnalyzer {
public:
    struct Config {
        double sampleRate = 48000.0;
        float minHz = 75.0F;
        float maxHz = 800.0F;
        std::size_t maxBlockSize = 2048;
    };

    struct Mark {
        double position = 0.0; ///< Absolute input sample index (fractional).
        double period = 0.0;   ///< Local period in samples.
        bool voiced = false;
    };

    void prepare(const Config& config);
    void reset() noexcept;

    /// Appends input and places every pitch mark the new input allows.
    void write(std::span<const float> input) noexcept;

    /// Number of input samples written so far.
    [[nodiscard]] std::int64_t written() const noexcept { return written_; }
    /// Interpolated input sample at an absolute fractional position.
    [[nodiscard]] float sampleAt(double position) const noexcept;

    /// Latest mark whose grain (position + period) lies within the written
    /// input, nearest to `position`. Returns false if none is available.
    [[nodiscard]] bool nearestAvailableMark(double position, Mark& out) const noexcept;

    [[nodiscard]] double maxPeriod() const noexcept { return maxPeriod_; }
    [[nodiscard]] double unvoicedPeriod() const noexcept { return unvoicedPeriod_; }
    [[nodiscard]] double sampleRate() const noexcept { return config_.sampleRate; }
    [[nodiscard]] const PitchTracker& tracker() const noexcept { return tracker_; }

private:
    void placeMarks() noexcept;
    [[nodiscard]] double correlate(double from, double predicted, int radius,
                                   int half) const noexcept;
    [[nodiscard]] double onsetPeak(double from, double period) const noexcept;
    [[nodiscard]] float at(std::int64_t index) const noexcept {
        return history_[static_cast<std::size_t>(index) & historyMask_];
    }

    Config config_{};
    PitchTracker tracker_;
    std::vector<float> history_;
    std::size_t historyMask_ = 0;
    std::int64_t written_ = 0;
    double maxPeriod_ = 640.0;
    double minPeriod_ = 60.0;
    double unvoicedPeriod_ = 240.0;

    static constexpr std::size_t kMarkCapacity = 1024;
    std::array<Mark, kMarkCapacity> marks_{};
    std::size_t markHead_ = 0;  // index of the next mark to write
    std::size_t markCount_ = 0; // marks currently stored
    double lastMark_ = 0.0;
    bool lastVoiced_ = false;
};

/// Synthesis half: overlap-adds two-period grains at the target period.
/// Formants move independently by resampling each grain before it is added.
class PsolaVoice {
public:
    void prepare(const PsolaAnalyzer& analyzer);
    void reset() noexcept;

    void setPitchMode(PitchMode mode) noexcept { mode_ = mode; }
    /// Pitch ratio (Ratio mode) or transposition applied after the mode's
    /// mapping (Fixed and Quantize modes).
    void setPitchRatio(float ratio) noexcept;
    void setFormantRatio(float ratio) noexcept;
    void setFixedFrequency(float hz) noexcept { fixedHz_ = hz; }
    /// Scale as a 12-bit mask of allowed pitch classes relative to `key`
    /// (bit 0 = key). `retuneMs` 0 snaps instantly.
    void setScale(int key, std::uint16_t mask, float retuneMs) noexcept;

    /// Renders grains centred up to `horizon` (absolute position). Rendering
    /// lazily, just ahead of emission, lets each grain use the pitch mark
    /// nearest to its own time, so the delay stays equal to latency().
    void render(const PsolaAnalyzer& analyzer, double horizon) noexcept;

    /// Adds (or writes) output samples for absolute positions
    /// [start, start + out.size()) and clears them from the accumulator.
    void emit(std::span<float> out, std::int64_t start, float gain, bool accumulate) noexcept;

    /// Fixed delay between input and output.
    [[nodiscard]] std::int64_t latency() const noexcept { return latency_; }
    /// Output samples lost because a grain arrived after they were emitted
    /// (should stay 0; exposed for tests).
    [[nodiscard]] std::int64_t lateSamples() const noexcept { return lateSamples_; }

private:
    [[nodiscard]] double targetRatio(double inputPeriod, double spacingHint) noexcept;
    void addGrain(const PsolaAnalyzer& analyzer, const PsolaAnalyzer::Mark& mark, double centre,
                  double outputSpacing) noexcept;

    std::vector<float> accumulator_;
    std::size_t mask_ = 0;
    std::int64_t latency_ = 0;
    double sampleRate_ = 48000.0;
    double synthesisPosition_ = 0.0;
    std::int64_t emitted_ = 0;
    std::int64_t lateSamples_ = 0;

    PitchMode mode_ = PitchMode::Ratio;
    float pitchRatio_ = 1.0F;
    float formantRatio_ = 1.0F;
    float fixedHz_ = 110.0F;
    int key_ = 0;
    std::uint16_t scaleMask_ = 0x0FFF;
    float retuneMs_ = 0.0F;
    double currentNote_ = -1.0;
};

/// Formant-preserving pitch shifter with independent formant control and a
/// fixed latency of about two of the longest expected pitch periods.
class PsolaShifter {
public:
    struct Config {
        double sampleRate = 48000.0;
        float minHz = 75.0F;
        float maxHz = 800.0F;
        std::size_t maxBlockSize = 2048;
    };

    void prepare(const Config& config);
    void reset() noexcept;

    void setPitchSemitones(float semitones) noexcept;
    void setFormantSemitones(float semitones) noexcept;
    void setPitchMode(PitchMode mode) noexcept { voice_.setPitchMode(mode); }
    void setFixedFrequency(float hz) noexcept { voice_.setFixedFrequency(hz); }
    void setScale(int key, std::uint16_t mask, float retuneMs) noexcept {
        voice_.setScale(key, mask, retuneMs);
    }

    void process(std::span<float> block) noexcept;
    [[nodiscard]] std::size_t latencySamples() const noexcept {
        return static_cast<std::size_t>(voice_.latency());
    }
    [[nodiscard]] const PsolaAnalyzer& analyzer() const noexcept { return analyzer_; }
    [[nodiscard]] const PsolaVoice& voice() const noexcept { return voice_; }

private:
    PsolaAnalyzer analyzer_;
    PsolaVoice voice_;
    std::size_t maxBlock_ = 2048;
};

} // namespace vox::dsp
