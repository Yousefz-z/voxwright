#include "app_test_support.hpp"
#include "hotkeys/hotkey_keys.hpp"

#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

using namespace vox::app;
using namespace vox::app::test;

namespace {

QKeyCombination combo(Qt::KeyboardModifiers m, Qt::Key k) {
    return QKeyCombination(m, k);
}

} // namespace

TEST_CASE("Qt keys map to Windows virtual keys", "[app][hotkeys]") {
    CHECK(windowsVirtualKey(combo(Qt::ControlModifier, Qt::Key_A)) == 0x41U);
    CHECK(windowsVirtualKey(combo(Qt::NoModifier, Qt::Key_0)) == 0x30U);
    CHECK(windowsVirtualKey(combo(Qt::KeypadModifier, Qt::Key_5)) == 0x65U);
    CHECK(windowsVirtualKey(combo(Qt::NoModifier, Qt::Key_F13)) == 0x7CU);
    CHECK(windowsVirtualKey(combo(Qt::NoModifier, Qt::Key_Space)) == 0x20U);
    CHECK(windowsVirtualKey(combo(Qt::NoModifier, Qt::Key_Semicolon)) == 0xBAU);
    CHECK(windowsVirtualKey(combo(Qt::KeypadModifier, Qt::Key_Plus)) == 0x6BU);
    CHECK_FALSE(windowsVirtualKey(combo(Qt::NoModifier, Qt::Key_VolumeUp)));
}

TEST_CASE("Qt keys map to macOS key codes and Carbon modifiers", "[app][hotkeys]") {
    CHECK(macVirtualKey(combo(Qt::NoModifier, Qt::Key_A)) == 0x00U);
    CHECK(macVirtualKey(combo(Qt::NoModifier, Qt::Key_Z)) == 0x06U);
    CHECK(macVirtualKey(combo(Qt::NoModifier, Qt::Key_1)) == 0x12U);
    CHECK(macVirtualKey(combo(Qt::KeypadModifier, Qt::Key_0)) == 0x52U);
    CHECK(macVirtualKey(combo(Qt::NoModifier, Qt::Key_F1)) == 0x7AU);
    CHECK(macVirtualKey(combo(Qt::NoModifier, Qt::Key_F20)) == 0x5AU);
    CHECK(macVirtualKey(combo(Qt::NoModifier, Qt::Key_Return)) == 0x24U);
    CHECK(macVirtualKey(combo(Qt::NoModifier, Qt::Key_Period)) == 0x2FU);
    // Qt's Control is the Command key on macOS; Meta is Control.
    CHECK(macModifierFlags(Qt::ControlModifier | Qt::ShiftModifier) == (0x0100U | 0x0200U));
    CHECK(macModifierFlags(Qt::MetaModifier) == 0x1000U);
    CHECK(macModifierFlags(Qt::AltModifier) == 0x0800U);
    CHECK(isModifierKey(Qt::Key_Shift));
    CHECK_FALSE(isModifierKey(Qt::Key_Q));
}

