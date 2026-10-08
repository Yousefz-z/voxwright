#pragma once

#include "vox/plugins/registry.hpp"
#include "vox/plugins/voice_preset.hpp"

#include <vox/core/result.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace vox::plugins {

// Editing operations for the voice designer. Each one keeps the preset
// valid: macro targets follow their blocks when blocks move, and a block's
// removal takes its macro targets with it. Failures leave the preset as it
// was and say why in words the designer can show.

/// Inserts a block of effect `effectId` with default parameters at `index`
/// (clamped to the end).
[[nodiscard]] Status insertBlock(VoicePreset& preset, std::size_t index, std::string_view effectId,
                                 const EffectRegistry& registry);

/// Removes block `index`, its macro targets, and any macro left without one.
[[nodiscard]] Status removeBlock(VoicePreset& preset, std::size_t index);

/// Moves block `from` so it ends up at position `to`.
[[nodiscard]] Status moveBlock(VoicePreset& preset, std::size_t from, std::size_t to);

/// Stores a parameter value (clamped to its range); returns the stored value.
[[nodiscard]] Result<float> setBlockParameter(VoicePreset& preset, std::size_t block,
                                              std::size_t param, float value,
                                              const EffectRegistry& registry);

/// The value a block parameter has before macros apply: the preset's value
/// or the effect's default.
[[nodiscard]] float blockParameter(const VoicePreset& preset, std::size_t block, std::size_t param,
                                   const EffectRegistry& registry);

/// The first macro that sets this parameter, if any.
[[nodiscard]] std::optional<std::size_t>
macroControlling(const VoicePreset& preset, std::size_t block, std::string_view param);

/// Adds a quick slider that sweeps one continuous parameter over its whole
/// range, starting where the parameter is now. Returns the macro's index.
[[nodiscard]] Result<std::size_t> exposeParameter(VoicePreset& preset, std::size_t block,
                                                  std::size_t param,
                                                  const EffectRegistry& registry);

/// Removes a quick slider. The settings it drove keep the values it gave
/// them at its starting position, so the voice does not change.
[[nodiscard]] Status removeMacro(VoicePreset& preset, std::size_t index,
                                 const EffectRegistry& registry);

/// A copy that belongs to the user: new id and name, not built in, and
/// without the measured expectations of the original.
[[nodiscard]] VoicePreset duplicateVoice(const VoicePreset& source, std::string id,
                                         std::string name);

/// Checks the whole preset the way loading a file would.
[[nodiscard]] Status validateVoice(const VoicePreset& preset, const EffectRegistry& registry);

} // namespace vox::plugins
