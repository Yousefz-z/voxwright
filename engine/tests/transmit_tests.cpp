#include "engine_test_helpers.hpp"

#include <vox/engine/transmit_control.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace vox::engine;
using namespace vox::engine::test;

namespace {

/// Processes `seconds` of a 300 Hz sine and returns the output.
std::vector<float> run(TransmitControl& t, double seconds) {
    auto x = vox::testing::sine(300.0, seconds, kFs, 0.5F);
    for (std::size_t pos = 0; pos < x.size(); pos += 128) {
        t.process(std::span<float>(x).subspan(pos, std::min<std::size_t>(128, x.size() - pos)));
    }
    return x;
}

double rmsOf(const std::vector<float>& x, double fromS, double toS) {
    const auto a = static_cast<std::size_t>(fromS * kFs);
    const auto b = static_cast<std::size_t>(toS * kFs);
    return vox::testing::rms(std::span<const float>(x).subspan(a, b - a));
}

} // namespace

TEST_CASE("Always-on passes the voice unchanged", "[engine][transmit]") {
    TransmitControl t;
    t.prepare(kFs);
    const auto out = run(t, 0.2);
    const auto ref = vox::testing::sine(300.0, 0.2, kFs, 0.5F);
    for (std::size_t i = 0; i < out.size(); i += 101) {
        CHECK(out[i] == ref[i]);
    }
}

TEST_CASE("Mute fades out in 20 ms and back in 10 ms without steps", "[engine][transmit]") {
    TransmitControl t;
    t.prepare(kFs);
    static_cast<void>(run(t, 0.05));
    t.setMuted(true);
    const auto muted = run(t, 0.1);
    CHECK(rmsOf(muted, 0.0, 0.005) > 0.1);  // still fading
    CHECK(rmsOf(muted, 0.021, 0.1) == 0.0); // silent after the 20 ms release
    CHECK(maxStep(muted) <= sineSlope(0.5, 300.0) * 1.01F);
    t.setMuted(false);
    const auto back = run(t, 0.05);
    CHECK(rmsOf(back, 0.011, 0.05) > 0.35); // full level after the 10 ms attack
    CHECK(maxStep(back) <= sineSlope(0.5, 300.0) * 1.01F);
}

TEST_CASE("Push-to-talk sends only while the key is held, plus the release delay",
          "[engine][transmit]") {
    TransmitControl t;
    t.prepare(kFs);
    t.setMode(TransmitMode::PushToTalk);
    t.setReleaseDelayMs(150.0F);
    const auto idle = run(t, 0.1);
    CHECK(rmsOf(idle, 0.025, 0.1) == 0.0);
    t.setTalkKeyDown(true);
    const auto talking = run(t, 0.1);
    CHECK(rmsOf(talking, 0.011, 0.1) > 0.35);
    t.setTalkKeyDown(false);
    const auto released = run(t, 0.3);
    // Word endings survive for the release delay, then a 20 ms fade.
    CHECK(rmsOf(released, 0.0, 0.14) > 0.35);
    CHECK(rmsOf(released, 0.175, 0.3) == 0.0);
}

TEST_CASE("Push-to-mute silences only while the key is held", "[engine][transmit]") {
    TransmitControl t;
    t.prepare(kFs);
    t.setMode(TransmitMode::PushToMute);
    const auto open = run(t, 0.05);
    CHECK(rmsOf(open, 0.0, 0.05) > 0.35);
    t.setTalkKeyDown(true);
    const auto held = run(t, 0.1);
    CHECK(rmsOf(held, 0.025, 0.1) == 0.0);
}

TEST_CASE("Censor key replaces the voice with a 1 kHz beep at -12 dBFS", "[engine][transmit]") {
    TransmitControl t;
    t.prepare(kFs);
    t.setCensorKeyDown(true);
    const auto out = run(t, 0.5);
    const auto steady = std::span<const float>(out).subspan(4800);
    CHECK(std::abs(vox::testing::peakAbs(steady) - 0.25F) < 0.002F);
    const auto spectrum = vox::testing::powerSpectrumDb(steady, 16384);
    const auto bin = [](double hz) {
        return static_cast<std::size_t>(std::lround(hz / kFs * 16384.0));
    };
    // The voice (300 Hz) is gone: at least 60 dB below the beep.
    CHECK(spectrum[bin(1000.0)] - spectrum[bin(300.0)] > 60.0);

    t.setMuted(true);
    const auto muted = run(t, 0.1);
    CHECK(rmsOf(muted, 0.021, 0.1) == 0.0); // muting also silences the beep
}
