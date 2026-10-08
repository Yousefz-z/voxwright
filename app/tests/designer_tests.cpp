#include "app_test_support.hpp"

#include <vox/plugins/registry.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

using namespace vox::app;
using namespace vox::app::test;
using Catch::Approx;

namespace {

int paramIndex(const char* effect, const char* id) {
    const auto* d = vox::plugins::EffectRegistry::builtin().find(effect);
    REQUIRE(d != nullptr);
    return static_cast<int>(d->indexOf(id));
}

bool hasVoice(AppContext& ctx, const QString& id) {
    return ctx.voices()->preset(id) != nullptr;
}

/// Largest sample-to-sample step in `x`.
float largestStep(std::span<const float> x) {
    float step = 0.0F;
    for (std::size_t i = 1; i < x.size(); ++i) {
        step = std::max(step, std::abs(x[i] - x[i - 1]));
    }
    return step;
}

/// The steepest step a sine of frequency `hz` and amplitude `peak` can take.
float sineStep(double hz, float peak) {
    return static_cast<float>(2.0 * std::numbers::pi * hz / 48000.0) * peak;
}

} // namespace

TEST_CASE("A new voice plays live while it is designed", "[app][designer]") {
    TestApp t;
    auto& ctx = t.context();
    auto* designer = ctx.designer();
    REQUIRE_FALSE(designer->editing());
    designer->newVoice();
    REQUIRE(designer->editing());
    CHECK(designer->isNew());
    CHECK(designer->previewing());
    REQUIRE(designer->blocks().size() == 1); // starts with a pitch block
    CHECK(designer->name() == QStringLiteral("My voice"));

    const int semitones = paramIndex("pitch", "semitones");
    CHECK(designer->setParameter(0, semitones, 30.0) == 24.0); // clamped
    REQUIRE(designer->setParameter(0, semitones, 12.0) == 12.0);

    vox::testing::VoiceSpec spec;
    spec.f0Hz = 150.0;
    spec.seconds = 1.0;
    const auto input = vox::testing::synthVoice(spec);
    const auto out = t.record(1.5, input);
    const auto tail = std::span<const float>(out).last(24000);
    const double f0 = vox::testing::medianVoiced(vox::testing::f0Track(tail, 48000.0, 60.0, 800.0));
    CHECK(f0 == Approx(300.0).epsilon(0.03));
}

TEST_CASE("Moving a setting or changing the chain never clicks", "[app][designer]") {
    TestApp t;
    auto* designer = t.context().designer();
    designer->newVoice();
    REQUIRE(designer->removeBlock(0).isEmpty());
    REQUIRE(designer->addBlock(QStringLiteral("eq")).isEmpty());
    const int bass = paramIndex("eq", "lowShelfDb");
    const auto tone = vox::testing::sine(100.0, 1.0, 48000.0, 0.3F);
    t.record(0.5, tone); // past the voice switch

    // Drag the bass from -12 to +6 dB in 60 steps, one per audio block.
    std::vector<float> out;
    for (int i = 0; i <= 60; ++i) {
        designer->setParameter(0, bass, -12.0 + 18.0 * i / 60.0);
        const auto block = t.record(128.0 / 48000.0, tone);
        out.insert(out.end(), block.begin(), block.end());
    }
    const float peak = vox::testing::peakAbs(out);
    INFO("largest step " << largestStep(out) << ", sine limit " << sineStep(100.0, peak));
    CHECK(largestStep(out) < 1.5F * sineStep(100.0, peak));

    // Removing the boosted equalizer crossfades back to the plain voice.
    const auto removed = designer->removeBlock(0);
    REQUIRE(removed.isEmpty());
    const auto after = t.record(0.2, tone);
    const float afterPeak = std::max(vox::testing::peakAbs(after), peak);
    CHECK(largestStep(after) < 1.5F * sineStep(100.0, afterPeak));
}

