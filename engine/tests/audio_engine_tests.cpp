#include "engine_test_helpers.hpp"

#include <vox/devices/fake_backend.hpp>
#include <vox/engine/audio_engine.hpp>
#include <vox/testing/alloc_trap.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace vox::engine;
using namespace vox::engine::test;
using vox::devices::DeviceKind;

namespace {

struct Rig {
    explicit Rig(std::uint32_t micRate = 48000, std::uint32_t micChannels = 1,
                 std::uint32_t outRate = 48000, std::uint32_t outChannels = 2) {
        backend.addDevice(
            {"mic", "USB Microphone", DeviceKind::Capture, true, micRate, micChannels, false});
        backend.addDevice({"cable", "CABLE Input (VB-Audio Virtual Cable)", DeviceKind::Playback,
                           false, outRate, outChannels, false});
        backend.addDevice(
            {"phones", "Headphones", DeviceKind::Playback, true, outRate, outChannels, false});
        selection.inputId = "mic";
        selection.virtualMicId = "cable";
        selection.monitorId = "phones";
    }

    /// Runs `frames` of lock-step I/O: capture blocks of `micBlock` frames
    /// from `input`, playback blocks of `outBlock`. Returns the mono virtual
    /// mic and monitor output (first channel).
    GraphOutput pump(const std::vector<float>& monoInput, std::size_t micBlock = 128,
                     std::size_t outBlock = 128) {
        GraphOutput out;
        const auto active = engine.activeDevices();
        const std::uint32_t micCh = active.input ? active.input->channels : 1;
        const double micRate = active.input ? active.input->sampleRate : 48000.0;
        const double outRate = active.virtualMic ? active.virtualMic->sampleRate : 48000.0;
        double micTime = 0.0;
        double outTime = 0.0;
        std::size_t pos = 0;
        while (pos + micBlock <= monoInput.size()) {
            if (active.input && micTime <= outTime) {
                backend.setTime(micTime);
                std::vector<float> block(micBlock * micCh);
                for (std::size_t i = 0; i < micBlock; ++i) {
                    for (std::uint32_t c = 0; c < micCh; ++c) {
                        block[i * micCh + c] = monoInput[pos + i];
                    }
                }
                backend.pumpCapture("mic", block);
                pos += micBlock;
                micTime += static_cast<double>(micBlock) / micRate;
            } else {
                backend.setTime(outTime);
                for (auto [id, dest] :
                     {std::pair{"cable", &out.mic}, std::pair{"phones", &out.monitor}}) {
                    if (const auto played = backend.pumpPlayback(id, outBlock)) {
                        const std::size_t ch = played->size() / outBlock;
                        for (std::size_t i = 0; i < outBlock; ++i) {
                            dest->push_back((*played)[i * ch]);
                        }
                    }
                }
                outTime += static_cast<double>(outBlock) / outRate;
                if (!active.input) {
                    pos += micBlock;
                }
            }
        }
        return out;
    }

    vox::devices::FakeBackend backend;
    AudioEngine engine{backend};
    DeviceSelection selection;
};

bool hasEvent(const std::vector<EngineEvent>& events, EngineEventKind kind, DeviceRole role) {
    return std::any_of(events.begin(), events.end(), [&](const EngineEvent& e) {
        return e.kind == kind && e.value == static_cast<std::uint32_t>(role);
    });
}

} // namespace

TEST_CASE("Microphone to virtual mic end to end at 48 kHz", "[engine][e2e]") {
    Rig rig;
    REQUIRE(rig.engine.start(rig.selection));
    const auto active = rig.engine.activeDevices();
    REQUIRE(active.input);
    REQUIRE(active.virtualMic);
    REQUIRE(active.monitor);
    CHECK_FALSE(active.pullMode);

    // 30 s of quiet input while the drift controller settles, then noise for
    // the delay and a tone for the level.
    auto input = vox::testing::whiteNoise(30.0, kFs, 0.001F, 3);
    const std::size_t start = input.size();
    const auto noise = vox::testing::whiteNoise(1.0, kFs, 0.2F, 11);
    const auto tone = vox::testing::sine(1000.0, 1.0, kFs, 0.3F);
    input.insert(input.end(), noise.begin(), noise.end());
    input.insert(input.end(), tone.begin(), tone.end());
    const auto out = rig.pump(input);
    REQUIRE(out.mic.size() > start + 90000);
    const std::size_t lag =
        vox::testing::estimateLag(std::span<const float>(input).subspan(start, 48000),
                                  std::span<const float>(out.mic).subspan(start, 48000), 4800);
    const auto stats = rig.engine.stats();
    const double measuredMs = static_cast<double>(lag) / kFs * 1000.0;
    INFO("measured " << measuredMs << " ms in lock-step, estimate " << stats.estimatedLatencyMs
                     << " ms");
    // The estimate also counts one capture and one playback period, which
    // lock-step pumping does not see.
    CHECK(measuredMs <= stats.estimatedLatencyMs);
    CHECK(stats.estimatedLatencyMs - measuredMs <= 5.4);
    CHECK(stats.estimatedLatencyMs < 10.0);
    CHECK(stats.underruns == 0);
    CHECK(stats.overruns == 0);
    const auto toneOut = std::span<const float>(out.mic).subspan(start + 48000 + 4800 + lag, 38400);
    CHECK(std::abs(vox::testing::peakAbs(toneOut) - 0.3F) < 0.003F);
    CHECK(stats.processingLoad > 0.0);
    CHECK(stats.processingLoad < 1.0);
}

