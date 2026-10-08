#pragma once

#include <vox/core/result.hpp>

#include <filesystem>
#include <span>
#include <vector>

namespace vox::testing {

struct WavData {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples; ///< Interleaved, normalized to [-1, 1].
};

/// Reads 16/24/32-bit PCM and 32-bit float RIFF WAVE files.
[[nodiscard]] Result<WavData> readWav(const std::filesystem::path& path);

/// Writes interleaved samples as 32-bit float WAVE.
[[nodiscard]] Status writeWav(const std::filesystem::path& path, std::span<const float> samples,
                              int sampleRate, int channels = 1);

} // namespace vox::testing
