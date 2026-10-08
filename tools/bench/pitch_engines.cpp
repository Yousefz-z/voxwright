// Compares the PSOLA and spectral pitch engines on synthetic vowels with known
// pitch and formants. Prints a Markdown table used in docs/architecture.md.

#include <vox/dsp/psola.hpp>
#include <vox/dsp/spectral_shifter.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/render.hpp>
#include <vox/testing/signals.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace {

constexpr double kFs = 48000.0;

struct Metrics {
    double meanAbsCents = 0.0;
    double maxAbsCents = 0.0;
    double aperiodicity = 0.0;
    double voicedFraction = 0.0;
    double envelopeErrorDb = 0.0;
    double latencyMs = 0.0;
    double realtimeFactor = 0.0;
};

struct Engine {
    std::string name;
    std::function<std::size_t(float pitch, float formant, std::vector<float>& audio)> run;
};

std::vector<float> vowel(double f0, const vox::testing::Vowel& v, double seconds) {
    vox::testing::VoiceSpec spec;
    spec.f0Hz = f0;
    spec.vowel = v;
    spec.seconds = seconds;
    spec.jitter = 0.002;
    return vox::testing::synthVoice(spec);
}

template <class Shifter>
std::size_t process(Shifter& s, std::vector<float>& audio) {
    audio = vox::testing::renderOffline(s, audio, 128);
    return s.latencySamples();
}

Metrics measure(const Engine& engine, float semitones) {
    Metrics m;
    const std::vector<double> f0s{100.0, 180.0, 260.0};
    const std::vector<vox::testing::Vowel> vowels{vox::testing::vowelA(), vox::testing::vowelI(),
                                                  vox::testing::vowelU()};
    int cases = 0;
    double processSeconds = 0.0;
    double audioSeconds = 0.0;
    for (const double f0 : f0s) {
        for (const auto& v : vowels) {
            auto audio = vowel(f0, v, 1.6);
            if (engine.name.starts_with("Reference")) {
                vox::testing::VoiceSpec spec;
                spec.f0Hz = f0 * std::exp2(static_cast<double>(semitones) / 12.0);
                spec.vowel = v;
                spec.seconds = 1.6;
                spec.jitter = 0.002;
                spec.seed = 99;
                audio = vox::testing::synthVoice(spec);
            }
            const auto t0 = std::chrono::steady_clock::now();
            const std::size_t latency = engine.run(semitones, 0.0F, audio);
            processSeconds +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            audioSeconds += 1.6;
            const std::size_t start = latency + static_cast<std::size_t>(0.3 * kFs);
            const auto steady =
                std::span<const float>(audio).subspan(start, audio.size() - start - 4800);
            const double expected = f0 * std::exp2(static_cast<double>(semitones) / 12.0);
            const auto est = vox::testing::estimateF0(steady, kFs, 40.0, 1100.0);
            const double cents =
                est.hz > 0.0 ? std::abs(vox::testing::centsBetween(est.hz, expected)) : 1200.0;
            m.meanAbsCents += cents;
            m.maxAbsCents = std::max(m.maxAbsCents, cents);
            const auto track = vox::testing::f0Track(steady, kFs, 40.0, 1100.0);
            std::size_t voiced = 0;
            for (const double f : track) {
                voiced += f > 0.0 ? 1U : 0U;
            }
            m.voicedFraction += static_cast<double>(voiced) / static_cast<double>(track.size());
            m.aperiodicity +=
                vox::testing::estimateF0(steady.first(9600), kFs, 40.0, 1100.0).aperiodicity;
            // Ideal result: the same vowel synthesized directly at the target pitch.
            const auto ideal = vowel(expected, v, 1.0);
            const auto envIdeal = vox::testing::lpcEnvelopeDb(
                std::span<const float>(ideal).subspan(4800, 38400), kFs, 44);
            const auto envOut = vox::testing::lpcEnvelopeDb(steady, kFs, 44);
            m.envelopeErrorDb +=
                vox::testing::envelopeDistanceDb(envIdeal, envOut, kFs, 200.0, 5000.0);
            m.latencyMs = static_cast<double>(latency) / kFs * 1000.0;
            ++cases;
        }
    }
    m.meanAbsCents /= cases;
    m.voicedFraction /= cases;
    m.aperiodicity /= cases;
    m.envelopeErrorDb /= cases;
    m.realtimeFactor = processSeconds / audioSeconds;
    return m;
}

} // namespace

int main() {
    std::vector<Engine> engines;
    // Measurement floor: the input is replaced by a direct synthesis at the
    // target pitch (different jitter), i.e. a perfect formant-preserving shift.
    engines.push_back({"Reference: ideal shift", [](float, float, std::vector<float>& a) {
                           static_cast<void>(a);
                           return std::size_t{0};
                       }});
    engines.push_back({"PSOLA (min f0 75 Hz)", [](float p, float f, std::vector<float>& a) {
                           vox::dsp::PsolaShifter s;
                           s.prepare({kFs, 75.0F, 800.0F, 2048});
                           s.setPitchSemitones(p);
                           s.setFormantSemitones(f);
                           return process(s, a);
                       }});
    const std::vector<std::pair<std::string, vox::dsp::SpectralShifter::Quality>> qualities{
        {"Spectral 20 ms", vox::dsp::SpectralShifter::Quality::LowLatency},
        {"Spectral 30 ms", vox::dsp::SpectralShifter::Quality::Balanced},
        {"Spectral 43 ms", vox::dsp::SpectralShifter::Quality::High},
        {"Spectral 120 ms", vox::dsp::SpectralShifter::Quality::Studio}};
    for (const auto& [name, quality] : qualities) {
        for (const bool preserve : {true, false}) {
            const auto q = quality;
            engines.push_back({name + (preserve ? ", formants kept" : ", formants move"),
                               [q, preserve](float p, float f, std::vector<float>& a) {
                                   vox::dsp::SpectralShifter s;
                                   s.prepare({kFs, 2048, q});
                                   s.setPreserveFormants(preserve);
                                   s.setPitchSemitones(p);
                                   s.setFormantSemitones(f);
                                   return process(s, a);
                               }});
        }
    }

    std::printf("Synthetic vowels /a/ /i/ /u/ at f0 100, 180, 260 Hz; 9 cases per row.\n\n");
    std::printf("| Engine | Shift (st) | Mean pitch error (cents) | Max pitch error (cents) | "
                "Voiced frames | YIN aperiodicity | Envelope error vs ideal (dB) | Latency (ms) | "
                "CPU (x real time) |\n");
    std::printf("|---|---|---|---|---|---|---|---|---|\n");
    for (const auto& engine : engines) {
        for (const float shift : {-12.0F, -5.0F, 5.0F, 12.0F}) {
            const Metrics m = measure(engine, shift);
            std::printf("| %s | %+.0f | %.1f | %.1f | %.0f %% | %.3f | %.1f | %.1f | %.4f |\n",
                        engine.name.c_str(), static_cast<double>(shift), m.meanAbsCents,
                        m.maxAbsCents, m.voicedFraction * 100.0, m.aperiodicity, m.envelopeErrorDb,
                        m.latencyMs, m.realtimeFactor);
            static_cast<void>(std::fflush(stdout));
        }
    }
    return 0;
}
