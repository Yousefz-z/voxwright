#include "drift_simulation.hpp"
#include "engine_test_helpers.hpp"

#include <vox/engine/output_stage.hpp>
#include <vox/testing/alloc_trap.hpp>
#include <vox/testing/analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace vox::engine;
using namespace vox::engine::test;

TEST_CASE("Output stage absorbs clock drift without dropouts", "[engine][output][drift]") {
    // Clocks within +-300 ppm cover real sound cards and USB devices
    // (typically under 100 ppm apart). Producer callbacks arrive up to
    // 1.5 ms late at random.
    struct Case {
        double deviceRate;
        std::size_t producerBlock;
        std::size_t deviceBlock;
    };
    for (const Case c : {Case{48000.0, 128, 128}, Case{44100.0, 128, 441}, Case{48000.0, 480, 128},
                         Case{96000.0, 256, 512}}) {
        for (const double ppm : {-300.0, 0.0, 300.0}) {
            const auto r = simulateDrift(c.deviceRate, ppm, 90.0, c.producerBlock, c.deviceBlock);
            const double snr = toneToNoiseDb(r.lastSecond, 1000.0, c.deviceRate);
            INFO("device " << c.deviceRate << " Hz, blocks " << c.producerBlock << "/"
                           << c.deviceBlock << ", " << ppm << " ppm: underruns " << r.underruns
                           << ", overruns " << r.overruns << ", trim " << r.meanTrimPpm
                           << " ppm (spread " << r.trimSpreadPpm << "), mean fill " << r.meanFillMs
                           << " ms, SNR " << snr << " dB");
            CHECK(r.underruns == 0);
            CHECK(r.overruns == 0);
            // The trim cancels the drift: a fast device needs more samples.
            CHECK(std::abs(r.meanTrimPpm - ppm) < 10.0);
            // Slow, small ratio movements only (under 0.1 cent of pitch).
            CHECK(r.trimSpreadPpm < 60.0);
            CHECK(snr > 90.0);
        }
    }
}

TEST_CASE("Output stage refills after an underrun instead of crackling", "[engine][output]") {
    OutputStage stage;
    stage.prepare(kEngineRate, kEngineRate, 256, 128);
    std::vector<float> block(128, 0.5F);
    std::vector<float> out(128);
    // Nothing pushed yet: silence while it fills.
    stage.pull(out.data(), 128, 1, 0.0);
    CHECK(vox::testing::peakAbs(out) == 0.0F);
    // It starts once it holds target + one read + the last producer burst
    // (256 + 128 + 128), so the next reads cannot run dry.
    for (int i = 0; i < 4; ++i) {
        stage.push(block, static_cast<double>(i));
    }
    stage.pull(out.data(), 128, 1, 0.0);
    CHECK(vox::testing::peakAbs(out) == 0.0F); // 4 x 128 minus the resampler's hold-back
    stage.push(block, 5.0);
    stage.pull(out.data(), 128, 1, 0.0);
    CHECK(vox::testing::peakAbs(out) > 0.4F);
    // Starve it.
    for (int i = 0; i < 4; ++i) {
        stage.pull(out.data(), 128, 1, 0.0);
    }
    CHECK(stage.underruns() == 1);
    // One block is not enough to resume: it waits for the full amount again.
    stage.push(block, 6.0);
    stage.pull(out.data(), 128, 1, 0.0);
    CHECK(vox::testing::peakAbs(out) == 0.0F);
    for (int i = 0; i < 4; ++i) {
        stage.push(block, 7.0 + i);
    }
    stage.pull(out.data(), 128, 1, 0.0);
    CHECK(vox::testing::peakAbs(out) > 0.4F);
}

TEST_CASE("Output stage counts overruns when the device stops reading", "[engine][output]") {
    OutputStage stage;
    stage.prepare(kEngineRate, kEngineRate, 256, 128);
    const std::vector<float> block(128, 0.1F);
    for (int i = 0; i < 1000; ++i) {
        stage.push(block, 0.0);
    }
    CHECK(stage.overruns() > 0);
}

TEST_CASE("Output stage writes every channel", "[engine][output]") {
    OutputStage stage;
    stage.prepare(kEngineRate, kEngineRate, 64, 128);
    std::vector<float> block(128);
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = static_cast<float>(i) * 0.001F;
    }
    stage.push(block, 0.0);
    std::vector<float> out(std::size_t{64} * 2);
    stage.pull(out.data(), 64, 2, 0.0);
    for (std::size_t i = 0; i < 64; ++i) {
        CHECK(out[2 * i] == out[2 * i + 1]);
    }
}

TEST_CASE("Output stage does not allocate", "[engine][output][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    OutputStage stage;
    stage.prepare(kEngineRate, 44100.0, 300, 512);
    const std::vector<float> block(512, 0.1F);
    std::vector<float> out(std::size_t{441} * 2);
    std::size_t allocations = 0;
    {
        const vox::testing::AllocationTrap trap;
        for (int i = 0; i < 100; ++i) {
            stage.push(block, static_cast<double>(i) * 0.01);
            stage.pull(out.data(), 441, 2, static_cast<double>(i) * 0.01 + 0.005);
        }
        allocations = trap.allocations();
    }
    CHECK(allocations == 0);
}
