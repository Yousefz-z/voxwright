#include "settings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

using vox::ErrorCode;
using vox::app::AppSettings;
using vox::app::SettingsStore;

TEST_CASE("Settings survive a save and load unchanged", "[app][settings]") {
    const QTemporaryDir dir;
    const SettingsStore store(dir.filePath(QStringLiteral("nested/settings.json")));
    AppSettings s;
    s.inputId = QStringLiteral("mic-7");
    s.inputName = QStringLiteral("Desk Microphone");
    s.virtualMicId = QStringLiteral("cable");
    s.virtualMicChosen = true;
    s.useVirtualMic = false;
    s.periodFrames = 256;
    s.exclusive = true;
    s.hearMyself = true;
    s.noiseReduction = true;
    s.noiseReductionStrength = 0.75F;
    s.gate = true;
    s.gateThresholdDb = -42.5F;
    s.inputGainDb = 3.0F;
    s.mix.soundsDb = -9.0F;
    s.currentVoiceId = QStringLiteral("deep-baritone");
    s.favorites = {QStringLiteral("cathedral"), QStringLiteral("old-telephone")};
    s.voices.insert(QStringLiteral("deep-baritone"), {{0.25F, 0.8F}, -3.0F, 2.5F});
    REQUIRE(store.save(s));

    const auto loaded = store.load();
    REQUIRE_FALSE(loaded.problem);
    const AppSettings& l = loaded.settings;
    CHECK(l.inputId == s.inputId);
    CHECK(l.inputName == s.inputName);
    CHECK(l.virtualMicId == s.virtualMicId);
    CHECK(l.virtualMicChosen);
    CHECK_FALSE(l.useVirtualMic);
    CHECK(l.periodFrames == 256);
    CHECK(l.exclusive);
    CHECK(l.hearMyself);
    CHECK(l.noiseReduction);
    CHECK(l.noiseReductionStrength == 0.75F);
    CHECK(l.gate);
    CHECK(l.gateThresholdDb == -42.5F);
    CHECK(l.inputGainDb == 3.0F);
    CHECK(l.mix.soundsDb == -9.0F);
    CHECK(l.currentVoiceId == s.currentVoiceId);
    CHECK(l.favorites == s.favorites);
    REQUIRE(l.voices.contains(QStringLiteral("deep-baritone")));
    const auto& v = l.voices.value(QStringLiteral("deep-baritone"));
    CHECK(v.macroPositions == std::vector<float>{0.25F, 0.8F});
    CHECK(v.bassDb == -3.0F);
    CHECK(v.trebleDb == 2.5F);
}

TEST_CASE("A missing settings file means defaults without a warning", "[app][settings]") {
    const QTemporaryDir dir;
    const auto loaded = SettingsStore(dir.filePath(QStringLiteral("settings.json"))).load();
    CHECK_FALSE(loaded.problem);
    CHECK(loaded.settings.currentVoiceId == QStringLiteral("clean-voice"));
}

TEST_CASE("A damaged settings file is kept aside and reported", "[app][settings][errors]") {
    const QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.json"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(R"({"devices": {"inputId": )");
    }
    const auto loaded = SettingsStore(path).load();
    REQUIRE(loaded.problem);
    const vox::Error problem = loaded.problem.value_or(vox::Error{});
    CHECK(problem.code == ErrorCode::SettingsDamaged);
    CHECK(problem.message.find("settings.json.damaged") != std::string::npos);
    CHECK(QFile::exists(path + QStringLiteral(".damaged")));
    CHECK_FALSE(QFile::exists(path));
    CHECK(loaded.settings.currentVoiceId == QStringLiteral("clean-voice"));
}

TEST_CASE("Settings from a newer version load what they can and say so", "[app][settings]") {
    const QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("settings.json"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write(R"({"version": 99, "currentVoice": "cathedral", "futureFeature": {"x": 1}})");
    }
    const auto loaded = SettingsStore(path).load();
    REQUIRE(loaded.problem);
    CHECK(loaded.problem.value_or(vox::Error{}).code == ErrorCode::SettingsFromNewerVersion);
    CHECK(loaded.settings.currentVoiceId == QStringLiteral("cathedral"));
}

TEST_CASE("Saving where no folder can be created fails with the reason",
          "[app][settings][errors]") {
    const QTemporaryDir dir;
    const QString blocker = dir.filePath(QStringLiteral("not-a-folder"));
    {
        QFile f(blocker);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    const SettingsStore store(blocker + QStringLiteral("/settings.json"));
    const auto saved = store.save(AppSettings{});
    REQUIRE_FALSE(saved);
    CHECK(saved.error().code == ErrorCode::FileWriteFailed);
    CHECK(saved.error().message.find("could not be created") != std::string::npos);
}
