#include <vox/dsp/psola.hpp>
#include <vox/dsp/spectral_shifter.hpp>
#include <vox/testing/alloc_trap.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/render.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <span>
#include <vector>

namespace {

constexpr double kFs = 48000.0;

std::vector<float> voiceAt(double f0, double seconds = 1.6,
                           const vox::testing::Vowel& vowel = vox::testing::vowelA()) {
    vox::testing::VoiceSpec spec;
    spec.f0Hz = f0;
    spec.seconds = seconds;
    spec.vowel = vowel;
    spec.peak = 0.5F;
    return vox::testing::synthVoice(spec);
}

/// Steady-state part of an output: skips latency plus settling time.
std::span<const float> steady(const std::vector<float>& out, std::size_t latency) {
    const std::size_t start = latency + static_cast<std::size_t>(0.3 * kFs);
    const std::size_t end = out.size() - static_cast<std::size_t>(0.1 * kFs);
    return std::span<const float>(out).subspan(start, end - start);
}

template <class Shifter>
std::vector<float> run(Shifter& shifter, const std::vector<float>& in, std::size_t block = 128) {
    return vox::testing::renderOffline(shifter, in, block);
}

vox::dsp::PsolaShifter makePsola() {
    vox::dsp::PsolaShifter s;
    s.prepare({kFs, 75.0F, 800.0F, 2048});
    return s;
}

vox::dsp::SpectralShifter
makeSpectral(vox::dsp::SpectralShifter::Quality q = vox::dsp::SpectralShifter::Quality::Balanced) {
    vox::dsp::SpectralShifter s;
    s.prepare({kFs, 2048, q});
    return s;
}

/// First two formant peaks of the LPC envelope (F1, F2).
std::vector<double> firstFormants(std::span<const float> x) {
    const auto env = vox::testing::lpcEnvelopeDb(x, kFs, 44, 2048);
    auto peaks = vox::testing::envelopePeaksHz(env, kFs, 6);
    std::erase_if(peaks, [](double hz) { return hz < 200.0; });
    peaks.resize(std::min<std::size_t>(peaks.size(), 2));
    return peaks;
}

} // namespace

TEST_CASE("PSOLA shifts pitch to within 5 cents", "[dsp][psola][pitch]") {
    const double f0 = GENERATE(100.0, 180.0, 260.0);
    const double semitones = GENERATE(-12.0, -7.0, -3.0, 4.0, 7.0, 12.0);
    auto shifter = makePsola();
    shifter.setPitchSemitones(static_cast<float>(semitones));
    const auto out = run(shifter, voiceAt(f0));
    const double expected = f0 * std::exp2(semitones / 12.0);
    const auto est =
        vox::testing::estimateF0(steady(out, shifter.latencySamples()), kFs, 40.0, 1100.0);
    const double error = vox::testing::centsBetween(est.hz, expected);
    INFO("f0 " << f0 << " shift " << semitones << " -> " << est.hz << " Hz (" << error
               << " cents)");
    CHECK(std::abs(error) < 5.0);
    CHECK(shifter.voice().lateSamples() == 0);
    CHECK(vox::testing::allFinite(out));
}

TEST_CASE("PSOLA keeps formants in place when shifting pitch", "[dsp][psola][formant]") {
    const double semitones = GENERATE(-5.0, 5.0);
    auto shifter = makePsola();
    shifter.setPitchSemitones(static_cast<float>(semitones));
    const auto in = voiceAt(120.0);
    const auto out = run(shifter, in);
    const auto before = firstFormants(steady(in, 0));
    const auto after = firstFormants(steady(out, shifter.latencySamples()));
    REQUIRE(before.size() == 2);
    REQUIRE(after.size() == 2);
    INFO("F1 " << before[0] << " -> " << after[0] << ", F2 " << before[1] << " -> " << after[1]);
    CHECK(std::abs(after[0] / before[0] - 1.0) < 0.10);
    CHECK(std::abs(after[1] / before[1] - 1.0) < 0.08);
}

