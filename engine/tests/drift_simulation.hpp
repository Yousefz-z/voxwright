#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::engine::test {

struct DriftResult {
    std::uint64_t underruns = 0;
    std::uint64_t overruns = 0;
    double meanTrimPpm = 0.0;      ///< Average trim over the last 10 s.
    double trimSpreadPpm = 0.0;    ///< Max minus min trim over the last 10 s.
    double meanFillMs = 0.0;       ///< Average ring fill (latency) over the last 10 s.
    std::vector<float> lastSecond; ///< Device output, mono.
};

/// Runs an OutputStage between a producer on the engine clock writing a
/// 1 kHz tone and a device whose clock runs `ppm` parts per million fast,
/// for `seconds` of simulated time. Producer callbacks are late by a random
/// 0 to `jitterMs` (not cumulative), like a loaded machine.
[[nodiscard]] DriftResult simulateDrift(double deviceRate, double ppm, double seconds,
                                        std::size_t producerBlock = 128,
                                        std::size_t deviceBlock = 128, double jitterMs = 1.5);

struct ProcessingSetup {
    double processingMs = 0.0;   ///< The write lands this long after its callback starts.
    double deviceOffsetMs = 0.0; ///< Device reads start this much later than producer callbacks.
    bool stampAtLanding = false; ///< push() gets the landing time, not the callback start.
};

/// simulateDrift() where the producer needs `processingMs` before its write
/// lands, as when the graph runs a voice inside the capture callback.
[[nodiscard]] DriftResult simulateDrift(double deviceRate, double ppm, double seconds,
                                        std::size_t producerBlock, std::size_t deviceBlock,
                                        double jitterMs, const ProcessingSetup& processing);

/// Ratio (dB) of a sine's energy to everything else (noise, distortion,
/// images) over the last second, from a Blackman-Harris spectrum with 1 Hz
/// bins. Slow frequency wander from the ratio trim stays inside the
/// +-10 Hz band counted as signal, as it is inaudible (under 0.1 cent).
[[nodiscard]] double toneToNoiseDb(std::span<const float> x, double hz, double rate);

} // namespace vox::engine::test