TEST_CASE("Devices at 44.1 kHz and stereo are converted correctly", "[engine][e2e]") {
    Rig rig(44100, 2, 44100, 2);
    REQUIRE(rig.engine.start(rig.selection));
    const auto tone = vox::testing::sine(1000.0, 2.0, 44100.0, 0.3F);
    const auto out = rig.pump(tone, 441, 441);
    REQUIRE(out.mic.size() > 44100);
    const auto tail = std::span<const float>(out.mic).subspan(out.mic.size() - 32768);
    const auto spectrum = vox::testing::powerSpectrumDb(tail, 32768);
    const auto peak = static_cast<std::size_t>(
        std::max_element(spectrum.begin() + 1, spectrum.end()) - spectrum.begin());
    const double hz = static_cast<double>(peak) * 44100.0 / 32768.0;
    CHECK(std::abs(hz - 1000.0) < 2.0);
    CHECK(std::abs(vox::testing::peakAbs(tail) - 0.3F) < 0.01F);
    CHECK(rig.engine.stats().underruns == 0);
}

TEST_CASE("Microphone and outputs at different rates", "[engine][e2e]") {
    Rig rig(48000, 1, 44100, 2);
    REQUIRE(rig.engine.start(rig.selection));
    const auto tone = vox::testing::sine(500.0, 2.0, 48000.0, 0.3F);
    const auto out = rig.pump(tone, 480, 441);
    const auto tail = std::span<const float>(out.mic).subspan(out.mic.size() - 32768);
    const auto spectrum = vox::testing::powerSpectrumDb(tail, 32768);
    const auto peak = static_cast<std::size_t>(
        std::max_element(spectrum.begin() + 1, spectrum.end()) - spectrum.begin());
    CHECK(std::abs(static_cast<double>(peak) * 44100.0 / 32768.0 - 500.0) < 2.0);
    CHECK(rig.engine.stats().underruns == 0);
}

TEST_CASE("Start failures name the device and leave nothing running", "[engine][errors]") {
    Rig rig;
    SECTION("microphone missing") {
        rig.selection.inputId = "unplugged";
        const auto r = rig.engine.start(rig.selection);
        REQUIRE_FALSE(r);
        CHECK(r.error().code == vox::ErrorCode::DeviceNotFound);
        CHECK(r.error().message.rfind("Microphone: ", 0) == 0);
    }
    SECTION("virtual cable held by another app in exclusive mode") {
        rig.backend.failNextOpen("cable", vox::makeError(vox::ErrorCode::DeviceInUseExclusive,
                                                         "Another app is using it exclusively."));
        const auto r = rig.engine.start(rig.selection);
        REQUIRE_FALSE(r);
        CHECK(r.error().code == vox::ErrorCode::DeviceInUseExclusive);
        CHECK(r.error().message.rfind("Virtual microphone output: ", 0) == 0);
    }
    SECTION("unsupported sample rate") {
        rig.backend.addDevice({"odd", "Odd Device", DeviceKind::Playback, false, 4000, 2, false});
        rig.selection.monitorId = "odd";
        const auto r = rig.engine.start(rig.selection);
        REQUIRE_FALSE(r);
        CHECK(r.error().code == vox::ErrorCode::UnsupportedSampleRate);
        CHECK(r.error().message.find("48000 Hz") != std::string::npos);
    }
    CHECK_FALSE(rig.engine.isRunning());
    CHECK_FALSE(rig.backend.isRunning("mic"));
    CHECK_FALSE(rig.backend.isRunning("cable"));
    CHECK_FALSE(rig.backend.isRunning("phones"));
}

