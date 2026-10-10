#include "engine_test_helpers.hpp"

#include <vox/engine/processing_graph.hpp>
#include <vox/plugins/voice_library.hpp>
#include <vox/testing/alloc_trap.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <span>

using namespace vox::engine;
using namespace vox::engine::test;

namespace {

std::size_t limiterLatency(const ProcessingGraph& g) {
    return g.fixedLatencySamples();
}

Command command(Command::Type type, std::uint32_t a = 0, float x = 0.0F) {
    Command c;
    c.type = type;
    c.a = a;
    c.x = x;
    return c;
}

std::vector<EngineEvent> drain(ProcessingGraph& g) {
    std::vector<EngineEvent> events;
    EngineEvent e;
    while (g.popEvent(e)) {
        events.push_back(e);
    }
    return events;
}

void installClip(ProcessingGraph& g, std::uint32_t id, std::vector<float> samples) {
    auto install = std::make_unique<ClipInstall>();
    install->id = id;
    install->clip = std::make_unique<Clip>(Clip{std::move(samples)});
    const auto rejected1 = g.installClip(std::move(install));
    REQUIRE(rejected1 == nullptr);
}

} // namespace

TEST_CASE("Without a voice the microphone reaches the virtual mic unchanged", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    const auto noise = vox::testing::whiteNoise(0.5, kFs, 0.2F, 3);
    const auto out = runGraph(g, noise);
    const std::size_t lag = vox::testing::estimateLag(noise, out.mic, 1000);
    CHECK(lag == limiterLatency(g));
    // Same signal apart from the delay and the DC blocker (about -0.01 dB
    // at 1 kHz): compare levels.
    const double inRms = vox::testing::rms(std::span<const float>(noise).subspan(4800, 14400));
    const double outRms =
        vox::testing::rms(std::span<const float>(out.mic).subspan(4800 + lag, 14400));
    CHECK(std::abs(vox::testing::toDb(outRms / inRms)) < 0.1);
    // Hear-myself is off by default.
    CHECK(vox::testing::peakAbs(out.monitor) == 0.0F);
}

TEST_CASE("Hear myself sends the voice to the monitor", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    REQUIRE(g.send(command(Command::Type::HearMyself, 1)));
    const auto tone = vox::testing::sine(440.0, 0.3, kFs, 0.3F);
    const auto out = runGraph(g, tone);
    const auto tail = std::span<const float>(out.monitor).subspan(4800);
    CHECK(std::abs(vox::testing::peakAbs(tail) - 0.3F) < 0.01F);
}

TEST_CASE("Switching voices crossfades in 20 ms without clicks", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    REQUIRE(g.setVoiceChain(buildChain(gainVoice(0.0F))) == nullptr);
    const auto tone = vox::testing::sine(220.0, 0.6, kFs, 0.4F);
    std::vector<float> mic(tone.size());
    std::vector<float> monitor(tone.size());
    const std::size_t switchAt = 9600;
    for (std::size_t pos = 0; pos < tone.size(); pos += 128) {
        if (pos == switchAt) {
            REQUIRE(g.setVoiceChain(buildChain(gainVoice(-12.0F))) == nullptr);
        }
        g.process(std::span<const float>(tone).subspan(pos, 128),
                  std::span<float>(mic).subspan(pos, 128),
                  std::span<float>(monitor).subspan(pos, 128));
    }
    CHECK(maxStep(mic) <= sineSlope(0.4, 220.0) * 1.02F);
    const auto before = std::span<const float>(mic).subspan(4800, 4800);
    const auto after = std::span<const float>(mic).subspan(switchAt + 960 + 128, 9600);
    CHECK(std::abs(vox::testing::peakAbs(before) - 0.4F) < 0.005F);
    CHECK(std::abs(vox::testing::peakAbs(after) - 0.4F * 0.2512F) < 0.002F);
    g.collectGarbage(); // frees the first chain on this thread
}

TEST_CASE("Voice off passes the dry microphone with a crossfade", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    REQUIRE(g.setVoiceChain(buildChain(gainVoice(-12.0F))) == nullptr);
    const auto tone = vox::testing::sine(220.0, 0.3, kFs, 0.4F);
    static_cast<void>(runGraph(g, tone));
    REQUIRE(g.send(command(Command::Type::VoiceEnabled, 0)));
    const auto out = runGraph(g, tone);
    CHECK(maxStep(out.mic) <= sineSlope(0.4, 220.0) * 1.02F);
    CHECK(std::abs(vox::testing::peakAbs(std::span<const float>(out.mic).subspan(4800)) - 0.4F) <
          0.005F);
}

