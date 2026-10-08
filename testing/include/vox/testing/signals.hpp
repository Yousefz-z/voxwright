#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace vox::testing {

inline constexpr double kRate = 48000.0;

[[nodiscard]] std::vector<float> sine(double freqHz, double seconds, double sampleRate = kRate,
                                      float amplitude = 0.5F);
[[nodiscard]] std::vector<float> logSweep(double startHz, double endHz, double seconds,
                                          double sampleRate = kRate, float amplitude = 0.5F);
[[nodiscard]] std::vector<float> whiteNoise(double seconds, double sampleRate = kRate,
                                            float amplitude = 0.5F, std::uint32_t seed = 1);
[[nodiscard]] std::vector<float> silence(double seconds, double sampleRate = kRate);

/// Formant frequencies and bandwidths of a vowel (adult male averages).
struct Vowel {
    std::array<double, 5> formantsHz;
    std::array<double, 5> bandwidthsHz;
};

[[nodiscard]] Vowel vowelA();
[[nodiscard]] Vowel vowelE();
[[nodiscard]] Vowel vowelI();
[[nodiscard]] Vowel vowelO();
[[nodiscard]] Vowel vowelU();

/// Parameters of a synthetic sustained vowel: a Rosenberg glottal pulse train
/// through a cascade of formant resonators and lip radiation. Pitch and
/// formants are known exactly, which makes pitch and formant accuracy
/// measurable without recordings.
struct VoiceSpec {
    double f0Hz = 120.0;
    double f0EndHz = 0.0; ///< Glide target; 0 keeps f0 constant.
    double vibratoHz = 0.0;
    double vibratoCents = 0.0;
    double jitter = 0.0;      ///< Relative random period perturbation (0.005 = 0.5 %).
    double breathiness = 0.0; ///< Aspiration noise level relative to the pulse.
    Vowel vowel = vowelA();
    double seconds = 1.0;
    float peak = 0.5F;
    std::uint32_t seed = 1;
};

[[nodiscard]] std::vector<float> synthVoice(const VoiceSpec& spec, double sampleRate = kRate);

/// A speech-like phrase (vowels with moving pitch, fricatives, pauses) plus
/// the per-sample ground-truth f0 (0 where unvoiced).
struct Phrase {
    std::vector<float> audio;
    std::vector<float> f0;
};

[[nodiscard]] Phrase synthPhrase(double baseF0Hz, double sampleRate = kRate,
                                 std::uint32_t seed = 7);

} // namespace vox::testing
