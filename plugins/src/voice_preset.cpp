#include "vox/plugins/voice_preset.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace vox::plugins {
namespace {

using Json = nlohmann::json;

constexpr int kFormatVersion = 1;

Error presetError(ErrorCode code, const std::string& where, const std::string& what) {
    return makeError(code, "Voice file problem at " + where + ": " + what);
}

Result<std::string> requireString(const Json& object, const char* key, const std::string& where) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string() || it->get<std::string>().empty()) {
        return presetError(ErrorCode::InvalidPreset, where + "." + key,
                           "a non-empty text value is required.");
    }
    return it->get<std::string>();
}

std::string optionalString(const Json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

/// Converts a JSON parameter value (number, boolean, or choice name) for `spec`.
Result<float> parameterValue(const Json& value, const ParamSpec& spec, const std::string& where) {
    if (value.is_boolean()) {
        return value.get<bool>() ? 1.0F : 0.0F;
    }
    if (value.is_string()) {
        const auto name = value.get<std::string>();
        for (std::size_t i = 0; i < spec.choices.size(); ++i) {
            if (spec.choices[i] == name) {
                return static_cast<float>(i);
            }
        }
        std::string options;
        for (const auto& c : spec.choices) {
            options += (options.empty() ? "" : ", ") + c;
        }
        return presetError(ErrorCode::ParameterOutOfRange, where,
                           "\"" + name + "\" is not one of: " +
                               (options.empty() ? "(no named values)" : options) + ".");
    }
    if (!value.is_number()) {
        return presetError(ErrorCode::InvalidPreset, where,
                           "expected a number, true/false, or a choice name.");
    }
    const auto v = value.get<float>();
    if (!std::isfinite(v) || v < spec.min - 1e-4F || v > spec.max + 1e-4F) {
        return presetError(ErrorCode::ParameterOutOfRange, where,
                           std::to_string(v) + " is outside " + std::to_string(spec.min) + " to " +
                               std::to_string(spec.max) +
                               (spec.unit.empty() ? "" : " " + spec.unit) + ".");
    }
    return spec.clamp(v);
}

float interpolate(const MacroTarget& t, float position) {
    const float p = std::clamp(position, 0.0F, 1.0F);
    if (t.curve == MacroCurve::Exponential && t.from > 0.0F && t.to > 0.0F) {
        return t.from * std::pow(t.to / t.from, p);
    }
    return t.from + (t.to - t.from) * p;
}

} // namespace

const std::vector<std::string>& voiceCategories() {
    static const std::vector<std::string> kCategories{"Natural", "Character", "Creature",
                                                      "Machine", "Device",    "Space",
                                                      "Place",   "Musical",   "Utility"};
    return kCategories;
}

