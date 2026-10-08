#include <vox/devices/fake_backend.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

namespace {

using vox::devices::DeviceKind;

class CountingCapture final : public vox::devices::CaptureHandler {
public:
    void onCapture(const float* /*interleaved*/, std::size_t frames,
                   std::uint32_t channels) noexcept override {
        framesSeen += frames;
        channelsSeen = channels;
    }
    std::atomic<std::size_t> framesSeen{0};
    std::atomic<std::uint32_t> channelsSeen{0};
};

class ConstantPlayback final : public vox::devices::PlaybackHandler {
public:
    void onPlayback(float* interleaved, std::size_t frames,
                    std::uint32_t channels) noexcept override {
        std::fill(interleaved, interleaved + frames * channels, 0.25F);
    }
};

void addStandardDevices(vox::devices::FakeBackend& b) {
    b.addDevice({"mic", "USB Microphone", DeviceKind::Capture, true, 48000, 1, false});
    b.addDevice({"spk", "Headphones", DeviceKind::Playback, true, 48000, 2, false});
    b.addDevice({"cable", "CABLE Input (VB-Audio Virtual Cable)", DeviceKind::Playback, false,
                 48000, 2, false});
}

} // namespace

TEST_CASE("Virtual cables are recognized by name", "[devices]") {
    using vox::devices::looksLikeVirtualCable;
    CHECK(looksLikeVirtualCable("CABLE Input (VB-Audio Virtual Cable)"));
    CHECK(looksLikeVirtualCable("BlackHole 2ch"));
    CHECK(looksLikeVirtualCable("VoiceMeeter Input (VB-Audio VoiceMeeter VAIO)"));
    CHECK(looksLikeVirtualCable("Voxwright Microphone"));
    CHECK_FALSE(looksLikeVirtualCable("Speakers (Realtek High Definition Audio)"));
    CHECK_FALSE(looksLikeVirtualCable("MacBook Pro Microphone"));
}

TEST_CASE("Fake backend enumerates, opens defaults, and reports formats", "[devices][fake]") {
    vox::devices::FakeBackend backend;
    addStandardDevices(backend);
    const auto playback = backend.enumerate(DeviceKind::Playback);
    REQUIRE(playback);
    REQUIRE(playback.value().size() == 2);
    CHECK(playback.value()[1].isVirtualCable);

    CountingCapture capture;
    auto stream = backend.openCapture({}, capture); // default device
    REQUIRE(stream);
    CHECK(stream.value()->info().deviceId == "mic");
    CHECK(stream.value()->info().sampleRate == 48000);
    CHECK_FALSE(backend.pumpCapture("mic", std::vector<float>(128, 0.0F))); // not started
    REQUIRE(stream.value()->start());
    CHECK(backend.pumpCapture("mic", std::vector<float>(128, 0.0F)));
    CHECK(capture.framesSeen == 128);
    stream.value()->stop();
    CHECK_FALSE(backend.isRunning("mic"));
}

TEST_CASE("Fake backend injects open failures and disconnects", "[devices][fake][errors]") {
    vox::devices::FakeBackend backend;
    addStandardDevices(backend);
    std::vector<vox::devices::DeviceEvent> events;
    backend.setEventCallback(
        [&events](const vox::devices::DeviceEvent& e) { events.push_back(e); });

    ConstantPlayback playback;
    backend.failNextOpen("spk", vox::makeError(vox::ErrorCode::DeviceInUseExclusive, "busy"));
    auto failed = backend.openPlayback({"spk", 0, 0, 0, false}, playback);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == vox::ErrorCode::DeviceInUseExclusive);

    auto missing = backend.openPlayback({"nope", 0, 0, 0, false}, playback);
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == vox::ErrorCode::DeviceNotFound);

    auto stream = backend.openPlayback({"spk", 0, 0, 0, false}, playback);
    REQUIRE(stream);
    REQUIRE(stream.value()->start());
    const auto out = backend.pumpPlayback("spk", 64);
    REQUIRE(out);
    const std::vector<float> samples = out.value_or(std::vector<float>{});
    REQUIRE(samples.size() == 128); // stereo
    CHECK(samples[127] == 0.25F);

    backend.disconnect("spk");
    CHECK_FALSE(backend.isRunning("spk"));
    CHECK_FALSE(backend.pumpPlayback("spk", 64));
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == vox::devices::DeviceEventKind::StreamStopped);
    CHECK(events[0].deviceId == "spk");
    CHECK(events[1].kind == vox::devices::DeviceEventKind::DeviceListChanged);
}

TEST_CASE("Stopping a fake stream waits for its running callback", "[devices][fake]") {
    vox::devices::FakeBackend backend;
    addStandardDevices(backend);
    CountingCapture capture;
    auto stream = backend.openCapture({"mic", 0, 0, 0, false}, capture);
    REQUIRE(stream);
    REQUIRE(stream.value()->start());
    std::atomic<bool> done{false};
    std::thread pump([&] {
        const std::vector<float> block(64, 0.0F);
        while (backend.pumpCapture("mic", block)) {
        }
        done = true;
    });
    while (capture.framesSeen == 0) {
        std::this_thread::yield();
    }
    stream.value()->stop();
    // After stop() returns no callback is running, so reading is race-free.
    const std::size_t frames = capture.framesSeen;
    pump.join();
    CHECK(done);
    CHECK(capture.framesSeen == frames);
}

TEST_CASE("The system backend starts or explains why not", "[devices][system]") {
    auto backend = vox::devices::createSystemBackend();
    if (!backend) {
        CHECK(backend.error().code == vox::ErrorCode::BackendInitFailed);
        CHECK_FALSE(backend.error().message.empty());
        return;
    }
    CHECK_FALSE(backend.value()->name().empty());
    // A machine without sound hardware (CI) may list nothing, but the call
    // must succeed.
    CHECK(backend.value()->enumerate(DeviceKind::Playback));
    CHECK(backend.value()->enumerate(DeviceKind::Capture));
}