TEST_CASE("Saved voices join the voice list and come back after a restart", "[app][designer]") {
    QString file;
    QString id;
    {
        TestApp t;
        auto& ctx = t.context();
        auto* designer = ctx.designer();
        designer->newVoice();
        designer->setName(QStringLiteral("Tin Robot"));
        designer->setCategory(QStringLiteral("Machine"));
        designer->setIcon(QStringLiteral("machine"));
        designer->setColor(QStringLiteral("#5aa9f5"));
        REQUIRE(designer->addBlock(QStringLiteral("tremolo")).isEmpty());
        REQUIRE(designer->exposeParameter(0, paramIndex("pitch", "semitones")).isEmpty());
        id = QString::fromStdString(designer->draft().id);
        REQUIRE(designer->save().isEmpty());
        CHECK_FALSE(designer->editing());
        CHECK(ctx.voices()->currentVoiceId() == id);
        CHECK(ctx.voices()->currentName() == QStringLiteral("Tin Robot"));
        CHECK(ctx.voices()->macros().size() == 1);
        REQUIRE(designer->customVoices().size() == 1);
        CHECK(ctx.voices()->voiceCount() == 55);

        QFile saved(t.dataDir() + QStringLiteral("/voices/") + id + QStringLiteral(".json"));
        REQUIRE(saved.open(QIODevice::ReadOnly));
        file = QString::fromUtf8(saved.readAll());
        REQUIRE(ctx.saveNow());
    }
    TestApp restarted({.cable = true},
                      QStringLiteral(R"({"version": 1, "currentVoice": "%1"})").arg(id),
                      {{QStringLiteral("voices/") + id + QStringLiteral(".json"), file.toUtf8()}});
    auto& ctx = restarted.context();
    REQUIRE(hasVoice(ctx, id));
    CHECK(ctx.voices()->currentVoiceId() == id);
    CHECK(ctx.voices()->currentColor() == QStringLiteral("#5AA9F5"));
}

TEST_CASE("Built-in voices are edited as copies", "[app][designer]") {
    TestApp t;
    auto& ctx = t.context();
    auto* designer = ctx.designer();
    REQUIRE(ctx.voices()->selectVoice(QStringLiteral("deep-baritone")));
    ctx.voices()->setMacro(0, 0.9);
    REQUIRE(designer->editVoice(QStringLiteral("deep-baritone")));
    CHECK(designer->isNew());
    CHECK(designer->name() == QStringLiteral("Deep Baritone (mine)"));
    CHECK(designer->macros().front().toMap().value(QStringLiteral("position")).toDouble() ==
          Approx(0.9));
    REQUIRE(designer->addBlock(QStringLiteral("distortion")).isEmpty());
    REQUIRE(designer->save().isEmpty());

    const auto* original = ctx.voices()->preset(QStringLiteral("deep-baritone"));
    REQUIRE(original != nullptr);
    CHECK(original->builtIn);
    CHECK(original->blocks.size() == 2);
    CHECK(ctx.voices()->currentName() == QStringLiteral("Deep Baritone (mine)"));

    // Editing the copy changes it in place.
    const QString copyId = ctx.voices()->currentVoiceId();
    REQUIRE(designer->editVoice(copyId));
    CHECK_FALSE(designer->isNew());
    REQUIRE(designer->removeBlock(2).isEmpty());
    REQUIRE(designer->save().isEmpty());
    CHECK(ctx.voices()->preset(copyId)->blocks.size() == 2);
    CHECK(designer->customVoices().size() == 1);
}

TEST_CASE("Choosing another voice while designing pauses the preview", "[app][designer]") {
    TestApp t;
    auto& ctx = t.context();
    auto* designer = ctx.designer();
    designer->newVoice();
    REQUIRE(designer->previewing());
    REQUIRE(ctx.voices()->selectVoice(QStringLiteral("cathedral")));
    CHECK_FALSE(designer->previewing());
    CHECK(designer->editing());
    designer->preview();
    CHECK(designer->previewing());

    // Discarding goes back to the voice that was chosen.
    designer->discard();
    CHECK_FALSE(designer->editing());
    CHECK(ctx.voices()->currentVoiceId() == QStringLiteral("cathedral"));
    CHECK(ctx.voices()->voiceCount() == 54);
}

TEST_CASE("Saving needs a name and an effect", "[app][designer][errors]") {
    TestApp t;
    auto* designer = t.context().designer();
    designer->newVoice();
    designer->setName(QStringLiteral("   "));
    CHECK(designer->save().contains(QStringLiteral("name")));
    designer->setName(QStringLiteral("Empty"));
    REQUIRE(designer->removeBlock(0).isEmpty());
    CHECK(designer->save().contains(QStringLiteral("effect")));
    CHECK(designer->editing());
    CHECK_FALSE(designer->addBlock(QStringLiteral("teleporter")).isEmpty());
}

