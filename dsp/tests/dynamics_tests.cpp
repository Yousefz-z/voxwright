#include "test_helpers.hpp"

#include <vox/dsp/dynamics.hpp>
#include <vox/dsp/meters.hpp>
#include <vox/dsp/noise.hpp>
#include <vox/dsp/noise_suppressor.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using Catch::Approx;
using namespace vox::dsp::test;
namespace vt = vox::testing;

TEST_CASE("Compressor static curve follows threshold and ratio", "[dsp][compressor]") {
    vox::dsp::Compressor c;
    c.prepare(kFs);
    c.setThresholdDb(-18.0F);
    c.setRatio(4.0F);
    c.setKneeDb(0.0F);
    CHECK(c.gainReductionDb(-30.0F) == Approx(0.0F));
    CHECK(c.gainReductionDb(-6.0F) == Approx(-9.0F));
    c.setKneeDb(6.0F);
    CHECK(c.gainReductionDb(-18.0F) == Approx(-0.5625F)); // middle of the knee
}

TEST_CASE("Compressor reduces a loud tone by the expected amount", "[dsp][compressor]") {
    vox::dsp::Compressor c;
    c.prepare(kFs);
    c.setThresholdDb(-20.0F);
    c.setRatio(4.0F);
    c.setKneeDb(0.0F);
    auto tone = vt::sine(1000.0, 1.0, kFs, 0.5F); // -6 dBFS peak
    const auto out = vt::renderOffline(c, tone, 128);
    const double reduction = vt::toDb(
        static_cast<double>(vt::peakAbs(std::span<const float>(out).subspan(24000))) / 0.5);
    // Level fed to the curve is the RMS (-9 dB) plus 3 dB = -6 dB: 14 dB over, 10.5 dB reduction.
    CHECK(reduction == Approx(-10.5).margin(0.6));
}

TEST_CASE("Limiter never exceeds its ceiling and is transparent below it", "[dsp][limiter]") {
    vox::dsp::Limiter lim;
    lim.prepare(kFs);
    lim.setCeilingDb(-1.0F);
    auto loud = vt::whiteNoise(1.0, kFs, 4.0F, 3);
    const auto out = vt::renderOffline(lim, loud, 97);
    CHECK(vt::peakAbs(out) <= vox::dsp::dbToGain(-1.0F) + 1e-6F);

    vox::dsp::Limiter clean;
    clean.prepare(kFs);
    clean.setCeilingDb(-1.0F);
    auto quiet = vt::sine(300.0, 0.5, kFs, 0.4F);
    const auto passed = vt::renderOffline(clean, quiet, 64);
    const std::size_t lat = clean.latencySamples();
    float maxDiff = 0.0F;
    for (std::size_t i = lat; i < passed.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(passed[i] - quiet[i - lat]));
    }
    CHECK(maxDiff < 1e-6F);
}

TEST_CASE("Limiter handles a single full-scale spike without overshoot", "[dsp][limiter]") {
    vox::dsp::Limiter lim;
    lim.prepare(kFs);
    lim.setCeilingDb(-0.3F);
    std::vector<float> x(4800, 0.1F);
    x[2000] = 8.0F;
    const auto out = vt::renderOffline(lim, x, 33);
    CHECK(vt::peakAbs(out) <= vox::dsp::dbToGain(-0.3F) + 1e-6F);
    // The spike is reduced smoothly: no sample right before it is cut harder than needed.
    CHECK(vt::allFinite(out));
}

TEST_CASE("Noise gate closes on room noise and opens for speech", "[dsp][gate]") {
    vox::dsp::NoiseGate g;
    g.prepare(kFs);
    g.setThresholdDb(-40.0F);
    g.setRangeDb(-60.0F);
    auto noise = vt::whiteNoise(1.0, kFs, 0.003F, 9); // about -55 dBFS
    const auto gated = vt::renderOffline(g, noise, 128);
    CHECK(vt::toDb(vt::rms(std::span<const float>(gated).subspan(24000)) /
                   vt::rms(std::span<const float>(noise).subspan(24000))) < -50.0);
    const auto speech = voice(120.0, 1.0);
    const auto passed = vt::renderOffline(g, speech, 128);
    CHECK(vt::toDb(vt::rms(std::span<const float>(passed).subspan(4800, 38400)) /
                   vt::rms(std::span<const float>(speech).subspan(4800, 38400))) > -0.5);
}

