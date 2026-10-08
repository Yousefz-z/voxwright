#include <vox/plugins/voice_preset.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using Catch::Approx;
using vox::ErrorCode;
using vox::plugins::EffectRegistry;
using vox::plugins::parseVoicePreset;

namespace {

const char* const kValid = R"({
  "version": 1, "id": "test", "name": "Test Voice", "category": "Creature",
  "description": "d", "icon": "creature", "color": "#123456", "tags": ["a"],
  "outputGainDb": -2.5,
  "blocks": [
    {"effect": "pitch", "params": {"semitones": -5, "mode": "Robot", "robotHz": 120}},
    {"effect": "reverb", "params": {"mix": 0.3}}
  ],
  "macros": [
    {"id": "depth", "name": "Depth", "default": 0.25,
     "targets": [{"block": 0, "param": "semitones", "from": 0, "to": -12}]},
    {"id": "robot", "name": "Robot", "default": 0.5,
     "targets": [{"block": 0, "param": "robotHz", "from": 60, "to": 240, "curve": "exponential"}]}
  ],
  "expect": {"pitchShiftSemitones": -5, "maxLatencyMs": 40}
})";

ErrorCode errorOf(const std::string& json) {
    const auto r = parseVoicePreset(json, EffectRegistry::builtin());
    REQUIRE_FALSE(r.hasValue());
    return r.error().code;
}

std::string withBlocks(const std::string& blocks) {
    return R"({"version": 1, "id": "x", "name": "X", "category": "Utility", "blocks": )" + blocks +
           "}";
}

} // namespace

TEST_CASE("A valid preset parses with choices, macros, and expectations", "[plugins][preset]") {
    const auto r = parseVoicePreset(kValid, EffectRegistry::builtin());
    REQUIRE(r.hasValue());
    const auto& p = r.value();
    CHECK(p.id == "test");
    CHECK(p.outputGainDb == Approx(-2.5F));
    REQUIRE(p.blocks.size() == 2);
    CHECK(p.blocks[0].params.size() == 3);
    const auto mode = std::find_if(p.blocks[0].params.begin(), p.blocks[0].params.end(),
                                   [](const auto& kv) { return kv.first == "mode"; });
    REQUIRE(mode != p.blocks[0].params.end());
    CHECK(mode->second == Approx(1.0F)); // "Robot" is choice index 1
    REQUIRE(p.macros.size() == 2);
    CHECK(p.expect.pitchShiftSemitones.value_or(0.0F) == Approx(-5.0F));
    CHECK(p.expect.maxLatencyMs == Approx(40.0F));
}

TEST_CASE("Macros map linearly and exponentially", "[plugins][preset]") {
    const auto p = parseVoicePreset(kValid, EffectRegistry::builtin()).value();
    const auto& registry = EffectRegistry::builtin();
    auto linear = vox::plugins::evaluateMacro(p, 0, 0.5F, registry);
    REQUIRE(linear.size() == 1);
    CHECK(linear[0].value == Approx(-6.0F));
    auto exponential = vox::plugins::evaluateMacro(p, 1, 0.5F, registry);
    REQUIRE(exponential.size() == 1);
    CHECK(exponential[0].value == Approx(120.0F)); // geometric midpoint of 60 and 240
    // Defaults resolve through the macro positions.
    const auto values = vox::plugins::resolveBlockParameters(p, 0, {}, registry);
    CHECK(values[0] == Approx(-3.0F)); // macro default 0.25 overrides the block's -5
}

TEST_CASE("Serialization round-trips", "[plugins][preset]") {
    const auto original = parseVoicePreset(kValid, EffectRegistry::builtin()).value();
    const auto json = vox::plugins::serializeVoicePreset(original);
    const auto again = parseVoicePreset(json, EffectRegistry::builtin());
    REQUIRE(again.hasValue());
    CHECK(again.value().name == original.name);
    CHECK(again.value().blocks.size() == original.blocks.size());
    CHECK(again.value().macros.size() == original.macros.size());
    CHECK(vox::plugins::serializeVoicePreset(again.value()) == json);
}

TEST_CASE("Each kind of preset mistake gets its own error", "[plugins][preset]") {
    CHECK(errorOf("{not json") == ErrorCode::PresetParseError);
    CHECK(errorOf(R"({"version": 9})") == ErrorCode::InvalidPreset);
    CHECK(errorOf(withBlocks(R"([{"effect": "teleporter"}])")) == ErrorCode::UnknownEffect);
    CHECK(errorOf(withBlocks(R"([{"effect": "reverb", "params": {"wetness": 1}}])")) ==
          ErrorCode::UnknownParameter);
    CHECK(errorOf(withBlocks(R"([{"effect": "reverb", "params": {"mix": 7}}])")) ==
          ErrorCode::ParameterOutOfRange);
    CHECK(errorOf(withBlocks(R"([{"effect": "pitch", "params": {"mode": "Wobble"}}])")) ==
          ErrorCode::ParameterOutOfRange);
    CHECK(errorOf(R"({"version": 1, "id": "x", "name": "X", "category": "Nope", "blocks": []})") ==
          ErrorCode::InvalidPreset);
    const std::string badMacro = R"({"version": 1, "id": "x", "name": "X", "category": "Utility",
        "blocks": [{"effect": "reverb"}],
        "macros": [{"id": "m", "name": "M", "targets": [{"block": 3, "param": "mix", "from": 0, "to": 1}]}]})";
    CHECK(errorOf(badMacro) == ErrorCode::InvalidPreset);
}

TEST_CASE("Error messages name the exact field", "[plugins][preset]") {
    const auto r = parseVoicePreset(
        withBlocks(R"([{"effect": "reverb"}, {"effect": "echo", "params": {"time": 99999}}])"),
        EffectRegistry::builtin());
    REQUIRE_FALSE(r.hasValue());
    CHECK(r.error().message.find("blocks[1].params.time") != std::string::npos);
    CHECK(r.error().message.find("2000") != std::string::npos);
}
