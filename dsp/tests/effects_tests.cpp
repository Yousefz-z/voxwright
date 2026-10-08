#include "test_helpers.hpp"

#include <vox/dsp/ambience.hpp>
#include <vox/dsp/distortion.hpp>
#include <vox/dsp/echo.hpp>
#include <vox/dsp/equalizer.hpp>
#include <vox/dsp/modulation.hpp>
#include <vox/dsp/multimode_filter.hpp>
#include <vox/dsp/resonator.hpp>
#include <vox/dsp/ring_modulator.hpp>
#include <vox/dsp/vocoder.hpp>
#include <vox/dsp/whisper.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using Catch::Approx;
using namespace vox::dsp::test;
namespace vt = vox::testing;

TEST_CASE("Equalizer output matches its analytic response", "[dsp][eq]") {
    vox::dsp::Equalizer eq;
    eq.prepare(kFs);
    eq.setFrequency(vox::dsp::Equalizer::Peak2, 1500.0F);
    eq.setGainDb(vox::dsp::Equalizer::Peak2, 6.0F);
    eq.setEnabled(vox::dsp::Equalizer::LowCut, true);
    eq.setFrequency(vox::dsp::Equalizer::LowCut, 100.0F);
    for (const double hz : {50.0, 400.0, 1500.0, 5000.0}) {
        vox::dsp::Equalizer e = eq;
        auto tone = vt::sine(hz, 1.0);
        auto out = vt::renderOffline(e, tone, 256);
        const double measured = vt::toDb(vt::rms(std::span<const float>(out).subspan(24000)) /
                                         vt::rms(std::span<const float>(tone).subspan(24000)));
        INFO(hz << " Hz: measured " << measured << " dB, expected " << e.magnitudeDb(hz));
        CHECK(measured == Approx(e.magnitudeDb(hz)).margin(0.15));
    }
}

TEST_CASE("Equalizer cuts are 24 dB per octave Butterworth", "[dsp][eq]") {
    vox::dsp::Equalizer eq;
    eq.prepare(kFs);
    eq.setEnabled(vox::dsp::Equalizer::HighCut, true);
    eq.setFrequency(vox::dsp::Equalizer::HighCut, 3000.0F);
    std::vector<float> settle(4800, 0.0F);
    eq.process(settle); // let the frequency glide finish
    CHECK(eq.magnitudeDb(3000.0) == Approx(-3.01).margin(0.05));
    // Bilinear-transform Butterworth: |H|^2 = 1 / (1 + W^8), W the warped ratio.
    const double w =
        std::tan(std::numbers::pi * 6000.0 / kFs) / std::tan(std::numbers::pi * 3000.0 / kFs);
    CHECK(eq.magnitudeDb(6000.0) ==
          Approx(-10.0 * std::log10(1.0 + std::pow(w, 8.0))).margin(0.05));
}

TEST_CASE("Multimode filter low-pass attenuates above cutoff", "[dsp][filter]") {
    vox::dsp::MultimodeFilter f;
    f.prepare(kFs);
    f.setMode(vox::dsp::MultimodeFilter::Mode::Lowpass);
    f.setCutoff(800.0F);
    f.setResonance(0.707F);
    auto high = vt::sine(6400.0, 0.5);
    const auto out = vt::renderOffline(f, high, 128);
    // 12 dB/octave, three octaves above cutoff: about -36 dB.
    CHECK(vt::toDb(vt::rms(std::span<const float>(out).subspan(4800)) / vt::rms(high)) < -30.0);
}

TEST_CASE("Distortion oversampling keeps aliases low", "[dsp][distortion]") {
    vox::dsp::Distortion d;
    d.prepare(kFs);
    d.setMode(vox::dsp::Distortion::Mode::Hard);
    d.setDriveDb(24.0F);
    d.setToneHz(20000.0F);
    d.setOutputDb(0.0F);
    auto tone = vt::sine(7000.0, 1.0, kFs, 0.5F);
    const auto out = vt::renderOffline(d, tone, 256);
    const auto x = std::span<const float>(out).subspan(4800);
    const double fundamental = levelAt(x, 7000.0);
    // Without oversampling the 5th and 7th harmonics fold to 13 kHz and 1 kHz.
    const double alias5 = levelAt(x, 13000.0);
    const double alias7 = levelAt(x, 1000.0);
    INFO("fundamental " << fundamental << " dB, alias@13k " << alias5 << ", alias@1k " << alias7);
    CHECK(fundamental - alias5 > 40.0);
    CHECK(fundamental - alias7 > 40.0);
}

