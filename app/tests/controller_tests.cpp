#include "app_test_support.hpp"

#include <vox/testing/signals.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

using namespace vox::app;
using namespace vox::app::test;

TEST_CASE("First start picks the virtual cable and starts audio", "[app][controller]") {
    TestApp t;
    auto& ctx = t.context();
    CHECK(ctx.audio()->running());
    CHECK(ctx.audio()->virtualCableFound());
    CHECK(ctx.audio()->virtualMicDeviceId() == QStringLiteral("cable"));
    CHECK(ctx.audio()->chatAppMicrophoneName() ==
          QStringLiteral("CABLE Output (VB-Audio Virtual Cable)"));
    CHECK(ctx.notifications()->contains(QStringLiteral("virtual-mic-picked")));
    const auto active = ctx.engine().activeDevices();
    REQUIRE(active.virtualMic);
    REQUIRE(active.input);
    CHECK(active.virtualMic.value_or(vox::devices::StreamInfo{}).deviceId == "cable");
    CHECK(active.input.value_or(vox::devices::StreamInfo{}).deviceId == "mic");
}

TEST_CASE("Without a virtual cable the app runs and explains how to get one", "[app][controller]") {
    TestApp t({.cable = false});
    auto& ctx = t.context();
    CHECK(ctx.audio()->running());
    CHECK_FALSE(ctx.audio()->virtualCableFound());
    CHECK(ctx.notifications()->contains(QStringLiteral("no-virtual-cable")));
    CHECK_FALSE(ctx.engine().activeDevices().virtualMic);
    CHECK(ctx.engine().activeDevices().monitor);
}

TEST_CASE("A saved microphone that is missing falls back to the default and says so",
          "[app][controller]") {
    TestApp t({.cable = true},
              R"({"devices": {"inputId": "usb-mic-1", "inputName": "Old USB Mic"}})");
    auto& ctx = t.context();
    CHECK(ctx.audio()->running());
    CHECK(ctx.engine().activeDevices().input.value_or(vox::devices::StreamInfo{}).deviceId ==
          "mic");
    CHECK(ctx.notifications()
              ->messageFor(QStringLiteral("device-0"))
              .contains(QStringLiteral("Old USB Mic")));
    // The user's choice is kept for when the device comes back.
    CHECK(ctx.settings().inputId == QStringLiteral("usb-mic-1"));
}

TEST_CASE("Selecting a voice switches the engine and is remembered", "[app][controller]") {
    TestApp t;
    auto& ctx = t.context();
    t.pump(0.2);
    CHECK(ctx.engine().stats().virtualMicLatency.voiceMs == 0.0);
    const QSignalSpy changed(ctx.voices(), &VoiceController::currentVoiceChanged);
    REQUIRE(ctx.voices()->selectVoice(QStringLiteral("deep-baritone")));
    CHECK(changed.count() == 1);
    CHECK(ctx.voices()->currentName() == QStringLiteral("Deep Baritone"));
    t.pump(0.2);
    // The engine now runs the pitch-shifted voice (30 ms of PSOLA latency).
    CHECK(ctx.engine().stats().virtualMicLatency.voiceMs > 25.0);
    CHECK_FALSE(ctx.voices()->selectVoice(QStringLiteral("no-such-voice")));

    REQUIRE(ctx.saveNow());
    QFile file(t.settingsPath());
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    CHECK(json.value("currentVoice").toString() == QStringLiteral("deep-baritone"));
}

TEST_CASE("Macro and tone changes are applied and saved per voice", "[app][controller]") {
    TestApp t;
    auto& ctx = t.context();
    REQUIRE(ctx.voices()->selectVoice(QStringLiteral("clean-voice")));
    REQUIRE_FALSE(ctx.voices()->macros().isEmpty());
    ctx.voices()->setMacro(0, 0.9);
    ctx.voices()->setTone(-4.0, 3.0);
    CHECK(ctx.voices()->macros().front().toMap().value("value").toDouble() ==
          Catch::Approx(0.9).margin(1e-6));
    CHECK(ctx.voices()->bassDb() == -4.0);
    const auto& saved = ctx.settings().voices.value(QStringLiteral("clean-voice"));
    CHECK(saved.macroPositions.front() == Catch::Approx(0.9F));
    CHECK(saved.trebleDb == 3.0F);
    t.pump(0.1);
    CHECK_FALSE(ctx.notifications()->contains(QStringLiteral("engine-busy")));
    ctx.voices()->resetCurrentVoice();
    CHECK(ctx.voices()->bassDb() == 0.0);
}

