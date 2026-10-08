#pragma once

#include <vox/core/result.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace vox::plugins {

/// A sound that ships with Voxwright. Every one is synthesized from code
/// (oscillators, noise, filters, envelopes); no recording is involved.
struct BuiltinSound {
    std::string_view id;
    std::string_view name;
    std::string_view category; ///< Reactions, Effects, Music, Ambience, Alerts.
    std::string_view icon;     ///< Glyph name from the app's icon set.
    std::string_view color;    ///< "#rrggbb"
};

[[nodiscard]] std::span<const BuiltinSound> builtinSounds() noexcept;

/// Loudness every built-in sound is normalized to (BS.1770 integrated),
/// unless its peak would then exceed -1 dBFS.
inline constexpr double kBuiltinSoundLufs = -18.0;

/// Renders a built-in sound as mono samples at `sampleRate` (control
/// thread; takes a few milliseconds). Fails only for an unknown id.
[[nodiscard]] Result<std::vector<float>> renderBuiltinSound(std::string_view id,
                                                            double sampleRate = 48000.0);

} // namespace vox::plugins
