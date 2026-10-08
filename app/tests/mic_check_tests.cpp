#include "app_test_support.hpp"
#include "virtual_mic_check.hpp"

#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QTest>

using namespace vox::app;
using namespace vox::app::test;

namespace {

/// Waits for the check to finish while the devices run.
VirtualMicCheck::State runCheck(TestApp& t, const std::vector<float>& micInput = {}) {
    auto* check = t.context().micCheck();
    check->setListenMs(300);
    check->start(t.context().audio()->chatAppMicrophoneName());
    if (check->state() != VirtualMicCheck::State::Running) {
        return check->state();
    }
    t.record(1.0, micInput);
    static_cast<void>(
        QTest::qWaitFor([&] { return check->state() != VirtualMicCheck::State::Running; }, 5000));
    return check->state();
}

} // namespace

TEST_CASE("The chirp is found in noise and nothing else matches it", "[app][miccheck]") {
    const auto chirp = VirtualMicCheck::probe();
    REQUIRE(chirp.size() == 14400);
    // Noise 20 dB below the (half level) chirp.
    std::vector<float> recording = vox::testing::whiteNoise(1.0, 48000.0, 5);
    const auto chirpRms = static_cast<float>(0.5 * vox::testing::rms(chirp));
    const auto noiseScale = 0.1F * chirpRms / static_cast<float>(vox::testing::rms(recording));
    for (float& s : recording) {
        s *= noiseScale;
    }
    const std::vector<float> noiseOnly = recording;
    for (std::size_t i = 0; i < chirp.size(); ++i) {
        recording[12345 + i] += 0.5F * chirp[i]; // quieter and delayed
    }
    CHECK(VirtualMicCheck::matchStrength(recording, chirp) > 0.8);
    CHECK(VirtualMicCheck::matchStrength(noiseOnly, chirp) < 0.3);
    const auto speech = vox::testing::synthPhrase(140.0).audio;
    CHECK(VirtualMicCheck::matchStrength(speech, chirp) < 0.3);
    CHECK(VirtualMicCheck::matchStrength({}, chirp) == 0.0);
}

TEST_CASE("The virtual microphone check passes when the cable loops back", "[app][miccheck]") {
    TestApp t({.cable = true, .cableLoopback = true});
    CHECK(runCheck(t) == VirtualMicCheck::State::Passed);
    CHECK(t.context().micCheck()->message().contains(QStringLiteral("CABLE Output")));
}

TEST_CASE("The virtual microphone check says what went wrong", "[app][miccheck][errors]") {
    SECTION("Nothing arrives") {
        TestApp t({.cable = true, .cableLoopback = false, .cableRecordingSide = true});
        CHECK(runCheck(t) == VirtualMicCheck::State::Silent);
        CHECK(t.context().micCheck()->message().contains(QStringLiteral("Nothing arrived")));
    }
    SECTION("Something else arrives") {
        TestApp t({.cable = true, .cableLoopback = true});
        // The cable carries the microphone too (voice changer on), so a loud
        // voice over the chirp makes it unrecognizable.
        t.context().audio()->setVoiceEnabled(true);
        const auto loud = vox::testing::whiteNoise(1.0, 48000.0, 7);
        std::vector<float> scaled(loud.size());
        for (std::size_t i = 0; i < loud.size(); ++i) {
            scaled[i] = loud[i] * 0.9F;
        }
        CHECK(runCheck(t, scaled) == VirtualMicCheck::State::Mismatch);
    }
    SECTION("The recording side is missing") {
        TestApp t({.cable = true});
        CHECK(runCheck(t) == VirtualMicCheck::State::Failed);
        CHECK(t.context().micCheck()->message().contains(QStringLiteral("not in the list")));
    }
}
