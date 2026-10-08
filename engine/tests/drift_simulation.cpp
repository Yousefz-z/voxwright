#include "drift_simulation.hpp"

#include <vox/engine/output_stage.hpp>
#include <vox/engine/types.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vox::engine::test {

DriftResult simulateDrift(double deviceRate, double ppm, double seconds, std::size_t producerBlock,
                          std::size_t deviceBlock, double jitterMs) {
    OutputStage stage;
    const auto target = static_cast<std::size_t>(std::ceil(0.002 * deviceRate));
    stage.prepare(kEngineRate, deviceRate, target, producerBlock);

    const double producerPeriod = static_cast<double>(producerBlock) / kEngineRate;
    const double devicePeriod =
        static_cast<double>(deviceBlock) / (deviceRate * (1.0 + ppm * 1e-6));
    std::uint32_t seed = 12345;
    const auto jitter = [&seed, jitterMs] {
        seed = seed * 1664525U + 1013904223U;
        return static_cast<double>(seed >> 8) / 16777216.0 * jitterMs * 1e-3;
    };
    double producerNominal = 0.0;
    double producerTime = jitter();
    double deviceTime = 0.0;
    double phase = 0.0;
    std::vector<float> block(producerBlock);
    std::vector<float> out(deviceBlock);
    DriftResult result;
    double trimSum = 0.0;
    double trimMin = 2.0;
    double trimMax = 0.0;
    double fillSum = 0.0;
    std::size_t samples = 0;
    while (deviceTime < seconds) {
        if (producerTime <= deviceTime) {
            for (float& v : block) {
                v = static_cast<float>(0.5 * std::sin(phase));
                phase += 2.0 * std::numbers::pi * 1000.0 / kEngineRate;
            }
            stage.push(block, producerTime);
            producerNominal += producerPeriod;
            producerTime = producerNominal + jitter();
            if (producerTime > seconds - 10.0) {
                trimSum += stage.ratioTrim();
                trimMin = std::min(trimMin, stage.ratioTrim());
                trimMax = std::max(trimMax, stage.ratioTrim());
                ++samples;
            }
        } else {
            stage.pull(out.data(), deviceBlock, 1, deviceTime);
            if (deviceTime > seconds - 10.0) {
                fillSum += static_cast<double>(stage.fill()) / deviceRate * 1000.0;
            }
            deviceTime += devicePeriod;
            if (deviceTime > seconds - 1.0) {
                result.lastSecond.insert(result.lastSecond.end(), out.begin(), out.end());
            }
        }
    }
    result.underruns = stage.underruns();
    result.overruns = stage.overruns();
    result.meanTrimPpm = (trimSum / static_cast<double>(samples) - 1.0) * 1e6;
    result.trimSpreadPpm = (trimMax - trimMin) * 1e6;
    result.meanFillMs = fillSum / (10.0 / devicePeriod);
    return result;
}

double toneToNoiseDb(std::span<const float> x, double hz, double rate) {
    const auto n = static_cast<std::size_t>(rate);
    const auto last = x.subspan(x.size() - n);
    std::vector<double> w(n);
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n);
        const double window = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t) -
                              0.01168 * std::cos(3.0 * t);
        w[i] = static_cast<double>(last[i]) * window;
        total += w[i] * w[i];
    }
    double band = 0.0;
    for (int k = static_cast<int>(hz) - 10; k <= static_cast<int>(hz) + 10; ++k) {
        double re = 0.0;
        double im = 0.0;
        const double step = 2.0 * std::numbers::pi * k / static_cast<double>(n);
        for (std::size_t i = 0; i < n; ++i) {
            re += w[i] * std::cos(step * static_cast<double>(i));
            im -= w[i] * std::sin(step * static_cast<double>(i));
        }
        band += 2.0 * (re * re + im * im) / static_cast<double>(n);
    }
    return 10.0 * std::log10(band / std::max(total - band, 1e-30));
}

} // namespace vox::engine::test
