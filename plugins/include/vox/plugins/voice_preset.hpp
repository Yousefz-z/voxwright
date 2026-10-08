#pragma once

#include "vox/plugins/effect.hpp"
#include "vox/plugins/registry.hpp"

#include <vox/core/result.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vox::plugins {

/// Most effect blocks a voice may hold. Bounds the processing cost of a
/// voice built in the designer or imported from a file.
inline constexpr std::size_t kMaxVoiceBlocks = 12;
/// Most quick sliders (macros) a voice may have.
inline constexpr std::size_t kMaxVoiceMacros = 4;

/// One effect block in a voice: effect id plus parameter values by id.
/// Parameters not listed keep the effect's defaults.
struct BlockSpec {
    std::string effect;
    std::vector<std::pair<std::string, float>> params;
    bool bypassed = false;
};

enum class MacroCurve {
    Linear,      ///< value = from + (to - from) * position
    Exponential, ///< equal steps are equal ratios (from and to must be > 0)
};

struct MacroTarget {
    std::size_t block = 0;
    std::string param;
    float from = 0.0F;
    float to = 1.0F;
    MacroCurve curve = MacroCurve::Linear;
};

/// A quick slider on the voice card that drives one or more block
/// parameters. Position is in [0, 1].
struct MacroSpec {
    std::string id;
    std::string name;
    float defaultPosition = 0.5F;
    std::vector<MacroTarget> targets;
};

/// Measured properties a built-in voice must keep (checked by tests).
struct VoiceExpectation {
    std::optional<float> pitchShiftSemitones; ///< Median f0 change on a test phrase.
    std::optional<float> fixedPitchHz;        ///< Monotone output pitch.
    bool unpitched = false;                   ///< Output must not be periodic (whisper).
    float maxLatencyMs = 60.0F;
};

struct VoicePreset {
    std::string id;
    std::string name;
    std::string category;
    std::string description;
    std::string icon;  ///< Glyph name from the app's icon set.
    std::string color; ///< Accent color, "#rrggbb".
    std::vector<std::string> tags;
    float outputGainDb = 0.0F;
    std::vector<BlockSpec> blocks;
    std::vector<MacroSpec> macros;
    VoiceExpectation expect;
    bool builtIn = false;
};

/// One parameter assignment produced by evaluating a macro.
struct ParamChange {
    std::size_t block = 0;
    std::size_t param = 0;
    float value = 0.0F;
};

/// Categories shown in the voice browser, in display order.
[[nodiscard]] const std::vector<std::string>& voiceCategories();

/// Parses and validates a voice preset (JSON). Every block, parameter, range,
/// and macro target is checked against the registry; the error message names
/// the exact field.
[[nodiscard]] Result<VoicePreset> parseVoicePreset(std::string_view json,
                                                   const EffectRegistry& registry);

/// Serializes a preset to JSON (stable field order, two-space indent).
[[nodiscard]] std::string serializeVoicePreset(const VoicePreset& preset);

/// Evaluates a macro at `position` into parameter changes.
[[nodiscard]] std::vector<ParamChange> evaluateMacro(const VoicePreset& preset,
                                                     std::size_t macroIndex, float position,
                                                     const EffectRegistry& registry);

/// All parameter values of block `block` after applying the preset and the
/// given macro positions (one per macro), in descriptor order.
[[nodiscard]] std::vector<float> resolveBlockParameters(const VoicePreset& preset,
                                                        std::size_t block,
                                                        const std::vector<float>& macroPositions,
                                                        const EffectRegistry& registry);

} // namespace vox::plugins