TEST_CASE("Favorites toggle and persist", "[app][controller]") {
    TestApp t;
    auto& ctx = t.context();
    ctx.voices()->toggleFavorite(QStringLiteral("cathedral"));
    CHECK(ctx.voices()->isFavorite(QStringLiteral("cathedral")));
    ctx.voices()->voices()->setCategory(VoiceFilterModel::favoritesCategory());
    CHECK(ctx.voices()->voices()->count() == 1);
    ctx.voices()->toggleFavorite(QStringLiteral("cathedral"));
    CHECK(ctx.voices()->voices()->count() == 0);
}

TEST_CASE("A lost device becomes a notification that clears when it returns", "[app][controller]") {
    TestApp t;
    auto& ctx = t.context();
    t.backend().disconnect("mic");
    t.pump(0.05);
    const QString key = QStringLiteral("device-0");
    REQUIRE(ctx.notifications()->contains(key));
    CHECK(ctx.notifications()->messageFor(key).contains(QStringLiteral("Studio Microphone")));
    CHECK(ctx.audio()->running());
    t.backend().addDevice({"mic", "Studio Microphone", DeviceKind::Capture, true, 48000, 1, false});
    t.pump(0.05);
    CHECK_FALSE(ctx.notifications()->contains(key));
}

TEST_CASE("A virtual cable installed while running is picked up", "[app][controller]") {
    TestApp t({.cable = false});
    auto& ctx = t.context();
    REQUIRE_FALSE(ctx.engine().activeDevices().virtualMic);
    t.backend().addDevice({"cable", "CABLE Input (VB-Audio Virtual Cable)", DeviceKind::Playback,
                           false, 48000, 2, false});
    t.pump(0.05);
    CHECK(ctx.audio()->virtualCableFound());
    CHECK(ctx.engine().activeDevices().virtualMic.value_or(vox::devices::StreamInfo{}).deviceId ==
          "cable");
}

TEST_CASE("Howling hear-myself is switched off with an explanation", "[app][controller]") {
    TestApp t;
    auto& ctx = t.context();
    ctx.audio()->setHearMyself(true);
    const auto howl = vox::testing::sine(2500.0, 1.0, 48000.0, 0.5F);
    t.pump(2.0, howl);
    CHECK_FALSE(ctx.audio()->hearMyself());
    CHECK(
        ctx.notifications()->messageFor(QStringLiteral("feedback")).contains(QStringLiteral("Hz")));
}

TEST_CASE("Audio that cannot start says which device and why", "[app][controller][errors]") {
    TestApp t;
    auto& ctx = t.context();
    REQUIRE(ctx.audio()->virtualMicDeviceId() == QStringLiteral("cable"));
    t.backend().failNextOpen("cable",
                             vox::makeError(vox::ErrorCode::DeviceInUseExclusive,
                                            "is being used by another app in exclusive mode."));
    ctx.audio()->restart();
    CHECK_FALSE(ctx.audio()->running());
    const QString message = ctx.notifications()->messageFor(QStringLiteral("engine"));
    CHECK(message.startsWith(QStringLiteral("Virtual microphone output:")));
    CHECK(message.contains(QStringLiteral("exclusive")));
    ctx.audio()->restart(); // the failure was one-off
    CHECK(ctx.audio()->running());
    CHECK_FALSE(ctx.notifications()->contains(QStringLiteral("engine")));
}

TEST_CASE("Toggles reach the engine and the settings file", "[app][controller]") {
    TestApp t;
    auto& ctx = t.context();
    ctx.audio()->setNoiseReduction(true);
    ctx.audio()->setGateEnabled(true);
    ctx.audio()->setGateThresholdDb(-35.0);
    ctx.audio()->setVoiceEnabled(false);
    t.pump(0.1);
    // Noise reduction now adds its 20 ms to the reported latency.
    CHECK(ctx.engine().stats().virtualMicLatency.noiseSuppressionMs == 20.0);
    REQUIRE(ctx.saveNow());
    QFile file(t.settingsPath());
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto input = QJsonDocument::fromJson(file.readAll()).object().value("input").toObject();
    CHECK(input.value("noiseReduction").toBool());
    CHECK(input.value("gate").toBool());
    CHECK(input.value("gateThresholdDb").toDouble() == -35.0);
    CHECK_FALSE(input.value("voiceEnabled").toBool());
}
