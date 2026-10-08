#include "vox/plugins/voice_edit.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace vox::plugins {
namespace {

Error missingBlock() {
    return makeError(ErrorCode::InvalidArgument, "That effect is no longer part of this voice.");
}

const EffectDescriptor* descriptorOf(const VoicePreset& preset, std::size_t block,
                                     const EffectRegistry& registry) {
    return block < preset.blocks.size() ? registry.find(preset.blocks[block].effect) : nullptr;
}

/// Calls `f` on every macro target.
template <class F>
void forEachTarget(VoicePreset& preset, const F& f) {
    for (MacroSpec& macro : preset.macros) {
        for (MacroTarget& target : macro.targets) {
            f(target);
        }
    }
}

std::string uniqueMacroId(const VoicePreset& preset, const std::string& base) {
    const auto used = [&preset](const std::string& id) {
        return id == "bass" || id == "treble" ||
               std::ranges::any_of(preset.macros, [&id](const MacroSpec& m) { return m.id == id; });
    };
    std::string id = base;
    for (int n = 2; used(id); ++n) {
        id = base + "-" + std::to_string(n);
    }
    return id;
}

} // namespace

Status insertBlock(VoicePreset& preset, std::size_t index, std::string_view effectId,
                   const EffectRegistry& registry) {
    if (registry.find(effectId) == nullptr) {
        return makeError(ErrorCode::UnknownEffect,
                         "There is no effect called \"" + std::string(effectId) + "\".");
    }
    if (preset.blocks.size() >= kMaxVoiceBlocks) {
        return makeError(ErrorCode::InvalidPreset, "A voice can hold up to " +
                                                       std::to_string(kMaxVoiceBlocks) +
                                                       " effects. Remove one to add another.");
    }
    index = std::min(index, preset.blocks.size());
    preset.blocks.insert(std::next(preset.blocks.begin(), static_cast<std::ptrdiff_t>(index)),
                         BlockSpec{std::string(effectId), {}, false});
    forEachTarget(preset, [index](MacroTarget& t) {
        if (t.block >= index) {
            ++t.block;
        }
    });
    return {};
}

Status removeBlock(VoicePreset& preset, std::size_t index) {
    if (index >= preset.blocks.size()) {
        return missingBlock();
    }
    preset.blocks.erase(std::next(preset.blocks.begin(), static_cast<std::ptrdiff_t>(index)));
    for (MacroSpec& macro : preset.macros) {
        std::erase_if(macro.targets, [index](const MacroTarget& t) { return t.block == index; });
    }
    std::erase_if(preset.macros, [](const MacroSpec& m) { return m.targets.empty(); });
    forEachTarget(preset, [index](MacroTarget& t) {
        if (t.block > index) {
            --t.block;
        }
    });
    return {};
}

Status moveBlock(VoicePreset& preset, std::size_t from, std::size_t to) {
    if (from >= preset.blocks.size() || to >= preset.blocks.size()) {
        return missingBlock();
    }
    if (from == to) {
        return {};
    }
    const auto at = [&preset](std::size_t i) {
        return std::next(preset.blocks.begin(), static_cast<std::ptrdiff_t>(i));
    };
    if (from < to) {
        std::rotate(at(from), at(from + 1), at(to + 1));
    } else {
        std::rotate(at(to), at(from), at(from + 1));
    }
    forEachTarget(preset, [from, to](MacroTarget& t) {
        if (t.block == from) {
            t.block = to;
        } else if (from < to && t.block > from && t.block <= to) {
            --t.block;
        } else if (from > to && t.block >= to && t.block < from) {
            ++t.block;
        }
    });
    return {};
}

Result<float> setBlockParameter(VoicePreset& preset, std::size_t block, std::size_t param,
                                float value, const EffectRegistry& registry) {
    const EffectDescriptor* d = descriptorOf(preset, block, registry);
    if (d == nullptr || param >= d->params.size()) {
        return missingBlock();
    }
    const ParamSpec& spec = d->params[param];
    const float v = spec.clamp(value);
    auto& params = preset.blocks[block].params;
    const auto it =
        std::ranges::find_if(params, [&spec](const auto& p) { return p.first == spec.id; });
    if (it != params.end()) {
        it->second = v;
    } else {
        params.emplace_back(spec.id, v);
    }
    return v;
}