TEST_CASE("PSOLA formant control moves formants without changing pitch", "[dsp][psola][formant]") {
    auto shifter = makePsola();
    shifter.setFormantSemitones(3.0F);
    const auto in = voiceAt(110.0);
    const auto out = run(shifter, in);
    const auto o = steady(out, shifter.latencySamples());
    const auto est = vox::testing::estimateF0(o, kFs, 40.0, 1100.0);
    CHECK(std::abs(vox::testing::centsBetween(est.hz, 110.0)) < 5.0);
    const auto before = firstFormants(steady(in, 0));
    const auto after = firstFormants(o);
    REQUIRE(before.size() == 2);
    REQUIRE(after.size() == 2);
    const double expected = std::exp2(3.0 / 12.0);
    INFO("F1 " << before[0] << " -> " << after[0] << ", F2 " << before[1] << " -> " << after[1]);
    CHECK(std::abs(after[1] / before[1] / expected - 1.0) < 0.08);
}

TEST_CASE("PSOLA latency matches the reported value", "[dsp][psola][latency]") {
    auto shifter = makePsola();
    // Aperiodic input so the cross-correlation has a single peak.
    const auto in = vox::testing::whiteNoise(1.0, kFs, 0.3F, 11);
    const auto out = run(shifter, in);
    const auto lag =
        vox::testing::estimateLag(std::span<const float>(in).subspan(0, 30000),
                                  std::span<const float>(out).subspan(0, 30000 + 4000), 3000);
    INFO("measured " << lag << ", reported " << shifter.latencySamples());
    CHECK(std::abs(static_cast<long>(lag) - static_cast<long>(shifter.latencySamples())) <= 2);
}

TEST_CASE("PSOLA keeps the output level close to the input level", "[dsp][psola][level]") {
    const double semitones = GENERATE(-12.0, -5.0, 0.0, 5.0, 12.0);
    auto shifter = makePsola();
    shifter.setPitchSemitones(static_cast<float>(semitones));
    const auto in = voiceAt(130.0);
    const auto out = run(shifter, in);
    const double inDb = vox::testing::toDb(vox::testing::rms(steady(in, 0)));
    const double outDb =
        vox::testing::toDb(vox::testing::rms(steady(out, shifter.latencySamples())));
    INFO("shift " << semitones << ": " << inDb << " dB -> " << outDb << " dB");
    CHECK(std::abs(outDb - inDb) < 3.0);
}

TEST_CASE("PSOLA works with irregular callback sizes", "[dsp][psola]") {
    auto shifter = makePsola();
    shifter.setPitchSemitones(-4.0F);
    const auto in = voiceAt(150.0);
    const auto out = vox::testing::renderOfflineIrregular(shifter, in, 700);
    const auto est =
        vox::testing::estimateF0(steady(out, shifter.latencySamples()), kFs, 40.0, 1100.0);
    CHECK(std::abs(vox::testing::centsBetween(est.hz, 150.0 * std::exp2(-4.0 / 12.0))) < 5.0);
    CHECK(shifter.voice().lateSamples() == 0);
}

TEST_CASE("PSOLA fixed mode turns a glide into a monotone", "[dsp][psola][robot]") {
    auto shifter = makePsola();
    shifter.setPitchMode(vox::dsp::PitchMode::Fixed);
    shifter.setFixedFrequency(110.0F);
    vox::testing::VoiceSpec spec;
    spec.f0Hz = 100.0;
    spec.f0EndHz = 160.0;
    spec.seconds = 1.6;
    const auto out = run(shifter, vox::testing::synthVoice(spec));
    const auto track =
        vox::testing::f0Track(steady(out, shifter.latencySamples()), kFs, 50.0, 400.0);
    std::size_t within = 0;
    std::size_t voiced = 0;
    for (const double f : track) {
        if (f > 0.0) {
            ++voiced;
            within += std::abs(vox::testing::centsBetween(f, 110.0)) < 10.0 ? 1U : 0U;
        }
    }
    REQUIRE(voiced > 50);
    CHECK(static_cast<double>(within) / static_cast<double>(voiced) > 0.95);
}

TEST_CASE("PSOLA quantize mode snaps to the scale", "[dsp][psola][tune]") {
    auto shifter = makePsola();
    shifter.setPitchMode(vox::dsp::PitchMode::Quantize);
    constexpr std::uint16_t kMajor = 0b101010110101; // C D E F G A B
    shifter.setScale(0, kMajor, 0.0F);
    vox::testing::VoiceSpec spec;
    spec.f0Hz = 196.0;    // G3
    spec.f0EndHz = 246.9; // B3, glides through G#, A, A#
    spec.seconds = 2.0;
    const auto out = run(shifter, vox::testing::synthVoice(spec));
    const auto track =
        vox::testing::f0Track(steady(out, shifter.latencySamples()), kFs, 80.0, 500.0);
    std::size_t onScale = 0;
    std::size_t voiced = 0;
    for (const double f : track) {
        if (f <= 0.0) {
            continue;
        }
        ++voiced;
        const double note = 69.0 + 12.0 * std::log2(f / 440.0);
        const double nearest = std::round(note);
        const int pc = (static_cast<int>(nearest) % 12 + 12) % 12;
        const bool inScale = ((kMajor >> pc) & 1) != 0;
        onScale += (inScale && std::abs(note - nearest) < 0.15) ? 1U : 0U;
    }
    REQUIRE(voiced > 50);
    CHECK(static_cast<double>(onScale) / static_cast<double>(voiced) > 0.9);
}