TEST_CASE("Without a microphone the virtual mic output drives processing", "[engine][e2e]") {
    Rig rig;
    rig.selection.useInput = false;
    REQUIRE(rig.engine.start(rig.selection));
    CHECK(rig.engine.activeDevices().pullMode);
    REQUIRE(rig.engine.loadSound(4, std::vector<float>(24000, 0.5F)));
    REQUIRE(rig.engine.triggerSound(4, {}, true));
    const auto out = rig.pump(std::vector<float>(48000, 0.0F));
    const auto mid = std::span<const float>(out.mic).subspan(9600, 4800);
    CHECK(std::abs(vox::testing::peakAbs(mid) - 0.2506F) < 0.002F);
    const auto monitorMid = std::span<const float>(out.monitor).subspan(9600, 4800);
    CHECK(std::abs(vox::testing::peakAbs(monitorMid) - 0.2506F) < 0.002F);
    const auto events = rig.engine.poll();
    CHECK(std::any_of(events.begin(), events.end(), [](const EngineEvent& e) {
        return e.kind == EngineEventKind::SoundFinished && e.value == 4;
    }));
}

TEST_CASE("Losing the microphone keeps sounds working; it is reopened when it returns",
          "[engine][devices]") {
    Rig rig;
    REQUIRE(rig.engine.start(rig.selection));
    rig.backend.disconnect("mic");
    auto events = rig.engine.poll();
    CHECK(hasEvent(events, EngineEventKind::DeviceLost, DeviceRole::Input));
    CHECK(rig.engine.isRunning());
    CHECK(rig.engine.activeDevices().pullMode);
    REQUIRE(rig.engine.loadSound(0, std::vector<float>(9600, 0.5F)));
    REQUIRE(rig.engine.triggerSound(0, {}, true));
    const auto out = rig.pump(std::vector<float>(9600, 0.0F));
    CHECK(vox::testing::peakAbs(out.mic) > 0.2F);

    rig.backend.addDevice({"mic", "USB Microphone", DeviceKind::Capture, true, 48000, 1, false});
    events = rig.engine.poll();
    CHECK(hasEvent(events, EngineEventKind::DeviceRestored, DeviceRole::Input));
    CHECK_FALSE(rig.engine.activeDevices().pullMode);
    CHECK(rig.backend.isRunning("mic"));
}

TEST_CASE("Losing the headphones keeps the virtual mic running", "[engine][devices]") {
    Rig rig;
    REQUIRE(rig.engine.start(rig.selection));
    rig.backend.disconnect("phones");
    const auto events = rig.engine.poll();
    CHECK(hasEvent(events, EngineEventKind::DeviceLost, DeviceRole::Monitor));
    const auto active = rig.engine.activeDevices();
    CHECK(active.input);
    CHECK(active.virtualMic);
    CHECK_FALSE(active.monitor);
    const auto tone = vox::testing::sine(300.0, 0.3, kFs, 0.3F);
    const auto out = rig.pump(tone);
    CHECK(vox::testing::peakAbs(out.mic) > 0.25F);
}

TEST_CASE("A returning device that fails to open is skipped, not fatal", "[engine][devices]") {
    Rig rig;
    REQUIRE(rig.engine.start(rig.selection));
    rig.backend.disconnect("phones");
    static_cast<void>(rig.engine.poll());
    rig.backend.failNextOpen("phones",
                             vox::makeError(vox::ErrorCode::DeviceStartFailed, "not ready"));
    rig.backend.addDevice({"phones", "Headphones", DeviceKind::Playback, true, 48000, 2, false});
    const auto events = rig.engine.poll();
    CHECK_FALSE(hasEvent(events, EngineEventKind::DeviceRestored, DeviceRole::Monitor));
    CHECK(rig.engine.isRunning());
    CHECK(rig.backend.isRunning("cable"));
    CHECK_FALSE(rig.backend.isRunning("phones"));
}

TEST_CASE("If no device can be reopened the engine stops and says why", "[engine][devices]") {
    Rig rig;
    REQUIRE(rig.engine.start(rig.selection));
    rig.backend.failNextOpen("cable", vox::makeError(vox::ErrorCode::DeviceStartFailed,
                                                     "The driver stopped responding."));
    rig.backend.disconnect("mic");
    const auto events = rig.engine.poll();
    const auto failed = std::find_if(events.begin(), events.end(), [](const EngineEvent& e) {
        return e.kind == EngineEventKind::RestartFailed;
    });
    REQUIRE(failed != events.end());
    CHECK(failed->detail.find("driver stopped responding") != std::string::npos);
    CHECK_FALSE(rig.engine.isRunning());
}

