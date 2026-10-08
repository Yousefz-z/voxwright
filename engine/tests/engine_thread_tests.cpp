// Runs the engine with real threads: one capture thread, two playback
// threads, and the control thread changing everything at once. Under
// ThreadSanitizer this is the data-race check for the whole engine.

#include "engine_test_helpers.hpp"

#include <vox/devices/fake_backend.hpp>
#include <vox/engine/audio_engine.hpp>
#include <vox/plugins/voice_library.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace vox::engine;
using namespace vox::engine::test;
using vox::devices::DeviceKind;

TEST_CASE("Engine threads run concurrently with control changes", "[engine][threads]") {
    vox::devices::FakeBackend backend;
    backend.addDevice({"mic", "Microphone", DeviceKind::Capture, true, 48000, 1, false});
    backend.addDevice({"cable", "CABLE Input", DeviceKind::Playback, false, 48000, 2, false});
    backend.addDevice({"phones", "Headphones", DeviceKind::Playback, true, 44100, 2, false});
    AudioEngine engine(backend);
    DeviceSelection selection;
    selection.inputId = "mic";
    selection.virtualMicId = "cable";
    selection.monitorId = "phones";
    REQUIRE(engine.start(selection));

    const auto& registry = vox::plugins::EffectRegistry::builtin();
    auto voices = vox::plugins::loadBuiltinVoices(registry);
    REQUIRE(voices);
    REQUIRE(engine.loadSound(0, std::vector<float>(4800, 0.3F)));

    std::atomic<bool> stop{false};
    std::atomic<bool> finite{true};
    const auto phrase = vox::testing::synthPhrase(140.0).audio;

    // Each device thread paces itself to its nominal rate, like real hardware.
    // It yields instead of sleeping: sleep granularity on Windows (about
    // 15 ms by default) is far coarser than a 2.7 ms device period.
    const auto paced = [&stop](double periodSeconds, auto&& body) {
        auto next = std::chrono::steady_clock::now();
        const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(periodSeconds));
        while (!stop.load()) {
            body();
            next += period;
            while (std::chrono::steady_clock::now() < next && !stop.load()) {
                std::this_thread::yield();
            }
        }
    };
    std::thread capture([&] {
        std::size_t pos = 0;
        std::vector<float> block(128);
        paced(128.0 / 48000.0, [&] {
            for (float& s : block) {
                s = phrase[pos++ % phrase.size()];
            }
            backend.pumpCapture("mic", block);
        });
    });
    const auto player = [&](const char* id, std::size_t frames, double rate) {
        return std::thread([&, id, frames, rate] {
            paced(static_cast<double>(frames) / rate, [&] {
                if (const auto out = backend.pumpPlayback(id, frames)) {
                    if (!vox::testing::allFinite(*out)) {
                        finite = false;
                    }
                }
            });
        });
    };
    std::thread cablePlayer = player("cable", 128, 48000.0);
    std::thread phonesPlayer = player("phones", 441, 44100.0);

    // Control thread: switch voices, move sliders, fire sounds and speech.
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    std::size_t step = 0;
    std::size_t events = 0;
    while (std::chrono::steady_clock::now() < until) {
        const auto& voice = voices.value()[step % voices.value().size()];
        CHECK(engine.setVoice(voice, {}, registry));
        CHECK(engine.setVoiceParameter(0, 0, 0.3F));
        CHECK(engine.setMixLevels({-1.0F, -6.0F, -3.0F, -2.0F}));
        CHECK(engine.setHearMyself(step % 2 == 0));
        CHECK(engine.setNoiseSuppression(step % 3 == 0, 0.8F));
        CHECK(engine.triggerSound(0, {}, true));
        if (step % 5 == 0) {
            CHECK(engine.playSpeech(std::vector<float>(2400, 0.1F), step % 10 == 0));
        }
        if (step % 7 == 0) {
            CHECK(engine.loadSound(1, std::vector<float>(2400, 0.2F)));
        }
        engine.transmit().setTalkKeyDown(step % 2 == 0);
        const auto stats = engine.stats();
        CHECK(stats.inputPeak >= 0.0F);
        events += engine.poll().size();
        ++step;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    stop = true;
    capture.join();
    cablePlayer.join();
    phonesPlayer.join();
    engine.stop();
    static_cast<void>(engine.poll());

    INFO(step << " control rounds, " << events << " events");
    CHECK(finite);
    CHECK(step > 20);
    CHECK(events > 0);
}
