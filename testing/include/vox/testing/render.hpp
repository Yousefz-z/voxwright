#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace vox::testing {

/// Offline render path for verifying effects: runs `processor.process(span)`
/// over `input` in fixed-size blocks, exactly as the real-time engine would.
template <class Processor>
std::vector<float> renderOffline(Processor& processor, std::span<const float> input,
                                 std::size_t blockSize) {
    std::vector<float> out(input.begin(), input.end());
    for (std::size_t pos = 0; pos < out.size(); pos += blockSize) {
        const std::size_t n = std::min(blockSize, out.size() - pos);
        processor.process(std::span<float>(out).subspan(pos, n));
    }
    return out;
}

/// Same, with random block sizes in [1, maxBlock] to mimic irregular device
/// callbacks.
template <class Processor>
std::vector<float> renderOfflineIrregular(Processor& processor, std::span<const float> input,
                                          std::size_t maxBlock, std::uint32_t seed = 3) {
    std::vector<float> out(input.begin(), input.end());
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> sizes(1, maxBlock);
    for (std::size_t pos = 0; pos < out.size();) {
        const std::size_t n = std::min(sizes(rng), out.size() - pos);
        processor.process(std::span<float>(out).subspan(pos, n));
        pos += n;
    }
    return out;
}

} // namespace vox::testing
