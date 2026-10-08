#pragma once

#include <vox/core/result.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace vox::devices {

/// A sound decoded to mono float at the engine rate.
struct DecodedClip {
    std::vector<float> samples;
    double sampleRate = 48000.0;
    double durationSeconds = 0.0;
    std::uint32_t sourceSampleRate = 0;
    std::uint32_t sourceChannels = 0;
};

struct DecodeLimits {
    std::uintmax_t maxFileBytes = 50ULL * 1024 * 1024;
    double maxSeconds = 600.0;
};

/// Decodes WAV, MP3, FLAC, or OGG Vorbis, mixes to mono, and converts to
/// `targetRate` with the best-quality resampler. Each failure (missing file,
/// too large, too long, unsupported format, corrupt data) has its own error.
[[nodiscard]] Result<DecodedClip> decodeAudioFile(const std::filesystem::path& path,
                                                  double targetRate = 48000.0,
                                                  const DecodeLimits& limits = {});

/// Same, from an encoded file held in memory (bundled sounds, tests).
[[nodiscard]] Result<DecodedClip> decodeAudioMemory(std::span<const std::uint8_t> encoded,
                                                    const std::string& label,
                                                    double targetRate = 48000.0,
                                                    const DecodeLimits& limits = {});

/// File extensions the decoder accepts (lower case, with the dot).
[[nodiscard]] const std::vector<std::string>& supportedAudioExtensions();

} // namespace vox::devices