TEST_CASE("Voice latency is reported and matches the measured delay", "[engine][graph]") {
    const auto& registry = vox::plugins::EffectRegistry::builtin();
    auto voices = vox::plugins::loadBuiltinVoices(registry);
    REQUIRE(voices);
    const auto& list = voices.value();
    const auto deep = std::find_if(list.begin(), list.end(),
                                   [](const auto& v) { return v.id == "deep-baritone"; });
    REQUIRE(deep != list.end());
    ProcessingGraph g(EngineConfig{});
    auto chain = buildChain(*deep);
    const std::size_t voiceLatency = chain->latencySamples();
    REQUIRE(voiceLatency > 0);
    const auto rejected2 = g.setVoiceChain(std::move(chain));
    REQUIRE(rejected2 == nullptr);
    static_cast<void>(runGraph(g, std::vector<float>(1024, 0.0F)));
    CHECK(g.voiceLatencySamples() == voiceLatency);
    INFO("voice " << voiceLatency << " samples, fixed " << g.fixedLatencySamples());
    const double totalMs =
        static_cast<double>(voiceLatency + g.fixedLatencySamples()) / kFs * 1000.0;
    CHECK(totalMs < 35.0);
}

TEST_CASE("Noise suppression adds its latency only while on", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    const std::size_t off = g.fixedLatencySamples();
    REQUIRE(g.send(command(Command::Type::NoiseSuppression, 1, 1.0F)));
    const auto noise = vox::testing::whiteNoise(0.2, kFs, 0.1F, 9);
    static_cast<void>(runGraph(g, noise));
    CHECK(g.fixedLatencySamples() == off + vox::dsp::NoiseSuppressor::latencySamples());
    REQUIRE(g.send(command(Command::Type::NoiseSuppression, 0, 1.0F)));
    static_cast<void>(runGraph(g, noise));
    CHECK(g.fixedLatencySamples() == off);
}

TEST_CASE("Input gain, mix levels, and the limiter", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    const auto tone = vox::testing::sine(440.0, 0.3, kFs, 0.1F);
    SECTION("+6 dB input gain doubles the level") {
        REQUIRE(g.send(command(Command::Type::InputGain, 0, 6.0F)));
        const auto out = runGraph(g, tone);
        CHECK(std::abs(vox::testing::peakAbs(std::span<const float>(out.mic).subspan(4800)) -
                       0.1995F) < 0.003F);
    }
    SECTION("voice level -6 dB halves the voice") {
        Command mix = command(Command::Type::Mix, 0, -6.0F);
        mix.y = -6.0F;
        mix.z = -3.0F;
        mix.w = 0.0F;
        REQUIRE(g.send(mix));
        const auto out = runGraph(g, tone);
        CHECK(std::abs(vox::testing::peakAbs(std::span<const float>(out.mic).subspan(4800)) -
                       0.0501F) < 0.001F);
    }
    SECTION("the limiter holds the output below -1 dBFS") {
        REQUIRE(g.send(command(Command::Type::InputGain, 0, 24.0F)));
        const auto loud = vox::testing::sine(440.0, 0.5, kFs, 0.9F);
        const auto out = runGraph(g, loud);
        CHECK(vox::testing::peakAbs(out.mic) <= 0.8913F + 1e-4F);
        CHECK(vox::testing::allFinite(out.mic));
    }
}

TEST_CASE("The gate attenuates background noise between words", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    REQUIRE(g.send(command(Command::Type::Gate, 1, -45.0F)));
    const auto hiss = vox::testing::whiteNoise(0.5, kFs, 0.001F, 4); // about -65 dBFS RMS
    const auto out = runGraph(g, hiss);
    const auto tail = std::span<const float>(out.mic).subspan(9600);
    const double reduction = vox::testing::toDb(
        vox::testing::rms(tail) / vox::testing::rms(std::span<const float>(hiss).subspan(9600)));
    INFO("reduction " << reduction << " dB");
    CHECK(reduction < -35.0);
    CHECK_FALSE(g.meters().gateOpen);
}

TEST_CASE("Push-to-talk mutes the voice but not the soundboard", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    installClip(g, 7, std::vector<float>(48000, 0.5F));
    g.transmit().setMode(TransmitMode::PushToTalk);
    Command trigger = command(Command::Type::TriggerSound, 7);
    trigger.b = 1;
    REQUIRE(g.send(trigger));
    const auto tone = vox::testing::sine(440.0, 0.3, kFs, 0.3F);
    const auto out = runGraph(g, tone);
    // Only the sound (0.5 at -6 dB) is left: a constant, not the tone.
    const auto tail = std::span<const float>(out.mic).subspan(4800, 4800);
    CHECK(std::abs(vox::testing::peakAbs(tail) - 0.2506F) < 0.002F);
    CHECK(maxStep(tail) < 1e-4F);
    CHECK_FALSE(g.meters().transmitting);
}

