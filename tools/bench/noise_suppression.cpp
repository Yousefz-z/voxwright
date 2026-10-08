// Measures RNNoise suppression on stationary white, pink, and brown noise at
// three levels after one second of adaptation. Prints a Markdown table used
// in docs/architecture.md.

#include <vox/dsp/noise.hpp>
#include <vox/dsp/noise_suppressor.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/render.hpp>

#include <array>
#include <cstdio>
#include <span>
#include <vector>

int main() {
    constexpr std::size_t kLength = 96000; // 2 s at 48 kHz
    constexpr std::array<const char*, 3> kNames{"White", "Pink", "Brown"};
    std::printf("| Noise | Input level (dBFS) | Reduction after 1 s (dB) |\n|---|---|---|\n");
    for (std::size_t kind = 0; kind < kNames.size(); ++kind) {
        for (const float amplitude : {0.002F, 0.02F, 0.1F}) {
            std::vector<float> noise(kLength);
            vox::dsp::FastRandom white(5);
            vox::dsp::PinkNoise pink(7);
            vox::dsp::BrownNoise brown(9);
            for (float& v : noise) {
                float n = brown.next();
                if (kind == 0) {
                    n = white.nextBipolar();
                } else if (kind == 1) {
                    n = pink.next() * 3.0F;
                }
                v = amplitude * n;
            }
            vox::dsp::NoiseSuppressor suppressor;
            suppressor.prepare(48000.0);
            suppressor.setStrength(1.0F);
            const auto out = vox::testing::renderOffline(suppressor, noise, 480);
            const auto in = std::span<const float>(noise).subspan(48000, 40000);
            const auto processed = std::span<const float>(out).subspan(
                48000 + vox::dsp::NoiseSuppressor::latencySamples(), 40000);
            std::printf("| %s | %.1f | %.1f |\n", kNames[kind],
                        vox::testing::toDb(vox::testing::rms(in)),
                        vox::testing::toDb(vox::testing::rms(processed) / vox::testing::rms(in)));
        }
    }
    return 0;
}