namespace {

Status parseHeader(const Json& root, VoicePreset& preset) {
    auto id = requireString(root, "id", "voice");
    if (!id) {
        return id.error();
    }
    auto name = requireString(root, "name", "voice");
    if (!name) {
        return name.error();
    }
    auto category = requireString(root, "category", "voice");
    if (!category) {
        return category.error();
    }
    preset.id = id.value();
    preset.name = name.value();
    preset.category = category.value();
    const auto& categories = voiceCategories();
    if (std::find(categories.begin(), categories.end(), preset.category) == categories.end()) {
        return presetError(ErrorCode::InvalidPreset, "voice.category",
                           "\"" + preset.category + "\" is not a known category.");
    }
    preset.description = optionalString(root, "description");
    preset.icon = optionalString(root, "icon");
    preset.color = optionalString(root, "color");
    if (const auto tags = root.find("tags"); tags != root.end() && tags->is_array()) {
        for (const auto& t : *tags) {
            if (t.is_string()) {
                preset.tags.push_back(t.get<std::string>());
            }
        }
    }
    if (const auto gain = root.find("outputGainDb"); gain != root.end()) {
        if (!gain->is_number() || gain->get<float>() < -24.0F || gain->get<float>() > 24.0F) {
            return presetError(ErrorCode::ParameterOutOfRange, "voice.outputGainDb",
                               "must be a number from -24 to 24.");
        }
        preset.outputGainDb = gain->get<float>();
    }
    return {};
}

Result<BlockSpec> parseBlock(const Json& block, const std::string& where,
                             const EffectRegistry& registry) {
    if (!block.is_object()) {
        return presetError(ErrorCode::InvalidPreset, where, "each block must be an object.");
    }
    auto effect = requireString(block, "effect", where);
    if (!effect) {
        return effect.error();
    }
    const EffectDescriptor* descriptor = registry.find(effect.value());
    if (descriptor == nullptr) {
        return presetError(ErrorCode::UnknownEffect, where + ".effect",
                           "unknown effect \"" + effect.value() +
                               "\". Update Voxwright or remove this block.");
    }
    BlockSpec spec;
    spec.effect = effect.value();
    spec.bypassed = block.value("bypassed", false);
    const auto params = block.find("params");
    if (params == block.end()) {
        return spec;
    }
    if (!params->is_object()) {
        return presetError(ErrorCode::InvalidPreset, where + ".params",
                           "must be an object of name: value.");
    }
    for (const auto& [key, value] : params->items()) {
        std::string field = where;
        field += ".params.";
        field += key;
        const std::size_t index = descriptor->indexOf(key);
        if (index >= descriptor->params.size()) {
            std::string what = "the ";
            what += descriptor->name;
            what += " effect has no parameter \"";
            what += key;
            what += "\".";
            return presetError(ErrorCode::UnknownParameter, field, what);
        }
        auto v = parameterValue(value, descriptor->params[index], field);
        if (!v) {
            return v.error();
        }
        spec.params.emplace_back(key, v.value());
    }
    return spec;
}

Result<MacroTarget> parseTarget(const Json& target, const std::string& where,
                                const VoicePreset& preset, const EffectRegistry& registry) {
    MacroTarget mt;
    if (!target.is_object() || !target.contains("block") || !target["block"].is_number_unsigned()) {
        return presetError(ErrorCode::InvalidPreset, where + ".block",
                           "a block index is required.");
    }
    mt.block = target["block"].get<std::size_t>();
    if (mt.block >= preset.blocks.size()) {
        return presetError(ErrorCode::InvalidPreset, where + ".block",
                           "block " + std::to_string(mt.block) + " does not exist.");
    }
    auto param = requireString(target, "param", where);
    if (!param) {
        return param.error();
    }
    mt.param = param.value();
    const EffectDescriptor* descriptor = registry.find(preset.blocks[mt.block].effect);
    const std::size_t index = descriptor->indexOf(mt.param);
    if (index >= descriptor->params.size()) {
        return presetError(ErrorCode::UnknownParameter, where + ".param",
                           "the " + descriptor->name + " effect has no parameter \"" + mt.param +
                               "\".");
    }
    const ParamSpec& spec = descriptor->params[index];
    for (const char* end : {"from", "to"}) {
        if (!target.contains(end)) {
            return presetError(ErrorCode::InvalidPreset, where + "." + end, "a value is required.");
        }
        auto v = parameterValue(target[end], spec, where + "." + end);
        if (!v) {
            return v.error();
        }
        (std::string_view(end) == "from" ? mt.from : mt.to) = v.value();
    }
    mt.curve = target.value("curve", std::string("linear")) == "exponential"
                   ? MacroCurve::Exponential
                   : MacroCurve::Linear;
    return mt;
}

Status parseMacros(const Json& root, const EffectRegistry& registry, VoicePreset& preset) {
    const auto macros = root.find("macros");
    if (macros == root.end()) {
        return {};
    }
    if (!macros->is_array()) {
        return presetError(ErrorCode::InvalidPreset, "voice.macros", "must be a list.");
    }
    std::set<std::string> ids;
    for (std::size_t m = 0; m < macros->size(); ++m) {
        const Json& macro = (*macros)[m];
        const std::string where = "macros[" + std::to_string(m) + "]";
        if (!macro.is_object()) {
            return presetError(ErrorCode::InvalidPreset, where, "each macro must be an object.");
        }
        auto id = requireString(macro, "id", where);
        if (!id) {
            return id.error();
        }
        auto name = requireString(macro, "name", where);
        if (!name) {
            return name.error();
        }
        MacroSpec spec;
        spec.id = id.value();
        spec.name = name.value();
        if (!ids.insert(spec.id).second || spec.id == "bass" || spec.id == "treble") {
            return presetError(ErrorCode::InvalidPreset, where + ".id",
                               "\"" + spec.id + "\" is used twice or reserved.");
        }
        spec.defaultPosition = std::clamp(macro.value("default", 0.5F), 0.0F, 1.0F);
        const auto targets = macro.find("targets");
        if (targets == macro.end() || !targets->is_array() || targets->empty()) {
            return presetError(ErrorCode::InvalidPreset, where + ".targets",
                               "at least one target is required.");
        }
        for (std::size_t t = 0; t < targets->size(); ++t) {
            auto target = parseTarget((*targets)[t], where + ".targets[" + std::to_string(t) + "]",
                                      preset, registry);
            if (!target) {
                return target.error();
            }
            spec.targets.push_back(std::move(target).value());
        }
        preset.macros.push_back(std::move(spec));
    }
    return {};
}

void parseExpect(const Json& root, VoicePreset& preset) {
    const auto expect = root.find("expect");
    if (expect == root.end() || !expect->is_object()) {
        return;
    }
    if (expect->contains("pitchShiftSemitones")) {
        preset.expect.pitchShiftSemitones = (*expect)["pitchShiftSemitones"].get<float>();
    }
    if (expect->contains("fixedPitchHz")) {
        preset.expect.fixedPitchHz = (*expect)["fixedPitchHz"].get<float>();
    }
    preset.expect.unpitched = expect->value("unpitched", false);
    preset.expect.maxLatencyMs = expect->value("maxLatencyMs", 60.0F);
}

} // namespace

