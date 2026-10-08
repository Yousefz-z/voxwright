#include "voice_metrics.hpp"

#include <vox/plugins/registry.hpp>
#include <vox/plugins/voice_edit.hpp>
#include <vox/plugins/voice_library.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace vox::plugins;
using Catch::Approx;

namespace {

const EffectRegistry& registry() {
    return EffectRegistry::builtin();
}

VoicePreset builtinVoice(const std::string& id) {
    auto voices = loadBuiltinVoices(registry());
    REQUIRE(voices);
    const auto& all = voices.value();
    const auto it = std::ranges::find_if(all, [&id](const VoicePreset& p) { return p.id == id; });
    REQUIRE(it != all.end());
    return *it;
}

VoicePreset blank() {
    VoicePreset p;
    p.id = "custom-test";
    p.name = "Test";
    p.category = "Character";
    return p;
}

std::size_t param(const std::string& effect, const std::string& id) {
    const EffectDescriptor* d = registry().find(effect);
    REQUIRE(d != nullptr);
    const std::size_t i = d->indexOf(id);
    REQUIRE(i < d->params.size());
    return i;
}

} // namespace

TEST_CASE("Quick sliders follow their effect when effects are added, moved, and removed",
          "[plugins][designer]") {
    // Deep Baritone: [pitch, eq]; both quick sliders drive the pitch block.
    VoicePreset v = builtinVoice("deep-baritone");
    REQUIRE(v.blocks.size() == 2);
    REQUIRE(v.macros.size() == 2);
    const auto pitchBefore = resolveBlockParameters(v, 0, {0.8F, 0.2F}, registry());

    REQUIRE(insertBlock(v, 0, "distortion", registry()));
    CHECK(v.blocks[0].effect == "distortion");
    CHECK(v.blocks[1].effect == "pitch");
    CHECK(v.macros[0].targets[0].block == 1);
    CHECK(resolveBlockParameters(v, 1, {0.8F, 0.2F}, registry()) == pitchBefore);

    REQUIRE(moveBlock(v, 1, 2)); // [distortion, eq, pitch]
    CHECK(v.blocks[2].effect == "pitch");
    CHECK(v.blocks[1].effect == "eq");
    CHECK(v.macros[1].targets[0].block == 2);
    CHECK(resolveBlockParameters(v, 2, {0.8F, 0.2F}, registry()) == pitchBefore);

    REQUIRE(moveBlock(v, 2, 0)); // [pitch, distortion, eq]
    CHECK(v.blocks[0].effect == "pitch");
    CHECK(v.blocks[2].effect == "eq");
    CHECK(v.macros[0].targets[0].block == 0);
    REQUIRE(validateVoice(v, registry()));

    // Removing an effect before the pitch block shifts the targets back.
    REQUIRE(removeBlock(v, 1)); // [pitch, eq]
    CHECK(v.macros[0].targets[0].block == 0);
    // Removing the pitch block takes both quick sliders with it.
    REQUIRE(removeBlock(v, 0));
    CHECK(v.blocks.size() == 1);
    CHECK(v.macros.empty());
    REQUIRE(validateVoice(v, registry()));
}

TEST_CASE("Editing mistakes are refused with a reason and change nothing",
          "[plugins][designer][errors]") {
    VoicePreset v = blank();
    const auto unknown = insertBlock(v, 0, "teleporter", registry());
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == vox::ErrorCode::UnknownEffect);
    CHECK(unknown.error().message.find("teleporter") != std::string::npos);

    for (std::size_t i = 0; i < kMaxVoiceBlocks; ++i) {
        REQUIRE(insertBlock(v, i, "eq", registry()));
    }
    const auto full = insertBlock(v, 0, "eq", registry());
    REQUIRE_FALSE(full);
    CHECK(full.error().message.find(std::to_string(kMaxVoiceBlocks)) != std::string::npos);
    CHECK(v.blocks.size() == kMaxVoiceBlocks);

    CHECK_FALSE(moveBlock(v, 0, kMaxVoiceBlocks));
    CHECK_FALSE(removeBlock(v, kMaxVoiceBlocks));
    CHECK_FALSE(setBlockParameter(v, kMaxVoiceBlocks, 0, 1.0F, registry()));
    CHECK_FALSE(setBlockParameter(v, 0, 999, 1.0F, registry()));
    CHECK_FALSE(removeMacro(v, 0, registry()));
    CHECK(v.blocks.size() == kMaxVoiceBlocks);
}