TEST_CASE("Distortion crush mode adds quantization noise by bit depth", "[dsp][distortion]") {
    auto noiseFloor = [](float bits) {
        vox::dsp::Distortion d;
        d.prepare(kFs);
        d.setMode(vox::dsp::Distortion::Mode::Crush);
        d.setBits(bits);
        d.setDownsample(1.0F);
        d.setDriveDb(0.0F);
        d.setToneHz(20000.0F);
        d.setOutputDb(0.0F);
        auto tone = vt::sine(997.0, 1.0, kFs, 0.9F);
        const auto out = vt::renderOffline(d, tone, 64);
        const auto x = std::span<const float>(out).subspan(4800);
        // Energy away from the fundamental relative to the fundamental.
        const auto db = vt::powerSpectrumDb(x, 16384);
        const double binHz = kFs / 16384.0;
        double tonal = 0.0;
        double rest = 0.0;
        for (std::size_t k = 1; k < db.size(); ++k) {
            const double p = std::pow(10.0, db[k] / 10.0);
            (std::abs(static_cast<double>(k) * binHz - 997.0) < 20.0 ? tonal : rest) += p;
        }
        return 10.0 * std::log10(rest / tonal);
    };
    const double crushed = noiseFloor(3.0F);
    const double clean = noiseFloor(16.0F);
    INFO("3-bit " << crushed << " dB, 16-bit " << clean << " dB");
    // Theory: 6.02 dB per bit; 3 bits leaves about -20 dB. At 16 bits the
    // measurement is limited by the Hann window's leakage (about -60 dB).
    CHECK(crushed > -28.0);
    CHECK(crushed < -12.0);
    CHECK(clean < -55.0);
}

TEST_CASE("Ring modulator produces sum and difference frequencies", "[dsp][ringmod]") {
    vox::dsp::RingModulator rm;
    rm.prepare(kFs);
    rm.setFrequency(200.0F);
    rm.setMix(1.0F);
    auto tone = vt::sine(1000.0, 1.0);
    const auto out = vt::renderOffline(rm, tone, 128);
    const auto x = std::span<const float>(out).subspan(4800);
    CHECK(levelAt(x, 800.0) - levelAt(x, 1000.0) > 40.0);
    CHECK(levelAt(x, 1200.0) - levelAt(x, 1000.0) > 40.0);
}

TEST_CASE("Frequency shifter moves a tone by the requested hertz", "[dsp][freqshift]") {
    const float shift = GENERATE(150.0F, -230.0F);
    vox::dsp::FrequencyShifter fs;
    fs.prepare(kFs);
    fs.setShiftHz(shift);
    fs.setMix(1.0F);
    auto tone = vt::sine(1000.0, 1.0);
    const auto out = vt::renderOffline(fs, tone, 128);
    const auto x = std::span<const float>(out).subspan(4800);
    const double target = 1000.0 + static_cast<double>(shift);
    const double image = 1000.0 - static_cast<double>(shift);
    CHECK(strongestHz(x, 200.0, 4000.0) == Approx(target).margin(4.0));
    CHECK(levelAt(x, target) - levelAt(x, image) > 30.0);
}