Result<VoicePreset> parseVoicePreset(std::string_view json, const EffectRegistry& registry) {
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return makeError(ErrorCode::PresetParseError,
                         "This voice file is not valid JSON. If you edited it by hand, check for a "
                         "missing comma, quote, or bracket.");
    }
    const auto version = root.value("version", 0);
    if (version != kFormatVersion) {
        return presetError(ErrorCode::InvalidPreset, "version",
                           "unsupported format version " + std::to_string(version) + " (expected " +
                               std::to_string(kFormatVersion) + ").");
    }
    VoicePreset preset;
    if (auto header = parseHeader(root, preset); !header) {
        return header.error();
    }
    const auto blocks = root.find("blocks");
    if (blocks == root.end() || !blocks->is_array()) {
        return presetError(ErrorCode::InvalidPreset, "voice.blocks",
                           "a list of effect blocks is required.");
    }
    for (std::size_t b = 0; b < blocks->size(); ++b) {
        auto block = parseBlock((*blocks)[b], "blocks[" + std::to_string(b) + "]", registry);
        if (!block) {
            return block.error();
        }
        preset.blocks.push_back(std::move(block).value());
    }
    if (auto macros = parseMacros(root, registry, preset); !macros) {
        return macros.error();
    }
    parseExpect(root, preset);
    return preset;
}

std::string serializeVoicePreset(const VoicePreset& preset) {
    Json root = Json::object();
    root["version"] = kFormatVersion;
    root["id"] = preset.id;
    root["name"] = preset.name;
    root["category"] = preset.category;
    root["description"] = preset.description;
    root["icon"] = preset.icon;
    root["color"] = preset.color;
    root["tags"] = preset.tags;
    root["outputGainDb"] = preset.outputGainDb;
    Json blocks = Json::array();
    for (const BlockSpec& b : preset.blocks) {
        Json block = Json::object();
        block["effect"] = b.effect;
        if (b.bypassed) {
            block["bypassed"] = true;
        }
        Json params = Json::object();
        for (const auto& [key, value] : b.params) {
            params[key] = value;
        }
        block["params"] = params;
        blocks.push_back(block);
    }
    root["blocks"] = blocks;
    Json macros = Json::array();
    for (const MacroSpec& m : preset.macros) {
        Json macro = Json::object();
        macro["id"] = m.id;
        macro["name"] = m.name;
        macro["default"] = m.defaultPosition;
        Json targets = Json::array();
        for (const MacroTarget& t : m.targets) {
            targets.push_back(
                {{"block", t.block},
                 {"param", t.param},
                 {"from", t.from},
                 {"to", t.to},
                 {"curve", t.curve == MacroCurve::Exponential ? "exponential" : "linear"}});
        }
        macro["targets"] = targets;
        macros.push_back(macro);
    }
    root["macros"] = macros;
    return root.dump(2);
}

std::vector<ParamChange> evaluateMacro(const VoicePreset& preset, std::size_t macroIndex,
                                       float position, const EffectRegistry& registry) {
    std::vector<ParamChange> changes;
    if (macroIndex >= preset.macros.size()) {
        return changes;
    }
    for (const MacroTarget& t : preset.macros[macroIndex].targets) {
        const EffectDescriptor* d = registry.find(preset.blocks[t.block].effect);
        if (d == nullptr) {
            continue;
        }
        const std::size_t index = d->indexOf(t.param);
        if (index < d->params.size()) {
            changes.push_back({t.block, index, d->params[index].clamp(interpolate(t, position))});
        }
    }
    return changes;
}

std::vector<float> resolveBlockParameters(const VoicePreset& preset, std::size_t block,
                                          const std::vector<float>& macroPositions,
                                          const EffectRegistry& registry) {
    std::vector<float> values;
    if (block >= preset.blocks.size()) {
        return values;
    }
    const EffectDescriptor* d = registry.find(preset.blocks[block].effect);
    if (d == nullptr) {
        return values;
    }
    for (const ParamSpec& p : d->params) {
        values.push_back(p.defaultValue);
    }
    for (const auto& [key, value] : preset.blocks[block].params) {
        const std::size_t index = d->indexOf(key);
        if (index < values.size()) {
            values[index] = value;
        }
    }
    for (std::size_t m = 0; m < preset.macros.size(); ++m) {
        const float position =
            m < macroPositions.size() ? macroPositions[m] : preset.macros[m].defaultPosition;
        for (const ParamChange& c : evaluateMacro(preset, m, position, registry)) {
            if (c.block == block) {
                values[c.param] = c.value;
            }
        }
    }
    return values;
}

} // namespace vox::plugins