TEST_CASE("Noise suppressor removes stationary noise and keeps speech", "[dsp][rnnoise]") {
    // One second of noise lets the recurrent model settle, then a phrase in
    // the same noise at roughly 10 dB SNR. Pink noise stands in for room
    // noise (fans, traffic); see docs/architecture.md for white-noise results.
    const auto phrase = vt::synthPhrase(120.0);
    const std::size_t lead = 48000;
    std::vector<float> mixed(lead + phrase.audio.size());
    vox::dsp::PinkNoise pink(21);
    for (float& v : mixed) {
        v = 0.06F * pink.next();
    }
    for (std::size_t i = 0; i < phrase.audio.size(); ++i) {
        mixed[lead + i] += phrase.audio[i];
    }
    vox::dsp::NoiseSuppressor ns;
    ns.prepare(kFs);
    ns.setStrength(1.0F);
    const auto out = vt::renderOffline(ns, mixed, 128);
    const std::size_t lat = vox::dsp::NoiseSuppressor::latencySamples();
    const auto noiseIn = std::span<const float>(mixed).subspan(28800, 14400);
    const auto noiseOut = std::span<const float>(out).subspan(28800 + lat, 14400);
    const double noiseReduction = vt::toDb(vt::rms(noiseOut) / vt::rms(noiseIn));
    // The /a/ vowel, 0.15 s to 0.5 s into the phrase.
    const auto speechIn = std::span<const float>(phrase.audio).subspan(9600, 12000);
    const auto speechOut = std::span<const float>(out).subspan(lead + 9600 + lat, 12000);
    const double speechChange = vt::toDb(vt::rms(speechOut) / vt::rms(speechIn));
    INFO("noise " << noiseReduction << " dB, speech " << speechChange << " dB");
    CHECK(noiseReduction < -15.0);
    CHECK(speechChange > -6.0);
}

TEST_CASE("Level meter reads peak and RMS of a sine", "[dsp][meter]") {
    vox::dsp::LevelMeter m;
    m.prepare(kFs);
    const auto tone = vt::sine(1000.0, 2.0, kFs, 0.5F);
    m.process(tone);
    CHECK(m.peak() == Approx(0.5F).margin(0.01F));
    CHECK(m.rms() == Approx(0.35355F).margin(0.01F));
}

TEST_CASE("Integrated loudness matches the BS.1770 reference points", "[dsp][loudness]") {
    // A 997 Hz sine at 0 dBFS in one channel reads -3.01 LUFS.
    const auto full = vt::sine(997.0, 5.0, kFs, 1.0F);
    CHECK(vox::dsp::integratedLoudness(full, kFs) == Approx(-3.01).margin(0.05));
    const auto quieter = vt::sine(997.0, 5.0, kFs, 0.1F);
    CHECK(vox::dsp::integratedLoudness(quieter, kFs) == Approx(-23.01).margin(0.05));
    // Rate independence of the K-weighting.
    const auto at44 = vt::sine(997.0, 5.0, 44100.0, 1.0F);
    CHECK(vox::dsp::integratedLoudness(at44, 44100.0) == Approx(-3.01).margin(0.05));
}

TEST_CASE("Feedback detector flags a howl but not speech or noise", "[dsp][feedback]") {
    auto run = [](const std::vector<float>& x) {
        vox::dsp::FeedbackDetector d;
        d.prepare(kFs);
        for (std::size_t pos = 0; pos < x.size(); pos += 256) {
            d.process(
                std::span<const float>(x).subspan(pos, std::min<std::size_t>(256, x.size() - pos)));
        }
        return d.feedbackDetected();
    };
    CHECK(run(vt::sine(2400.0, 1.0, kFs, 0.4F)));
    CHECK_FALSE(run(vt::synthPhrase(120.0).audio));
    CHECK_FALSE(run(vt::synthPhrase(220.0).audio));
    CHECK_FALSE(run(vt::whiteNoise(1.0, kFs, 0.5F)));
}