float blockParameter(const VoicePreset& preset, std::size_t block, std::size_t param,
                     const EffectRegistry& registry) {
    const EffectDescriptor* d = descriptorOf(preset, block, registry);
    if (d == nullptr || param >= d->params.size()) {
        return 0.0F;
    }
    const ParamSpec& spec = d->params[param];
    for (const auto& [id, value] : preset.blocks[block].params) {
        if (id == spec.id) {
            return spec.clamp(value);
        }
    }
    return spec.defaultValue;
}

std::optional<std::size_t> macroControlling(const VoicePreset& preset, std::size_t block,
                                            std::string_view param) {
    for (std::size_t m = 0; m < preset.macros.size(); ++m) {
        const bool controls =
            std::ranges::any_of(preset.macros[m].targets, [&](const MacroTarget& t) {
                return t.block == block && t.param == param;
            });
        if (controls) {
            return m;
        }
    }
    return std::nullopt;
}

Result<std::size_t> exposeParameter(VoicePreset& preset, std::size_t block, std::size_t param,
                                    const EffectRegistry& registry) {
    const EffectDescriptor* d = descriptorOf(preset, block, registry);
    if (d == nullptr || param >= d->params.size()) {
        return missingBlock();
    }
    const ParamSpec& spec = d->params[param];
    if (spec.kind != ParamKind::Continuous) {
        return makeError(ErrorCode::InvalidArgument,
                         "Only settings with a range can become quick sliders.");
    }
    if (macroControlling(preset, block, spec.id)) {
        return makeError(ErrorCode::InvalidArgument,
                         "\"" + spec.name + "\" already has a quick slider.");
    }
    if (preset.macros.size() >= kMaxVoiceMacros) {
        return makeError(ErrorCode::InvalidPreset,
                         "A voice can have up to " + std::to_string(kMaxVoiceMacros) +
                             " quick sliders. Remove one to add another.");
    }
    MacroSpec macro;
    macro.id = uniqueMacroId(preset, spec.id);
    const bool nameTaken = std::ranges::any_of(
        preset.macros, [&spec](const MacroSpec& m) { return m.name == spec.name; });
    macro.name = nameTaken ? d->name + " " + spec.name : spec.name;
    const bool logarithmic = spec.scale == ParamScale::Logarithmic && spec.min > 0.0F;
    macro.targets.push_back({block, spec.id, spec.min, spec.max,
                             logarithmic ? MacroCurve::Exponential : MacroCurve::Linear});
    macro.defaultPosition = spec.toNormalized(blockParameter(preset, block, param, registry));
    preset.macros.push_back(std::move(macro));
    return preset.macros.size() - 1;
}

Status removeMacro(VoicePreset& preset, std::size_t index, const EffectRegistry& registry) {
    if (index >= preset.macros.size()) {
        return makeError(ErrorCode::InvalidArgument, "That quick slider no longer exists.");
    }
    // The settings it drove keep the values it gave them, so nothing jumps.
    for (const ParamChange& c :
         evaluateMacro(preset, index, preset.macros[index].defaultPosition, registry)) {
        static_cast<void>(setBlockParameter(preset, c.block, c.param, c.value, registry));
    }
    preset.macros.erase(std::next(preset.macros.begin(), static_cast<std::ptrdiff_t>(index)));
    return {};
}

VoicePreset duplicateVoice(const VoicePreset& source, std::string id, std::string name) {
    VoicePreset copy = source;
    copy.id = std::move(id);
    copy.name = std::move(name);
    copy.builtIn = false;
    copy.expect = {};
    return copy;
}

Status validateVoice(const VoicePreset& preset, const EffectRegistry& registry) {
    auto parsed = parseVoicePreset(serializeVoicePreset(preset), registry);
    if (!parsed) {
        return parsed.error();
    }
    return {};
}

} // namespace vox::plugins