TEST_CASE("Echo repeats at the set time and decays by the feedback", "[dsp][echo]") {
    vox::dsp::Echo echo;
    echo.prepare(kFs);
    echo.setTimeMs(250.0F);
    echo.setFeedback(0.5F);
    echo.setDampingHz(20000.0F);
    echo.setMix(1.0F);
    std::vector<float> impulse(48000, 0.0F);
    impulse[100] = 1.0F;
    const auto out = vt::renderOffline(echo, impulse, 128);
    const std::size_t d = 12000;
    auto peakNear = [&](std::size_t centre) {
        float m = 0.0F;
        for (std::size_t i = centre - 50; i < centre + 50; ++i) {
            m = std::max(m, std::abs(out[i]));
        }
        return m;
    };
    const float first = peakNear(100 + d);
    const float second = peakNear(100 + 2 * d);
    CHECK(first > 0.8F);
    CHECK(second / first == Approx(0.5F).margin(0.08F)); // high-pass in the loop costs a little
}

TEST_CASE("Reverb decay time follows the RT60 setting", "[dsp][reverb]") {
    const float rt60 = GENERATE(0.8F, 2.0F);
    vox::dsp::Reverb rv;
    rv.prepare(kFs);
    rv.setDecaySeconds(rt60);
    rv.setDampingHz(20000.0F);
    rv.setPredelayMs(0.0F);
    rv.setMix(1.0F);
    std::vector<float> impulse(
        static_cast<std::size_t>(kFs * (static_cast<double>(rt60) * 1.5 + 0.5)), 0.0F);
    impulse[0] = 1.0F;
    const auto out = vt::renderOffline(rv, impulse, 256);
    // Schroeder backward integration of the tail (skip the direct sound).
    std::vector<double> edc(out.size(), 0.0);
    double acc = 0.0;
    for (std::size_t i = out.size(); i-- > 480;) {
        acc += static_cast<double>(out[i]) * static_cast<double>(out[i]);
        edc[i] = acc;
    }
    const double start = edc[480];
    auto timeAt = [&](double db) {
        for (std::size_t i = 480; i < edc.size(); ++i) {
            if (10.0 * std::log10(edc[i] / start) <= db) {
                return static_cast<double>(i) / kFs;
            }
        }
        return static_cast<double>(edc.size()) / kFs;
    };
    const double t30 = 2.0 * (timeAt(-35.0) - timeAt(-5.0)); // T30 extrapolated to 60 dB
    INFO("RT60 set " << rt60 << " s, measured T30 " << t30 << " s");
    CHECK(t30 == Approx(rt60).epsilon(0.25));
}

TEST_CASE("Tremolo with a square LFO chops the signal", "[dsp][tremolo]") {
    vox::dsp::Tremolo t;
    t.prepare(kFs);
    t.setRate(8.0F);
    t.setDepth(1.0F);
    t.setShape(vox::dsp::LfoShape::Square);
    auto tone = vt::sine(440.0, 1.0);
    const auto out = vt::renderOffline(t, tone, 128);
    const auto levels = windowedRms(std::span<const float>(out).subspan(4800), 480);
    const auto [mn, mx] = std::minmax_element(levels.begin(), levels.end());
    CHECK(*mn / *mx < 0.05);
}

TEST_CASE("Vibrato swings pitch by the requested cents", "[dsp][vibrato]") {
    vox::dsp::Vibrato v;
    v.prepare(kFs);
    v.setRate(4.0F);
    v.setDepthCents(50.0F);
    auto tone = vt::sine(440.0, 2.0);
    const auto out = vt::renderOffline(v, tone, 128);
    const auto track =
        vt::f0Track(std::span<const float>(out).subspan(4800), kFs, 300.0, 600.0, 0.02, 0.005);
    double lo = 1e9;
    double hi = 0.0;
    for (const double f : track) {
        if (f > 0.0) {
            lo = std::min(lo, f);
            hi = std::max(hi, f);
        }
    }
    INFO("range " << vt::centsBetween(lo, 440.0) << " .. " << vt::centsBetween(hi, 440.0)
                  << " cents");
    CHECK(vt::centsBetween(hi, 440.0) == Approx(50.0).margin(12.0));
    CHECK(vt::centsBetween(lo, 440.0) == Approx(-50.0).margin(12.0));
}