TEST_CASE("Hotkeys are validated, checked for conflicts, and saved", "[app][hotkeys]") {
    TestApp t;
    auto* hotkeys = t.context().hotkeys();
    CHECK(hotkeys->sequenceFromKey(Qt::Key_V,
                                   static_cast<int>(Qt::ControlModifier | Qt::AltModifier)) ==
          QStringLiteral("Ctrl+Alt+V"));
    CHECK(
        hotkeys->sequenceFromKey(Qt::Key_Control, static_cast<int>(Qt::ControlModifier)).isEmpty());

    CHECK(hotkeys->assignAction(QStringLiteral("voiceChanger"), QStringLiteral("Ctrl+Alt+V"))
              .isEmpty());
    const QString conflict =
        hotkeys->assignAction(QStringLiteral("hearMyself"), QStringLiteral("Ctrl+Alt+V"));
    CHECK(conflict.contains(QStringLiteral("already used for")));
    CHECK(conflict.contains(QStringLiteral("Voice changer")));
    CHECK_FALSE(
        hotkeys->assignAction(QStringLiteral("mute"), QStringLiteral("Ctrl+Alt+V, X")).isEmpty());
    CHECK_FALSE(
        hotkeys->assignAction(QStringLiteral("mute"), QStringLiteral("VolumeUp")).isEmpty());
    CHECK_FALSE(
        hotkeys->assignAction(QStringLiteral("noSuchAction"), QStringLiteral("F2")).isEmpty());
    REQUIRE(t.hotkeys().bindings().size() == 1);

    REQUIRE(t.context().saveNow());
    QFile file(t.settingsPath());
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto saved = QJsonDocument::fromJson(file.readAll()).object().value("hotkeys").toObject();
    CHECK(saved.value("voiceChanger").toString() == QStringLiteral("Ctrl+Alt+V"));

    // Clearing frees the key for another action.
    CHECK(hotkeys->assignAction(QStringLiteral("voiceChanger"), {}).isEmpty());
    CHECK(hotkeys->assignAction(QStringLiteral("hearMyself"), QStringLiteral("Ctrl+Alt+V"))
              .isEmpty());
}

TEST_CASE("Action hotkeys toggle features and step through voices", "[app][hotkeys]") {
    TestApp t;
    auto& ctx = t.context();
    REQUIRE(ctx.hotkeys()
                ->assignAction(QStringLiteral("voiceChanger"), QStringLiteral("F2"))
                .isEmpty());
    REQUIRE(ctx.hotkeys()->assignAction(QStringLiteral("mute"), QStringLiteral("F3")).isEmpty());
    REQUIRE(
        ctx.hotkeys()->assignAction(QStringLiteral("nextVoice"), QStringLiteral("F4")).isEmpty());
    REQUIRE(
        ctx.hotkeys()->assignAction(QStringLiteral("randomVoice"), QStringLiteral("F5")).isEmpty());
    const bool voiceOn = ctx.audio()->voiceEnabled();
    REQUIRE(t.hotkeys().press(combo(Qt::NoModifier, Qt::Key_F2), true));
    t.hotkeys().press(combo(Qt::NoModifier, Qt::Key_F2), false);
    QCoreApplication::processEvents();
    CHECK(ctx.audio()->voiceEnabled() != voiceOn);
    t.hotkeys().press(combo(Qt::NoModifier, Qt::Key_F3), true);
    QCoreApplication::processEvents();
    CHECK(ctx.audio()->muted());
    const QString before = ctx.voices()->currentVoiceId();
    t.hotkeys().press(combo(Qt::NoModifier, Qt::Key_F4), true);
    QCoreApplication::processEvents();
    CHECK(ctx.voices()->currentVoiceId() != before);
    const QString afterNext = ctx.voices()->currentVoiceId();
    t.hotkeys().press(combo(Qt::NoModifier, Qt::Key_F5), true);
    QCoreApplication::processEvents();
    CHECK(ctx.voices()->currentVoiceId() != afterNext);
}