TEST_CASE("Deleting a voice removes it everywhere", "[app][designer]") {
    TestApp t;
    auto& ctx = t.context();
    auto* designer = ctx.designer();
    designer->newVoice();
    REQUIRE(designer->save().isEmpty());
    const QString id = ctx.voices()->currentVoiceId();
    ctx.voices()->toggleFavorite(id);
    REQUIRE(ctx.hotkeys()->assignVoice(id, QStringLiteral("F9")).isEmpty());

    CHECK_FALSE(designer->deleteVoice(QStringLiteral("cathedral")).isEmpty()); // built in
    REQUIRE(designer->deleteVoice(id).isEmpty());
    CHECK_FALSE(hasVoice(ctx, id));
    CHECK_FALSE(
        QFile::exists(t.dataDir() + QStringLiteral("/voices/") + id + QStringLiteral(".json")));
    CHECK_FALSE(ctx.voices()->isFavorite(id));
    CHECK(ctx.hotkeys()->voiceHotkey(id).isEmpty());
    CHECK(t.hotkeys().bindings().empty());
    CHECK(ctx.voices()->currentVoiceId() != id);
    CHECK_FALSE(ctx.voices()->currentVoiceId().isEmpty());
}

TEST_CASE("Voices export and import, and bad files are named", "[app][designer][errors]") {
    TestApp t;
    auto& ctx = t.context();
    auto* designer = ctx.designer();
    designer->newVoice();
    designer->setName(QStringLiteral("Shared"));
    REQUIRE(designer->save().isEmpty());
    const QString id = ctx.voices()->currentVoiceId();

    const QTemporaryDir out;
    const QString exported = out.filePath(QStringLiteral("shared"));
    REQUIRE(designer->exportVoice(id, QUrl::fromLocalFile(exported)).isEmpty());
    REQUIRE(QFile::exists(exported + QStringLiteral(".voxvoice")));

    QFile garbage(out.filePath(QStringLiteral("garbage.voxvoice")));
    REQUIRE(garbage.open(QIODevice::WriteOnly));
    garbage.write("not a voice");
    garbage.close();
    QFile unknown(out.filePath(QStringLiteral("future.voxvoice")));
    REQUIRE(unknown.open(QIODevice::WriteOnly));
    unknown.write(R"({"version": 1, "id": "x", "name": "Future", "category": "Character",
                     "blocks": [{"effect": "time-machine"}]})");
    unknown.close();

    const int imported = designer->importVoices(
        {QUrl::fromLocalFile(exported + QStringLiteral(".voxvoice")),
         QUrl::fromLocalFile(garbage.fileName()), QUrl::fromLocalFile(unknown.fileName()),
         QUrl::fromLocalFile(out.filePath(QStringLiteral("missing.voxvoice")))});
    CHECK(imported == 1);
    CHECK(designer->customVoices().size() == 2);
    const QString names = designer->customVoices().back().toMap().value("name").toString() +
                          designer->customVoices().front().toMap().value("name").toString();
    CHECK(names.contains(QStringLiteral("Shared 2")));

    REQUIRE(ctx.notifications()->contains(QStringLiteral("voice-import")));
    const QString text = ctx.notifications()->messageFor(QStringLiteral("voice-import"));
    CHECK(text.contains(QStringLiteral("garbage.voxvoice")));
    CHECK(text.contains(QStringLiteral("time-machine")));
    CHECK(text.contains(QStringLiteral("missing.voxvoice")));
}

TEST_CASE("A damaged voice file is set aside and reported", "[app][designer][errors]") {
    TestApp t({.cable = true}, {},
              {{QStringLiteral("voices/custom-broken.json"), QByteArray("{ not json")},
               {QStringLiteral("voices/custom-wrongname.json"),
                QByteArray(R"({"version": 1, "id": "custom-other", "name": "Other",
                              "category": "Character", "blocks": [{"effect": "eq"}]})")}});
    auto& ctx = t.context();
    CHECK(ctx.designer()->customVoices().isEmpty());
    CHECK(ctx.notifications()->contains(QStringLiteral("custom-voices")));
    const QDir voices(t.dataDir() + QStringLiteral("/voices"));
    CHECK(voices.exists(QStringLiteral("custom-broken.json.damaged")));
    CHECK(voices.exists(QStringLiteral("custom-wrongname.json.damaged")));
}
