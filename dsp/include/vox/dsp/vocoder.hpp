#pragma once

#include "vox/dsp/biquad.hpp"
#include "vox/dsp/envelope.hpp"
#include "vox/dsp/noise.hpp"
#include "vox/dsp/one_pole.hpp"
#include "vox/dsp/oscillator.hpp"
#include "vox/dsp/psola.hpp"
#include "vox/dsp/smoothed_value.hpp"
#include "vox/dsp/svf.hpp"

#include <array>
#include <span>
#include <vector>

namespace vox::dsp {

/// Channel vocoder with a built-in carrier. The voice's band envelopes shape
/// a synthesizer; the carrier can hold a fixed note or follow the speaker's
/// pitch (talking robot that keeps the intonation). High frequencies of the
/// input can be mixed back in so consonants stay intelligible.
class ChannelVocoder {
public:
    enum class Carrier { Saw, Square, Pulse, Noise, Chord };

    static constexpr int kMaxBands = 32;

    void prepare(double sampleRate, std::size_t maxBlockSize);
    void reset() noexcept;

    void setBands(int bands) noexcept;
    void setCarrier(Carrier carrier) noexcept { carrier_ = carrier; }
    /// Carrier frequency when not following pitch.
    void setCarrierHz(float hz) noexcept { carrierHz_ = hz; }
    /// Follow the input pitch, transposed by `semitones`.
    void setFollowPitch(bool follow, float semitones) noexcept;
    /// Shift the synthesis bands relative to the analysis bands.
    void setFormantSemitones(float semitones) noexcept;
    void setSibilance(float amount) noexcept { sibilance_ = amount; }
    void setMix(float mix) noexcept { mix_.setTarget(mix); }

    void process(std::span<float> block) noexcept;

private:
    void updateBands() noexcept;
    [[nodiscard]] float nextCarrier() noexcept;

    double sampleRate_ = 48000.0;
    int bands_ = 20;
    Carrier carrier_ = Carrier::Saw;
    float carrierHz_ = 110.0F;
    bool followPitch_ = false;
    float followRatio_ = 1.0F;
    float formantRatio_ = 1.0F;
    float sibilance_ = 0.3F;
    std::array<StateVariableFilter, kMaxBands> analysis_{};
    std::array<StateVariableFilter, kMaxBands> synthesis_{};
    std::array<EnvelopeFollower, kMaxBands> envelopes_{};
    std::array<Oscillator, 3> oscillators_{};
    FastRandom noise_;
    PitchTracker tracker_;
    OnePoleLowpass frequency_;
    Biquad sibilanceFilter_;
    SmoothedValue mix_;
    float gain_ = 1.0F;
};

/// Up to three PSOLA voices at fixed intervals over a shared analysis, with
/// the dry voice delayed to stay aligned (harmonies, choir, octave doubling).
class Harmonizer {
public:
    static constexpr int kMaxVoices = 3;

    void prepare(double sampleRate, std::size_t maxBlockSize);
    void reset() noexcept;

    void setVoice(int index, bool enabled, float semitones, float gainDb,
                  float formantSemitones) noexcept;
    void setDryGainDb(float db) noexcept { dry_.setTarget(db); }

    void process(std::span<float> block) noexcept;
    [[nodiscard]] std::size_t latencySamples() const noexcept;

private:
    struct VoiceSlot {
        PsolaVoice voice;
        bool enabled = false;
        SmoothedValue gainDb;
    };
    PsolaAnalyzer analyzer_;
    std::array<VoiceSlot, kMaxVoices> voices_{};
    std::vector<float> dryDelay_;
    std::size_t dryPos_ = 0;
    std::vector<float> scratch_;
    SmoothedValue dry_;
    std::size_t maxBlock_ = 2048;
};

} // namespace vox::dsp