TEST_CASE("Modulation effects keep level and stay finite", "[dsp][modulation]") {
    const auto in = voice(140.0);
    const double inDb = vt::toDb(vt::rms(in));
    vox::dsp::Chorus chorus;
    chorus.prepare(kFs);
    chorus.setVoices(3);
    chorus.setMix(0.6F);
    vox::dsp::Flanger flanger;
    flanger.prepare(kFs);
    flanger.setFeedback(0.7F);
    vox::dsp::Phaser phaser;
    phaser.prepare(kFs);
    phaser.setFeedback(0.6F);
    for (auto out : {vt::renderOffline(chorus, in, 128), vt::renderOffline(flanger, in, 128),
                     vt::renderOffline(phaser, in, 128)}) {
        CHECK(vt::allFinite(out));
        CHECK(std::abs(vt::toDb(vt::rms(out)) - inDb) < 6.0);
    }
}

TEST_CASE("Comb resonator rings at its tuned frequency", "[dsp][comb]") {
    vox::dsp::CombResonator c;
    c.prepare(kFs);
    c.setFrequency(300.0F);
    c.setFeedback(0.9F);
    c.setDampingHz(15000.0F);
    c.setMix(1.0F);
    auto noise = vt::whiteNoise(1.0, kFs, 0.3F);
    const auto out = vt::renderOffline(c, noise, 128);
    const auto x = std::span<const float>(out).subspan(4800);
    CHECK(levelAt(x, 300.0, 5.0) - levelAt(x, 450.0, 5.0) > 15.0);
}

TEST_CASE("Vocoder imposes the carrier pitch on speech", "[dsp][vocoder]") {
    vox::dsp::ChannelVocoder v;
    v.prepare(kFs, 512);
    v.setBands(24);
    v.setCarrier(vox::dsp::ChannelVocoder::Carrier::Saw);
    v.setCarrierHz(98.0F);
    v.setSibilance(0.0F);
    v.setMix(1.0F);
    const auto out = vt::renderOffline(v, voice(170.0), 256);
    const auto est =
        vt::estimateF0(std::span<const float>(out).subspan(9600, 48000), kFs, 50.0, 500.0);
    CHECK(vt::centsBetween(est.hz, 98.0) == Approx(0.0).margin(10.0));
    CHECK(std::abs(vt::toDb(vt::rms(out)) - vt::toDb(vt::rms(voice(170.0)))) < 8.0);
}

TEST_CASE("Vocoder in follow mode tracks the speaker's pitch", "[dsp][vocoder]") {
    vox::dsp::ChannelVocoder v;
    v.prepare(kFs, 512);
    v.setFollowPitch(true, 12.0F);
    v.setSibilance(0.0F);
    v.setMix(1.0F);
    const auto out = vt::renderOffline(v, voice(110.0), 256);
    const auto est =
        vt::estimateF0(std::span<const float>(out).subspan(14400, 38400), kFs, 50.0, 600.0);
    CHECK(vt::centsBetween(est.hz, 220.0) == Approx(0.0).margin(15.0));
}

TEST_CASE("Harmonizer adds the requested interval and keeps the dry voice", "[dsp][harmonizer]") {
    vox::dsp::Harmonizer h;
    h.prepare(kFs, 512);
    h.setDryGainDb(0.0F);
    h.setVoice(0, true, 7.0F, 0.0F, 0.0F);
    const auto out = vt::renderOffline(h, voice(150.0), 128);
    const auto x = std::span<const float>(out).subspan(h.latencySamples() + 9600, 36000);
    // Both fundamentals present with similar strength.
    CHECK(std::abs(levelAt(x, 150.0, 4.0) - levelAt(x, 150.0 * 1.4983, 4.0)) < 8.0);
    CHECK(levelAt(x, 150.0 * 1.4983, 4.0) - levelAt(x, 190.0, 4.0) > 20.0);
}

