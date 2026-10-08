// Measures the real-time engine for docs/architecture.md: processing cost
// per block, the latency budget for several device period sizes (through
// the fake backend, in simulated time), and drift compensation.

#include "drift_simulation.hpp"

#include <vox/devices/fake_backend.hpp>
#include <vox/engine/audio_engine.hpp>
#include <vox/plugins/voice_edit.hpp>
#include <vox/plugins/voice_library.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using vox::devices::DeviceKind;
using vox::engine::AudioEngine;
using vox::engine::DeviceSelection;
using vox::engine::EngineConfig;

const vox::plugins::VoicePreset* findVoice(const std::vector<vox::plugins::VoicePreset>& voices,
                                           const std::string& id) {
    const auto it =
        std::find_if(voices.begin(), voices.end(), [&id](const auto& v) { return v.id == id; });
    return it == voices.end() ? nullptr : &*it;
}

struct Rig {
    explicit Rig(std::uint32_t period)
        : engine(backend, configFor(period)) {
        backend.addDevice({"mic", "Microphone", DeviceKind::Capture, true, 48000, 1, false});
        backend.addDevice({"cable", "CABLE Input", DeviceKind::Playback, false, 48000, 2, false});
        backend.addDevice({"phones", "Headphones", DeviceKind::Playback, true, 48000, 2, false});
        selection.inputId = "mic";
        selection.virtualMicId = "cable";
        selection.monitorId = "phones";
    }
    static EngineConfig configFor(std::uint32_t period) {
        EngineConfig c;
        c.periodFrames = period;
        return c;
    }

    /// Lock-step I/O in simulated time; returns the virtual mic output.
    std::vector<float> pump(const std::vector<float>& input, std::size_t period) {
        std::vector<float> out;
        double time = 0.0;
        std::vector<float> block(period);
        for (std::size_t pos = 0; pos + period <= input.size(); pos += period) {
            backend.setTime(time);
            std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(pos), period, block.begin());
            static_cast<void>(backend.pumpCapture("mic", block));
            time += static_cast<double>(period) / 48000.0 * 0.5;
            backend.setTime(time);
            if (const auto played = backend.pumpPlayback("cable", period)) {
                for (std::size_t i = 0; i < period; ++i) {
                    out.push_back((*played)[i * 2]);
                }
            }
            static_cast<void>(backend.pumpPlayback("phones", period));
            time += static_cast<double>(period) / 48000.0 * 0.5;
        }
        return out;
    }

    vox::devices::FakeBackend backend;
    AudioEngine engine;
    DeviceSelection selection;
};

/// The largest voice the designer allows, built from the most expensive
/// effects (vox::plugins::kMaxVoiceBlocks of them).
vox::plugins::VoicePreset designerMaximum() {
    vox::plugins::VoicePreset v;
    v.id = "custom-bench";
    v.name = "Designer maximum";
    v.category = "Character";
    const auto& registry = vox::plugins::EffectRegistry::builtin();
    for (const char* effect : {"pitch", "harmonizer", "vocoder", "reverb", "whisper", "chorus",
                               "flanger", "phaser", "echo", "distortion", "filter", "comb"}) {
        static_cast<void>(vox::plugins::insertBlock(v, v.blocks.size(), effect, registry));
    }
    return v;
}

