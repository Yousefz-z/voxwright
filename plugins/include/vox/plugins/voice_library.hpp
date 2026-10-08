#pragma once

#include "vox/plugins/registry.hpp"
#include "vox/plugins/voice_preset.hpp"

#include <vox/core/result.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace vox::plugins {

struct EmbeddedText {
    std::string_view name;
    std::string_view text;
};

/// The JSON sources of the built-in voices, compiled into the binary.
[[nodiscard]] std::span<const EmbeddedText> builtinVoiceSources() noexcept;

/// Parses every built-in voice. Fails (naming the file) if any is invalid,
/// which the test suite guarantees never happens in a release.
[[nodiscard]] Result<std::vector<VoicePreset>> loadBuiltinVoices(const EffectRegistry& registry);

} // namespace vox::plugins
