#pragma once

#include <vox/plugins/registry.hpp>
#include <vox/plugins/voice_preset.hpp>

#include <string>
#include <vector>

namespace vox::plugins::test {

/// Measurements of one voice rendered over the synthetic test phrase.
struct VoiceMetrics {
    std::string id;
    bool built = false;
    std::string error;
    double latencyMs = 0.0;
    double realtimeFactor = 0.0;
    bool finite = true;
    double peak = 0.0;
    double loudnessChangeLu = 0.0; ///< Output minus input integrated loudness.
    double pitchShiftSemitones = 0.0;
    double outputMedianF0 = 0.0;
    double voicedFraction = 0.0;
    double aperiodicity = 0.0;
    double centroidRatio = 1.0;  ///< Output / input spectral centroid.
    double highBandChange = 0.0; ///< Change of energy fraction above 4 kHz.
    double lowBandChange = 0.0;  ///< Change of energy fraction below 250 Hz.
    double tailDb = -120.0;      ///< Level in the final pause relative to the phrase.
    double tailCentroidHz = 0.0; ///< Spectral centroid of that pause (background character).
};

inline constexpr double kPhraseF0 = 120.0;

/// Renders `preset` over the test phrase and measures it. If `rendered` is
/// given it receives the output (latency removed).
[[nodiscard]] VoiceMetrics measureVoice(const VoicePreset& preset, const EffectRegistry& registry,
                                        std::vector<float>* rendered = nullptr);

/// The test phrase used by measureVoice (two phrases plus 0.6 s of silence).
[[nodiscard]] const std::vector<float>& testPhrase();

/// Weighted distance between the feature vectors of two voices.
[[nodiscard]] double voiceDistance(const VoiceMetrics& a, const VoiceMetrics& b);

} // namespace vox::plugins::test
