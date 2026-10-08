#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace vox::dsp {

/// Phase-vocoder transposer built on Signalsmith Stretch.
///
/// Measured in docs/architecture.md: it needs analysis blocks several pitch
/// periods long (its 120 ms default) to be accurate on speech, and its formant
/// compensation does not hold close vowel formants in place. Voices therefore
/// use PsolaShifter; this class transposes soundboard clips offline, where
/// latency does not matter and the input is often polyphonic.
class SpectralShifter {
public:
    enum class Quality {
        LowLatency, ///< 20 ms analysis block
        Balanced,   ///< 30 ms analysis block
        High,       ///< 43 ms analysis block
        Studio,     ///< 120 ms block, the library's own default; for offline-style use
    };

    struct Config {
        double sampleRate = 48000.0;
        std::size_t maxBlockSize = 2048;
        Quality quality = Quality::Studio;
    };

    SpectralShifter();
    ~SpectralShifter();
    SpectralShifter(const SpectralShifter&) = delete;
    SpectralShifter& operator=(const SpectralShifter&) = delete;
    SpectralShifter(SpectralShifter&&) noexcept;
    SpectralShifter& operator=(SpectralShifter&&) noexcept;

    void prepare(const Config& config);
    void reset() noexcept;

    void setPitchSemitones(float semitones) noexcept;
    /// Formant shift on top of the pitch shift. With preservation on, 0 keeps
    /// the original formants (off by default, see the class comment).
    void setFormantSemitones(float semitones) noexcept;
    void setPreserveFormants(bool preserve) noexcept;

    void process(std::span<float> block) noexcept;
    [[nodiscard]] std::size_t latencySamples() const noexcept;

private:
    void applyFormants() noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<float> scratch_;
    float formantSemitones_ = 0.0F;
    bool preserveFormants_ = false;
};

} // namespace vox::dsp