TEST_CASE("Settings changed while stopped are kept", "[engine][commands]") {
    Rig rig;
    for (int i = 0; i < 5000; ++i) {
        REQUIRE(rig.engine.setInputGainDb(6.0F)); // never fills the queue while stopped
    }
    REQUIRE(rig.engine.start(rig.selection));
    const auto tone = vox::testing::sine(300.0, 0.5, kFs, 0.1F);
    const auto out = rig.pump(tone);
    CHECK(std::abs(vox::testing::peakAbs(std::span<const float>(out.mic).subspan(9600)) - 0.1995F) <
          0.003F);
}

TEST_CASE("A stalled audio device produces a clear error instead of growing memory",
          "[engine][commands][errors]") {
    Rig rig;
    REQUIRE(rig.engine.start(rig.selection));
    vox::Status last;
    for (int i = 0; i < 2000 && last; ++i) {
        last = rig.engine.setInputGainDb(0.0F); // nothing is pumped: nobody reads
    }
    REQUIRE_FALSE(last);
    CHECK(last.error().code == vox::ErrorCode::CommandQueueFull);
    CHECK(last.error().message.find("Settings > Audio") != std::string::npos);
}

TEST_CASE("Invalid soundboard and voice requests are rejected with reasons",
          "[engine][commands][errors]") {
    Rig rig;
    auto r = rig.engine.loadSound(5000, std::vector<float>(10, 0.1F));
    REQUIRE_FALSE(r);
    CHECK(r.error().code == vox::ErrorCode::InvalidArgument);
    r = rig.engine.loadSound(1, {});
    REQUIRE_FALSE(r);
    CHECK(r.error().code == vox::ErrorCode::InvalidArgument);
    r = rig.engine.playSpeech({}, false);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == vox::ErrorCode::InvalidArgument);

    auto preset = gainVoice(0.0F);
    preset.blocks[0].effect = "does-not-exist";
    r = rig.engine.setVoice(preset, {}, vox::plugins::EffectRegistry::builtin());
    REQUIRE_FALSE(r);
    CHECK(r.error().code == vox::ErrorCode::UnknownEffect);
}

TEST_CASE("The capture callback path does not allocate", "[engine][e2e][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    Rig rig(44100, 2, 48000, 2);
    REQUIRE(rig.engine.start(rig.selection));
    REQUIRE(rig.engine.setVoice(gainVoice(-3.0F), {}, vox::plugins::EffectRegistry::builtin()));
    REQUIRE(rig.engine.setNoiseSuppression(true, 1.0F));
    const std::vector<float> block(std::size_t{441} * 2, 0.05F);
    std::size_t allocations = 0;
    {
        const vox::testing::AllocationTrap trap;
        for (int i = 0; i < 50; ++i) {
            rig.backend.pumpCapture("mic", block);
        }
        allocations = trap.allocations();
    }
    CHECK(allocations == 0);
}

TEST_CASE("Device list changes are reported, running or not", "[engine][devices]") {
    Rig rig;
    static_cast<void>(rig.engine.poll()); // the rig's own setup changes
    rig.backend.addDevice({"usb", "USB Headset", DeviceKind::Playback, false, 48000, 2, false});
    auto events = rig.engine.poll();
    CHECK(std::any_of(events.begin(), events.end(), [](const EngineEvent& e) {
        return e.kind == EngineEventKind::DevicesChanged;
    }));
    REQUIRE(rig.engine.start(rig.selection));
    rig.backend.removeDevice("usb");
    events = rig.engine.poll();
    CHECK(std::any_of(events.begin(), events.end(), [](const EngineEvent& e) {
        return e.kind == EngineEventKind::DevicesChanged;
    }));
    CHECK(rig.engine.isRunning());
}

TEST_CASE("Buffer size and exclusive mode apply from the next start", "[engine][devices]") {
    Rig rig;
    rig.engine.setDeviceOptions(256, true);
    REQUIRE(rig.engine.start(rig.selection));
    const auto active = rig.engine.activeDevices();
    REQUIRE(active.input);
    const auto input = active.input.value_or(vox::devices::StreamInfo{});
    CHECK(input.periodFrames == 256);
    CHECK(input.exclusive);
    CHECK(rig.engine.stats().virtualMicLatency.captureMs == Catch::Approx(256.0 / 48.0));
}