TEST_CASE("A quick slider starts where its setting is and leaves it there when removed",
          "[plugins][designer]") {
    VoicePreset v = blank();
    REQUIRE(insertBlock(v, 0, "eq", registry()));
    const std::size_t bass = param("eq", "lowShelfDb");
    const std::size_t cutHz = param("eq", "lowCutHz");

    // Values are clamped to the setting's range.
    CHECK(setBlockParameter(v, 0, bass, 99.0F, registry()).value() == 24.0F);
    REQUIRE(setBlockParameter(v, 0, bass, 5.0F, registry()));
    CHECK(blockParameter(v, 0, bass, registry()) == 5.0F);
    REQUIRE(setBlockParameter(v, 0, cutHz, 150.0F, registry()));

    // Linear and logarithmic settings both keep their value.
    const auto first = exposeParameter(v, 0, bass, registry());
    REQUIRE(first);
    const auto second = exposeParameter(v, 0, cutHz, registry());
    REQUIRE(second);
    CHECK(v.macros[second.value()].targets[0].curve == MacroCurve::Exponential);
    const auto values = resolveBlockParameters(v, 0, {}, registry());
    CHECK(values[bass] == Approx(5.0F).margin(1e-4));
    CHECK(values[cutHz] == Approx(150.0F).epsilon(1e-4));
    CHECK(macroControlling(v, 0, "lowShelfDb") == first.value());
    REQUIRE(validateVoice(v, registry()));

    CHECK_FALSE(exposeParameter(v, 0, bass, registry()));                  // already has one
    CHECK_FALSE(exposeParameter(v, 0, param("eq", "lowCut"), registry())); // a toggle

    // The same setting on a second equalizer gets a name that tells them apart.
    REQUIRE(insertBlock(v, 1, "eq", registry()));
    const auto again = exposeParameter(v, 1, bass, registry());
    REQUIRE(again);
    CHECK(v.macros[first.value()].name == "Bass");
    CHECK(v.macros[again.value()].name == "Equalizer Bass");
    CHECK(v.macros[again.value()].id != v.macros[first.value()].id);
    REQUIRE(exposeParameter(v, 0, param("eq", "mid1Db"), registry()));
    const auto fifth = exposeParameter(v, 0, param("eq", "mid2Db"), registry()); // limit 4
    REQUIRE_FALSE(fifth);
    CHECK(fifth.error().message.find(std::to_string(kMaxVoiceMacros)) != std::string::npos);

    // Moved to the top and removed, the setting keeps the top value.
    v.macros[first.value()].defaultPosition = 1.0F;
    REQUIRE(removeMacro(v, first.value(), registry()));
    CHECK(blockParameter(v, 0, bass, registry()) == 24.0F);
    CHECK_FALSE(macroControlling(v, 0, "lowShelfDb"));
}

TEST_CASE("Copies of built-in voices belong to the user and round-trip through files",
          "[plugins][designer]") {
    const VoicePreset original = builtinVoice("deep-baritone");
    VoicePreset copy = duplicateVoice(original, "custom-abc", "Deep Baritone (mine)");
    CHECK_FALSE(copy.builtIn);
    CHECK_FALSE(copy.expect.pitchShiftSemitones.has_value());
    CHECK(copy.blocks.size() == original.blocks.size());
    REQUIRE(insertBlock(copy, 2, "tremolo", registry()));
    copy.blocks[2].bypassed = true;

    auto parsed = parseVoicePreset(serializeVoicePreset(copy), registry());
    REQUIRE(parsed);
    const VoicePreset& back = parsed.value();
    CHECK(back.id == "custom-abc");
    REQUIRE(back.blocks.size() == 3);
    CHECK(back.blocks[2].effect == "tremolo");
    CHECK(back.blocks[2].bypassed);
    REQUIRE(back.macros.size() == copy.macros.size());
    CHECK(back.macros[0].targets[0].block == copy.macros[0].targets[0].block);
}

TEST_CASE("Voice files over the effect and quick slider limits are refused",
          "[plugins][designer][errors]") {
    std::string blocks;
    for (std::size_t i = 0; i <= kMaxVoiceBlocks; ++i) {
        blocks += std::string(i == 0 ? "" : ",") + R"({"effect": "eq"})";
    }
    const std::string tooMany = R"({"version": 1, "id": "custom-x", "name": "X",
        "category": "Character", "blocks": [)" +
                                blocks + "]}";
    const auto parsed = parseVoicePreset(tooMany, registry());
    REQUIRE_FALSE(parsed);
    CHECK(parsed.error().message.find("voice.blocks") != std::string::npos);

    std::string macros;
    for (std::size_t i = 0; i <= kMaxVoiceMacros; ++i) {
        macros += std::string(i == 0 ? "" : ",") + R"({"id": "m)" + std::to_string(i) +
                  R"(", "name": "M", "targets": [{"block": 0, "param": "lowShelfDb",
                  "from": 0, "to": 6}]})";
    }
    const std::string tooManyMacros = R"({"version": 1, "id": "custom-x", "name": "X",
        "category": "Character", "blocks": [{"effect": "eq"}], "macros": [)" +
                                      macros + "]}";
    const auto parsedMacros = parseVoicePreset(tooManyMacros, registry());
    REQUIRE_FALSE(parsedMacros);
    CHECK(parsedMacros.error().message.find("voice.macros") != std::string::npos);
}

TEST_CASE("Voices built in the designer measure as designed", "[plugins][designer][voices]") {
    using vox::plugins::test::measureVoice;
    VoicePreset v = blank();
    REQUIRE(insertBlock(v, 0, "pitch", registry()));
    const std::size_t semitones = param("pitch", "semitones");
    const std::size_t formant = param("pitch", "formant");

    // An octave up.
    REQUIRE(setBlockParameter(v, 0, semitones, 12.0F, registry()));
    const auto up = measureVoice(v, registry());
    REQUIRE(up.built);
    CHECK(up.pitchShiftSemitones == Approx(12.0).margin(0.5));

    // Formant alone: the pitch stays, the spectrum brightens.
    REQUIRE(setBlockParameter(v, 0, semitones, 0.0F, registry()));
    REQUIRE(setBlockParameter(v, 0, formant, 4.0F, registry()));
    const auto bright = measureVoice(v, registry());
    REQUIRE(bright.built);
    CHECK(bright.pitchShiftSemitones == Approx(0.0).margin(0.3));
    CHECK(bright.centroidRatio > 1.1);

    // Bypassed, the block leaves the voice as it was.
    v.blocks[0].bypassed = true;
    const auto bypassed = measureVoice(v, registry());
    CHECK(bypassed.pitchShiftSemitones == Approx(0.0).margin(0.3));
    CHECK(bypassed.centroidRatio == Approx(1.0).margin(0.05));
}