void blockCost(std::vector<vox::plugins::VoicePreset> voices) {
    voices.push_back(designerMaximum());
    std::printf("### Processing cost per 128-frame block (2.67 ms of audio)\n\n");
    std::printf("| Configuration | Mean (%% of block) | 99th percentile (%% of block) | Worst "
                "(%% of block) |\n|---|---|---|---|\n");
    const auto phrase = vox::testing::synthPhrase(120.0).audio;
    struct Scenario {
        const char* label;
        const char* voice;
        bool suppression;
    };
    const auto& registry = vox::plugins::EffectRegistry::builtin();
    for (const Scenario sc :
         {Scenario{"No voice", nullptr, false},
          Scenario{"No voice, noise reduction on", nullptr, true},
          Scenario{"Clean Voice", "clean-voice", false},
          Scenario{"Deep Baritone (PSOLA)", "deep-baritone", true},
          Scenario{"Choir Bot (vocoder + harmonizer)", "choir-bot", true},
          Scenario{"Cathedral (reverb)", "cathedral", true},
          Scenario{"Designer maximum (12 heavy effects)", "custom-bench", true}}) {
        vox::engine::ProcessingGraph graph(EngineConfig{});
        if (sc.voice != nullptr) {
            const auto* preset = findVoice(voices, sc.voice);
            if (preset == nullptr) {
                continue;
            }
            vox::plugins::PrepareContext context;
            context.maxBlockSize = graph.maxBlock();
            auto chain = vox::plugins::VoiceChain::build(*preset, {}, registry, context);
            if (!chain) {
                continue;
            }
            static_cast<void>(graph.setVoiceChain(std::move(chain).value()));
        }
        vox::engine::Command nr;
        nr.type = vox::engine::Command::Type::NoiseSuppression;
        nr.a = sc.suppression ? 1U : 0U;
        nr.x = 1.0F;
        static_cast<void>(graph.send(nr));
        std::vector<float> mic(128);
        std::vector<float> monitor(128);
        std::vector<double> times;
        for (int rep = 0; rep < 4; ++rep) {
            for (std::size_t pos = 0; pos + 128 <= phrase.size(); pos += 128) {
                const auto t0 = std::chrono::steady_clock::now();
                graph.process(std::span<const float>(phrase).subspan(pos, 128), mic, monitor);
                times.push_back(
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            }
        }
        std::sort(times.begin(), times.end());
        double sum = 0.0;
        for (const double t : times) {
            sum += t;
        }
        const double block = 128.0 / 48000.0;
        std::printf("| %s | %.1f | %.1f | %.1f |\n", sc.label,
                    100.0 * sum / static_cast<double>(times.size()) / block,
                    100.0 * times[times.size() * 99 / 100] / block, 100.0 * times.back() / block);
    }
}

void latency(const std::vector<vox::plugins::VoicePreset>& voices) {
    std::printf("\n### Latency, microphone to virtual microphone (engine side)\n\n");
    std::printf("| Period (frames) | Voice | Noise reduction | Capture | Noise red. | Voice | "
                "Limiter | Buffer | Resampler | Playback | Estimate (ms) | Measured, lock-step "
                "(ms) |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    const auto& registry = vox::plugins::EffectRegistry::builtin();
    for (const std::uint32_t period : {64U, 128U, 256U, 480U}) {
        for (const bool heavy : {false, true}) {
            Rig rig(period);
            if (!rig.engine.start(rig.selection)) {
                continue;
            }
            if (heavy) {
                const auto* preset = findVoice(voices, "deep-baritone");
                if (preset == nullptr || !rig.engine.setVoice(*preset, {}, registry) ||
                    !rig.engine.setNoiseSuppression(true, 1.0F)) {
                    continue;
                }
            }
            // Settle the drift controller on quiet input, then measure on noise.
            auto input = vox::testing::whiteNoise(30.0, 48000.0, 0.001F, 3);
            const std::size_t start = input.size();
            const auto noise = vox::testing::whiteNoise(1.0, 48000.0, 0.2F, 11);
            input.insert(input.end(), noise.begin(), noise.end());
            const auto out = rig.pump(input, period);
            const std::size_t lag =
                vox::testing::estimateLag(std::span<const float>(input).subspan(start, 40000),
                                          std::span<const float>(out).subspan(start, 40000), 4800);
            const auto s = rig.engine.stats();
            const auto& b = s.virtualMicLatency;
            std::printf("| %u | %s | %s | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f | %.1f | "
                        "%.1f |\n",
                        period, heavy ? "Deep Baritone" : "none", heavy ? "on" : "off", b.captureMs,
                        b.noiseSuppressionMs, b.voiceMs, b.limiterMs, b.bufferMs,
                        b.outputResamplerMs, b.playbackMs, b.totalMs(),
                        static_cast<double>(lag) / 48.0);
        }
    }
}

void drift() {
    std::printf("\n### Drift compensation (90 s simulated, 1.5 ms producer jitter)\n\n");
    std::printf("| Device rate | Producer/device block | Clock error (ppm) | Underruns | "
                "Overruns | Trim found (ppm) | Trim spread (ppm) | Mean buffer (ms) | "
                "Tone-to-noise (dB) |\n|---|---|---|---|---|---|---|---|---|\n");
    struct Case {
        double rate;
        std::size_t producer;
        std::size_t device;
    };
    for (const Case c : {Case{48000.0, 128, 128}, Case{44100.0, 128, 441}, Case{48000.0, 480, 128},
                         Case{96000.0, 256, 512}}) {
        for (const double ppm : {-300.0, 0.0, 300.0}) {
            const auto r =
                vox::engine::test::simulateDrift(c.rate, ppm, 90.0, c.producer, c.device);
            std::printf(
                "| %.0f | %zu/%zu | %+.0f | %llu | %llu | %+.1f | %.1f | %.2f | %.1f |\n", c.rate,
                c.producer, c.device, ppm, static_cast<unsigned long long>(r.underruns),
                static_cast<unsigned long long>(r.overruns), r.meanTrimPpm, r.trimSpreadPpm,
                r.meanFillMs, vox::engine::test::toneToNoiseDb(r.lastSecond, 1000.0, c.rate));
            static_cast<void>(std::fflush(stdout));
        }
    }
}

} // namespace

int main() {
    const auto& registry = vox::plugins::EffectRegistry::builtin();
    auto voices = vox::plugins::loadBuiltinVoices(registry);
    if (!voices) {
        static_cast<void>(std::fprintf(stderr, "%s\n", voices.error().message.c_str()));
        return 1;
    }
    blockCost(voices.value());
    latency(voices.value());
    drift();
    return 0;
}