TEST_CASE("Soundboard clips reach both outputs and report when finished", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    installClip(g, 3, std::vector<float>(4800, 0.4F));
    Command trigger = command(Command::Type::TriggerSound, 3);
    trigger.b = 1;
    REQUIRE(g.send(trigger));
    const auto out = runGraph(g, vox::testing::silence(0.2));
    const std::size_t at = 2400 + limiterLatency(g);
    CHECK(std::abs(out.mic[at] - 0.4F * 0.501187F) < 1e-3F);
    CHECK(std::abs(out.monitor[at] - 0.4F * 0.501187F) < 1e-3F);
    const auto events = drain(g);
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == EngineEventKind::SoundFinished);
    CHECK(events[0].value == 3);
}

TEST_CASE("Sounds can be kept out of the headphones", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    Command mix = command(Command::Type::Mix);
    mix.y = -6.0F; // the default sound level, as in the test above
    mix.a = 0;     // not in the monitor
    REQUIRE(g.send(mix));
    installClip(g, 3, std::vector<float>(9600, 0.4F));
    Command trigger = command(Command::Type::TriggerSound, 3);
    trigger.b = 1;
    REQUIRE(g.send(trigger));
    const auto out = runGraph(g, vox::testing::silence(0.2));
    const std::size_t at = 4800 + limiterLatency(g);
    CHECK(std::abs(out.mic[at] - 0.4F * 0.501187F) < 1e-3F);
    CHECK(std::abs(out.monitor[at]) < 1e-4F);
}

TEST_CASE("Replacing and removing clips frees them on the control thread", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    installClip(g, 1, std::vector<float>(100, 0.1F));
    static_cast<void>(runGraph(g, vox::testing::silence(0.01)));
    installClip(g, 1, std::vector<float>(100, 0.2F)); // replaces
    auto removal = std::make_unique<ClipInstall>();
    removal->id = 1;
    const auto rejected3 = g.installClip(std::move(removal));
    REQUIRE(rejected3 == nullptr);
    static_cast<void>(runGraph(g, vox::testing::silence(0.01)));
    g.collectGarbage(); // ASan reports a leak if any clip was lost
    Command trigger = command(Command::Type::TriggerSound, 1);
    trigger.b = 1;
    REQUIRE(g.send(trigger));
    const auto out = runGraph(g, vox::testing::silence(0.01));
    CHECK(vox::testing::peakAbs(out.mic) == 0.0F); // slot is empty
}

TEST_CASE("Speech plays in order, optionally through the voice", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    REQUIRE(g.setVoiceChain(buildChain(gainVoice(-12.0F))) == nullptr);
    auto first = std::make_unique<SpeechClip>();
    first->samples.assign(4800, 0.5F);
    auto second = std::make_unique<SpeechClip>();
    second->samples.assign(4800, 0.5F);
    second->throughVoice = true;
    const auto rejected4 = g.playSpeech(std::move(first));
    REQUIRE(rejected4 == nullptr);
    const auto rejected5 = g.playSpeech(std::move(second));
    REQUIRE(rejected5 == nullptr);
    const auto out = runGraph(g, vox::testing::silence(0.3));
    const std::size_t lat = limiterLatency(g);
    // Speech level -3 dB; the second clip also goes through the -12 dB voice.
    CHECK(std::abs(out.mic[2400 + lat] - 0.5F * 0.7079F) < 2e-3F);
    CHECK(std::abs(out.mic[4800 + 2400 + lat] - 0.5F * 0.7079F * 0.2512F) < 2e-3F);
    CHECK(out.mic[9600 + lat + 200] == 0.0F);
    const auto events = drain(g);
    CHECK(std::count_if(events.begin(), events.end(), [](const EngineEvent& e) {
              return e.kind == EngineEventKind::SpeechFinished;
          }) == 2);
    g.collectGarbage();
}

TEST_CASE("Speech through the voice reaches the headphones and pauses the microphone",
          "[engine][graph]") {
    // Hear-myself is off, as usual: the user still hears what is said for
    // them, and other apps get the speech in the voice, not mixed with the room.
    ProcessingGraph g(EngineConfig{});
    REQUIRE(g.setVoiceChain(buildChain(gainVoice(-12.0F))) == nullptr);
    auto speech = std::make_unique<SpeechClip>();
    speech->samples.assign(9600, 0.5F); // 0.2 s
    speech->throughVoice = true;
    const auto rejected = g.playSpeech(std::move(speech));
    REQUIRE(rejected == nullptr);
    const auto mic = vox::testing::sine(440.0, 1.0, kFs, 0.2F);
    const auto out = runGraph(g, mic);
    const std::size_t lat = limiterLatency(g);
    constexpr float kVoiceGain = 0.2512F;                  // the -12 dB voice
    constexpr float kSpoken = 0.5F * 0.7079F * kVoiceGain; // speech level -3 dB
    const auto window = [&](const std::vector<float>& x, std::size_t from, std::size_t n) {
        return std::span<const float>(x).subspan(from + lat, n);
    };
    // Mid-clip, past the 20 ms fade: both outputs carry the speech alone.
    for (const auto* x : {&out.mic, &out.monitor}) {
        const auto w = window(*x, 4800, 2400);
        CHECK(std::abs(*std::ranges::min_element(w) - kSpoken) < 3e-3F);
        CHECK(std::abs(*std::ranges::max_element(w) - kSpoken) < 3e-3F);
    }
    // Well after the clip: the microphone is back and the headphones are quiet.
    CHECK(std::abs(vox::testing::rms(window(out.mic, 24000, 4800)) -
                   0.2 * static_cast<double>(kVoiceGain) / std::sqrt(2.0)) < 2e-3);
    CHECK(vox::testing::rms(window(out.monitor, 24000, 4800)) < 1e-4);
}