TEST_CASE("Voice hotkeys switch to their voice and are saved", "[app][hotkeys]") {
    TestApp t;
    auto& ctx = t.context();
    auto* hotkeys = ctx.hotkeys();
    REQUIRE(ctx.voices()->selectVoice(QStringLiteral("clean-voice")));
    REQUIRE(hotkeys->assignVoice(QStringLiteral("cathedral"), QStringLiteral("Ctrl+1")).isEmpty());
    CHECK(hotkeys->voiceHotkey(QStringLiteral("cathedral")) == QStringLiteral("Ctrl+1"));
    CHECK_FALSE(hotkeys->assignVoice({}, QStringLiteral("F6")).isEmpty());

    // Conflicts name the voice, in both directions.
    const QString taken = hotkeys->assignAction(QStringLiteral("mute"), QStringLiteral("Ctrl+1"));
    CHECK(taken.contains(QStringLiteral("Cathedral")));
    REQUIRE(hotkeys->assignAction(QStringLiteral("mute"), QStringLiteral("F3")).isEmpty());
    CHECK(hotkeys->assignVoice(QStringLiteral("deep-baritone"), QStringLiteral("F3"))
              .contains(QStringLiteral("Mute")));

    REQUIRE(t.hotkeys().press(combo(Qt::ControlModifier, Qt::Key_1), true));
    t.hotkeys().press(combo(Qt::ControlModifier, Qt::Key_1), false);
    QCoreApplication::processEvents();
    CHECK(ctx.voices()->currentVoiceId() == QStringLiteral("cathedral"));

    REQUIRE(ctx.saveNow());
    {
        QFile file(t.settingsPath());
        REQUIRE(file.open(QIODevice::ReadOnly));
        const auto saved =
            QJsonDocument::fromJson(file.readAll()).object().value("hotkeys").toObject();
        CHECK(saved.value("voice:cathedral").toString() == QStringLiteral("Ctrl+1"));
    }

    // Cleared, the key does nothing and the entry is gone.
    REQUIRE(hotkeys->assignVoice(QStringLiteral("cathedral"), {}).isEmpty());
    CHECK(hotkeys->voiceHotkey(QStringLiteral("cathedral")).isEmpty());
    CHECK_FALSE(t.hotkeys().press(combo(Qt::ControlModifier, Qt::Key_1), true));
    CHECK(t.hotkeys().bindings().size() == 1); // mute
}

TEST_CASE("Saved voice hotkeys are active at startup", "[app][hotkeys]") {
    TestApp t({.cable = true}, QStringLiteral(R"({"version": 1, "currentVoice": "clean-voice",
                                 "hotkeys": {"voice:deep-baritone": "F8"}})"));
    auto& ctx = t.context();
    CHECK(ctx.hotkeys()->voiceHotkey(QStringLiteral("deep-baritone")) == QStringLiteral("F8"));
    REQUIRE(t.hotkeys().press(combo(Qt::NoModifier, Qt::Key_F8), true));
    QCoreApplication::processEvents();
    CHECK(ctx.voices()->currentVoiceId() == QStringLiteral("deep-baritone"));
}

TEST_CASE("Push-to-talk follows the talk key without waiting for the UI", "[app][hotkeys]") {
    TestApp t;
    auto& ctx = t.context();
    ctx.audio()->setTransmitMode(1); // push-to-talk
    ctx.audio()->setReleaseDelayMs(0.0);
    REQUIRE(ctx.hotkeys()->assignAction(QStringLiteral("talk"), QStringLiteral("F13")).isEmpty());
    const auto voice = vox::testing::sine(220.0, 0.5, 48000.0, 0.3F);
    t.pump(0.3, voice);
    CHECK_FALSE(ctx.engine().stats().transmitting);
    // The press reaches the engine through the immediate handler: no event
    // processing happens between the press and the audio.
    t.hotkeys().press(combo(Qt::NoModifier, Qt::Key_F13), true);
    t.pump(0.1, voice);
    CHECK(ctx.engine().stats().transmitting);
    t.hotkeys().press(combo(Qt::NoModifier, Qt::Key_F13), false);
    t.pump(0.2, voice);
    CHECK_FALSE(ctx.engine().stats().transmitting);
}

TEST_CASE("Without global hotkey support the reason is given", "[app][hotkeys][errors]") {
    const TestApp t;
    // The fixture's hotkeys are supported; check the unsupported stub directly.
    ManualHotkeys unsupported(false);
    const auto result = unsupported.setBindings({{1, combo(Qt::NoModifier, Qt::Key_F1)}});
    REQUIRE_FALSE(result);
    CHECK(result.error().code == vox::ErrorCode::HotkeysUnsupported);
    CHECK(unsupported.setBindings({}));
}