TEST_CASE("PSOLA stays finite and silent-in silent-out on edge cases", "[dsp][psola]") {
    auto shifter = makePsola();
    shifter.setPitchSemitones(7.0F);
    shifter.setFormantSemitones(-4.0F);
    const auto quiet = run(shifter, vox::testing::silence(0.5));
    CHECK(vox::testing::peakAbs(quiet) == 0.0F);
    const auto noise = run(shifter, vox::testing::whiteNoise(1.0, kFs, 0.9F));
    CHECK(vox::testing::allFinite(noise));
    CHECK(vox::testing::peakAbs(noise) < 4.0F);
    std::vector<float> square(48000);
    for (std::size_t i = 0; i < square.size(); ++i) {
        square[i] = (i / 200) % 2 == 0 ? 1.0F : -1.0F;
    }
    const auto loud = run(shifter, square);
    CHECK(vox::testing::allFinite(loud));
}

TEST_CASE("PSOLA processing does not allocate", "[dsp][psola][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    auto shifter = makePsola();
    shifter.setPitchSemitones(5.0F);
    auto in = voiceAt(120.0, 0.5);
    const vox::testing::AllocationTrap trap;
    for (std::size_t pos = 0; pos + 256 <= in.size(); pos += 256) {
        shifter.process(std::span<float>(in).subspan(pos, 256));
    }
    CHECK(trap.allocations() == 0);
    CHECK(trap.deallocations() == 0);
}

// The spectral engine needs analysis blocks several pitch periods long; at
// its library-default 120 ms block it must transpose accurately (see
// docs/architecture.md for the shorter-block and formant measurements).
TEST_CASE("Spectral shifter transposes to within 5 cents", "[dsp][spectral][pitch]") {
    const double f0 = GENERATE(100.0, 180.0, 260.0);
    const double semitones = GENERATE(-12.0, -5.0, 7.0, 12.0);
    auto shifter = makeSpectral(vox::dsp::SpectralShifter::Quality::Studio);
    shifter.setPitchSemitones(static_cast<float>(semitones));
    const auto out = run(shifter, voiceAt(f0));
    const double expected = f0 * std::exp2(semitones / 12.0);
    const auto est =
        vox::testing::estimateF0(steady(out, shifter.latencySamples()), kFs, 40.0, 1100.0);
    const double error = vox::testing::centsBetween(est.hz, expected);
    INFO("f0 " << f0 << " shift " << semitones << " -> " << est.hz << " Hz (" << error
               << " cents)");
    CHECK(std::abs(error) < 5.0);
}

TEST_CASE("Spectral shifter latency matches the reported value", "[dsp][spectral][latency]") {
    auto shifter = makeSpectral(vox::dsp::SpectralShifter::Quality::LowLatency);
    const auto in = vox::testing::whiteNoise(1.0, kFs, 0.3F, 11);
    const auto out = run(shifter, in);
    const auto lag =
        vox::testing::estimateLag(std::span<const float>(in).subspan(0, 30000),
                                  std::span<const float>(out).subspan(0, 30000 + 4000), 3000);
    INFO("measured " << lag << ", reported " << shifter.latencySamples());
    CHECK(std::abs(static_cast<long>(lag) - static_cast<long>(shifter.latencySamples())) <= 8);
}

TEST_CASE("Spectral shifter processing does not allocate", "[dsp][spectral][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    auto shifter = makeSpectral();
    shifter.setPitchSemitones(-3.0F);
    auto in = voiceAt(120.0, 0.5);
    const vox::testing::AllocationTrap trap;
    for (std::size_t pos = 0; pos + 256 <= in.size(); pos += 256) {
        shifter.process(std::span<float>(in).subspan(pos, 256));
    }
    CHECK(trap.allocations() == 0);
    CHECK(trap.deallocations() == 0);
}
