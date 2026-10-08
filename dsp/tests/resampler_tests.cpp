#include "test_helpers.hpp"

#include <vox/dsp/resampler.hpp>
#include <vox/testing/alloc_trap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

/// Signal-to-residual ratio (dB) after a least-squares fit of a sine at
/// `hz` (amplitude, phase, and DC) to x.
double sineFitSnrDb(std::span<const float> x, double hz, double rate) {
    double ss = 0.0;
    double cc = 0.0;
    double sc = 0.0;
    double xs = 0.0;
    double xc = 0.0;
    const double w = 2.0 * std::numbers::pi * hz / rate;
    double mean = 0.0;
    for (const float v : x) {
        mean += static_cast<double>(v);
    }
    mean /= static_cast<double>(x.size());
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double s = std::sin(w * static_cast<double>(n));
        const double c = std::cos(w * static_cast<double>(n));
        const double v = static_cast<double>(x[n]) - mean;
        ss += s * s;
        cc += c * c;
        sc += s * c;
        xs += v * s;
        xc += v * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (xs * cc - xc * sc) / det;
    const double b = (xc * ss - xs * sc) / det;
    double signal = 0.0;
    double residual = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double fit =
            a * std::sin(w * static_cast<double>(n)) + b * std::cos(w * static_cast<double>(n));
        const double e = static_cast<double>(x[n]) - mean - fit;
        signal += fit * fit;
        residual += e * e;
    }
    return 10.0 * std::log10(signal / std::max(residual, 1e-30));
}

} // namespace

TEST_CASE("One-shot resampling keeps a sine clean and at its frequency", "[dsp][resampler]") {
    for (const double from : {44100.0, 32000.0, 96000.0}) {
        const auto in = vox::testing::sine(1000.0, 1.0, from, 0.5);
        const auto out = vox::dsp::resample(in, from, 48000.0);
        INFO("from " << from << " Hz");
        // Length scales with the ratio (within one filter length).
        CHECK(std::abs(static_cast<double>(out.size()) - 48000.0) < 64.0);
        const auto middle = std::span<const float>(out).subspan(4800, 38400);
        const double snr = sineFitSnrDb(middle, 1000.0, 48000.0);
        INFO("SNR " << snr << " dB");
        CHECK(snr > 100.0);
    }
}

TEST_CASE("Streaming resampler matches the nominal ratio and quality", "[dsp][resampler]") {
    const auto in = vox::testing::sine(997.0, 2.0, 44100.0, 0.5);
    for (const auto quality :
         {vox::dsp::ResamplerQuality::Fast, vox::dsp::ResamplerQuality::Medium}) {
        vox::dsp::StreamResampler r;
        r.prepare(quality, 48000.0 / 44100.0);
        std::vector<float> out;
        std::vector<float> buffer(700);
        // Irregular block sizes, like device callbacks.
        const std::size_t sizes[] = {441, 96, 512, 1, 333, 128};
        std::size_t pos = 0;
        for (std::size_t i = 0; pos < in.size(); ++i) {
            const std::size_t n = std::min(sizes[i % 6], in.size() - pos);
            std::size_t used = 0;
            while (used < n) {
                const auto counts =
                    r.process(std::span<const float>(in).subspan(pos + used, n - used), buffer);
                out.insert(out.end(), buffer.begin(),
                           buffer.begin() + static_cast<std::ptrdiff_t>(counts.produced));
                used += counts.consumed;
                if (counts.consumed == 0 && counts.produced == 0) {
                    break;
                }
            }
            pos += n;
        }
        const double expected = static_cast<double>(in.size()) * 48000.0 / 44100.0;
        INFO("quality " << static_cast<int>(quality) << ", produced " << out.size());
        // Streaming holds back about half a filter length.
        CHECK(static_cast<double>(out.size()) > expected - 400.0);
        CHECK(static_cast<double>(out.size()) <= expected + 1.0);
        const double snr =
            sineFitSnrDb(std::span<const float>(out).subspan(9600, 48000), 997.0, 48000.0);
        INFO("SNR " << snr << " dB");
        CHECK(snr > (quality == vox::dsp::ResamplerQuality::Fast ? 80.0 : 100.0));
    }
}

TEST_CASE("Streaming resampler output is aligned and holds back little input", "[dsp][resampler]") {
    // Equal rates, so input and output can be compared sample by sample.
    // libsamplerate compensates its filter delay (the output is aligned
    // with the input) by holding back the newest input until the filter
    // has seen enough after it. That hold-back is the streaming latency.
    const auto noise = vox::testing::whiteNoise(0.5, 48000.0, 0.3F, 5);
    for (const auto quality :
         {vox::dsp::ResamplerQuality::Fast, vox::dsp::ResamplerQuality::Medium}) {
        vox::dsp::StreamResampler r;
        r.prepare(quality, 1.0);
        std::vector<float> out(noise.size() + 1024, 0.0F);
        const auto counts = r.process(noise, out);
        out.resize(counts.produced);
        const std::size_t lag = vox::testing::estimateLag(noise, out, 2000);
        const std::size_t heldBack = noise.size() - counts.produced;
        INFO("quality " << static_cast<int>(quality) << ": lag " << lag << ", held back "
                        << heldBack << " samples");
        CHECK(lag == 0);
        CHECK(heldBack < 64); // under 1.4 ms
    }
}

TEST_CASE("Ratio changes for drift compensation are click-free", "[dsp][resampler]") {
    const auto in = vox::testing::sine(440.0, 2.0, 48000.0, 0.5);
    vox::dsp::StreamResampler r;
    r.prepare(vox::dsp::ResamplerQuality::Fast, 1.0);
    std::vector<float> out;
    std::vector<float> buffer(256);
    for (std::size_t pos = 0; pos + 128 <= in.size(); pos += 128) {
        // Trim swings by +-0.5 % every block: far harsher than real drift.
        r.setRatio(1.0 + ((pos / 128) % 2 == 0 ? 0.005 : -0.005));
        const auto counts = r.process(std::span<const float>(in).subspan(pos, 128), buffer);
        out.insert(out.end(), buffer.begin(),
                   buffer.begin() + static_cast<std::ptrdiff_t>(counts.produced));
    }
    // A click would show as a jump much larger than the sine's own slope.
    float maxStep = 0.0F;
    for (std::size_t i = 4800; i + 1 < out.size(); ++i) {
        maxStep = std::max(maxStep, std::abs(out[i + 1] - out[i]));
    }
    const auto slope = static_cast<float>(0.5 * 2.0 * std::numbers::pi * 440.0 / 48000.0);
    INFO("max step " << maxStep << ", sine slope " << slope);
    CHECK(maxStep < slope * 1.05F);
}

TEST_CASE("Streaming resampler does not allocate while processing", "[dsp][resampler][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    const auto in = vox::testing::sine(440.0, 0.5, 44100.0, 0.5);
    vox::dsp::StreamResampler r;
    r.prepare(vox::dsp::ResamplerQuality::Medium, 48000.0 / 44100.0);
    std::vector<float> out(1024);
    std::size_t allocations = 0;
    {
        const vox::testing::AllocationTrap trap;
        for (std::size_t pos = 0; pos + 441 <= in.size(); pos += 441) {
            static_cast<void>(r.process(std::span<const float>(in).subspan(pos, 441), out));
        }
        allocations = trap.allocations();
    }
    CHECK(allocations == 0);
}