TEST_CASE("Feedback guard turns hear-myself off when the monitor howls", "[engine][graph]") {
    ProcessingGraph g(EngineConfig{});
    REQUIRE(g.send(command(Command::Type::HearMyself, 1)));
    const auto howl = vox::testing::sine(2500.0, 2.0, kFs, 0.5F);
    const auto out = runGraph(g, howl);
    const auto events = drain(g);
    const auto it = std::find_if(events.begin(), events.end(), [](const EngineEvent& e) {
        return e.kind == EngineEventKind::FeedbackDetected;
    });
    REQUIRE(it != events.end());
    CHECK(std::abs(static_cast<double>(it->value) - 2500.0) < 30.0);
    // The monitor is silent by the end; the virtual mic is unaffected.
    CHECK(vox::testing::peakAbs(
              std::span<const float>(out.monitor).subspan(out.monitor.size() - 4800)) == 0.0F);
    CHECK(vox::testing::peakAbs(std::span<const float>(out.mic).subspan(out.mic.size() - 4800)) >
          0.4F);
}

TEST_CASE("The processing graph never allocates on the audio thread", "[engine][graph][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    const auto& registry = vox::plugins::EffectRegistry::builtin();
    auto voices = vox::plugins::loadBuiltinVoices(registry);
    REQUIRE(voices);
    ProcessingGraph g(EngineConfig{});
    REQUIRE(g.send(command(Command::Type::NoiseSuppression, 1, 1.0F)));
    REQUIRE(g.send(command(Command::Type::Gate, 1, -50.0F)));
    REQUIRE(g.send(command(Command::Type::HearMyself, 1)));
    installClip(g, 0, std::vector<float>(2000, 0.2F));
    const auto phrase = vox::testing::synthPhrase(120.0).audio;
    std::vector<float> mic(128);
    std::vector<float> monitor(128);
    // Everything that crosses threads is built before the trap.
    std::vector<std::unique_ptr<vox::plugins::VoiceChain>> chains;
    for (std::size_t i = 0; i < 6; ++i) {
        chains.push_back(buildChain(voices.value()[i * 9]));
    }
    auto speech = std::make_unique<SpeechClip>();
    speech->samples.assign(3000, 0.1F);
    auto replacement = std::make_unique<ClipInstall>();
    replacement->id = 0;
    replacement->clip = std::make_unique<Clip>(Clip{std::vector<float>(3000, 0.1F)});
    const auto rejected6 = g.setVoiceChain(std::move(chains[0]));
    REQUIRE(rejected6 == nullptr);
    const auto rejected7 = g.playSpeech(std::move(speech));
    REQUIRE(rejected7 == nullptr);
    const auto rejected8 = g.installClip(std::move(replacement));
    REQUIRE(rejected8 == nullptr);
    for (std::size_t i = 1; i < chains.size(); ++i) {
        const auto rejected9 = g.setVoiceChain(std::move(chains[i]));
        REQUIRE(rejected9 == nullptr);
    }

    std::size_t allocations = 0;
    std::size_t deallocations = 0;
    {
        const vox::testing::AllocationTrap trap;
        for (std::size_t pos = 0; pos + 128 <= phrase.size(); pos += 128) {
            if (pos % 6400 == 0) {
                Command trigger = command(Command::Type::TriggerSound, 0);
                trigger.b = 1;
                trigger.options.mode = PlayMode::Overlap;
                static_cast<void>(g.send(trigger));
                static_cast<void>(g.send(command(Command::Type::VoiceParameter, 0, 0.5F)));
            }
            g.process(std::span<const float>(phrase).subspan(pos, 128), mic, monitor);
        }
        allocations = trap.allocations();
        deallocations = trap.deallocations();
    }
    CHECK(allocations == 0);
    CHECK(deallocations == 0);
    g.collectGarbage();
}
