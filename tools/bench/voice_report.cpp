// Renders every built-in voice over the synthetic test phrase and prints its
// measurements as a Markdown table (docs/voices.md). The "gain" column is the
// output gain that would make the voice exactly as loud as the input.
// With --wav-dir DIR, also writes the input and every rendered voice as WAV
// files for tools/analysis/voice_spectrograms.py.

#include "voice_metrics.hpp"

#include <vox/plugins/voice_library.hpp>
#include <vox/testing/wav.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <tuple>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    std::filesystem::path wavDir;
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "--wav-dir") {
            wavDir = args[i + 1];
        }
    }
    if (!wavDir.empty()) {
        std::filesystem::create_directories(wavDir);
        const auto written =
            vox::testing::writeWav(wavDir / "input.wav", vox::plugins::test::testPhrase(), 48000);
        if (!written) {
            static_cast<void>(std::fprintf(stderr, "%s\n", written.error().message.c_str()));
            return 1;
        }
    }
    const auto& registry = vox::plugins::EffectRegistry::builtin();
    auto voices = vox::plugins::loadBuiltinVoices(registry);
    if (!voices) {
        static_cast<void>(std::fprintf(stderr, "%s\n", voices.error().message.c_str()));
        return 1;
    }
    std::printf("| Voice | Category | Pitch shift (st) | Output f0 (Hz) | Voiced | Aperiodicity | "
                "Brightness (x input) | Loudness change (LU) | Tail (dB) | Tail centroid (Hz) | "
                "Latency (ms) | "
                "CPU (x real time) | Suggested gain (dB) |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    std::vector<vox::plugins::test::VoiceMetrics> all;
    for (const auto& v : voices.value()) {
        std::vector<float> rendered;
        const auto m =
            vox::plugins::test::measureVoice(v, registry, wavDir.empty() ? nullptr : &rendered);
        all.push_back(m);
        if (!wavDir.empty()) {
            const auto written = vox::testing::writeWav(wavDir / (v.id + ".wav"), rendered, 48000);
            if (!written) {
                static_cast<void>(std::fprintf(stderr, "%s\n", written.error().message.c_str()));
                return 1;
            }
        }
        std::printf("| %s | %s | %+.2f | %.1f | %.0f %% | %.3f | %.2f | %+.1f | %.0f | %.0f | %.1f "
                    "| %.4f | %+.1f |\n",
                    v.name.c_str(), v.category.c_str(), m.pitchShiftSemitones, m.outputMedianF0,
                    m.voicedFraction * 100.0, m.aperiodicity, m.centroidRatio, m.loudnessChangeLu,
                    m.tailDb, m.tailCentroidHz, m.latencyMs, m.realtimeFactor,
                    static_cast<double>(v.outputGainDb) - m.loudnessChangeLu);
        static_cast<void>(std::fflush(stdout));
    }
    std::vector<std::tuple<double, std::string, std::string>> pairs;
    for (std::size_t a = 0; a < all.size(); ++a) {
        for (std::size_t b = a + 1; b < all.size(); ++b) {
            pairs.emplace_back(vox::plugins::test::voiceDistance(all[a], all[b]), all[a].id,
                               all[b].id);
        }
    }
    std::sort(pairs.begin(), pairs.end());
    std::printf("\nClosest pairs (feature distance):\n\n| Distance | Voices |\n|---|---|\n");
    for (std::size_t i = 0; i < std::min<std::size_t>(5, pairs.size()); ++i) {
        std::printf("| %.3f | %s / %s |\n", std::get<0>(pairs[i]), std::get<1>(pairs[i]).c_str(),
                    std::get<2>(pairs[i]).c_str());
    }
    return 0;
}