TEST_CASE("Whisper removes pitch but keeps formants", "[dsp][whisper]") {
    vox::dsp::Whisper w;
    w.prepare(kFs);
    const auto in = voice(120.0);
    const auto out = vt::renderOffline(w, in, 128);
    const auto x = std::span<const float>(out).subspan(9600, 48000);
    CHECK(vt::estimateF0(x, kFs, 60.0, 500.0).aperiodicity > 0.3);
    const auto envIn =
        vt::lpcEnvelopeDb(std::span<const float>(in).subspan(9600, 48000), kFs, 44, 2048);
    const auto envOut = vt::lpcEnvelopeDb(x, kFs, 44, 2048);
    CHECK(vt::envelopeDistanceDb(envIn, envOut, kFs, 300.0, 4000.0) < 6.0);
    CHECK(std::abs(vt::toDb(vt::rms(x)) -
                   vt::toDb(vt::rms(std::span<const float>(in).subspan(9600, 48000)))) < 4.0);
}

TEST_CASE("Whisper output has no level spikes on speech onsets", "[dsp][whisper]") {
    vox::dsp::Whisper w;
    w.prepare(kFs);
    const auto phrase = vt::synthPhrase(120.0);
    const auto out = vt::renderOffline(w, phrase.audio, 256);
    // Noise has a higher crest factor than a vowel, so compare short-term
    // loudness instead of peaks: no 10 ms window may be more than 12 dB
    // louder than the input (no smeared tails after words).
    const auto inLevels = windowedRms(phrase.audio, 480);
    const auto outLevels = windowedRms(out, 480);
    double worst = 0.0;
    for (std::size_t i = 0; i < inLevels.size(); ++i) {
        if (inLevels[i] > 0.01) {
            worst = std::max(worst, outLevels[i] / inLevels[i]);
        }
    }
    INFO("largest short-term level ratio " << worst);
    CHECK(worst < 4.0);
    CHECK(vt::peakAbs(out) < 2.0F);
}

TEST_CASE("Every ambience kind produces sound and stops when disabled", "[dsp][ambience]") {
    using Kind = vox::dsp::Ambience::Kind;
    const Kind kind = GENERATE(Kind::Rain, Kind::Wind, Kind::Crowd, Kind::EngineHum,
                               Kind::RadioStatic, Kind::SpaceDrone, Kind::CaveDrips,
                               Kind::Underwater, Kind::Fire, Kind::Traffic, Kind::Computer);
    vox::dsp::Ambience a;
    a.prepare(kFs);
    a.setKind(kind);
    a.setLevelDb(-12.0F);
    std::vector<float> silenceIn(static_cast<std::size_t>(kFs * 3.0), 0.0F);
    const auto out = vt::renderOffline(a, silenceIn, 256);
    CHECK(vt::allFinite(out));
    const double level = vt::toDb(vt::rms(out));
    INFO("kind " << static_cast<int>(kind) << " level " << level << " dBFS");
    CHECK(level > -60.0);
    CHECK(level < -6.0);
    a.setEnabled(false);
    std::vector<float> more(static_cast<std::size_t>(kFs), 0.0F);
    const auto after = vt::renderOffline(a, more, 256);
    CHECK(vt::peakAbs(std::span<const float>(after).subspan(24000)) == 0.0F);
}

TEST_CASE("Stutter repeats slices", "[dsp][stutter]") {
    vox::dsp::Stutter s;
    s.prepare(kFs);
    s.setSliceMs(50.0F);
    s.setProbability(1.0F);
    s.setMaxRepeats(2);
    auto noise = vt::whiteNoise(2.0, kFs, 0.3F, 5);
    const auto out = vt::renderOffline(s, noise, 128);
    // A repeated slice correlates with the output one slice earlier.
    const std::size_t slice = 2400;
    double best = 0.0;
    for (std::size_t start = 9600; start + 2 * slice < out.size(); start += slice / 2) {
        double num = 0.0;
        double ea = 0.0;
        double eb = 0.0;
        for (std::size_t i = 0; i < slice / 2; ++i) {
            const auto a = static_cast<double>(out[start + i]);
            const auto b = static_cast<double>(out[start + i + slice]);
            num += a * b;
            ea += a * a;
            eb += b * b;
        }
        best = std::max(best, num / std::sqrt(ea * eb + 1e-12));
    }
    CHECK(best > 0.9);
}
