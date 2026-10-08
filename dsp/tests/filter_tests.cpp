#include <vox/dsp/biquad.hpp>
#include <vox/dsp/delay_line.hpp>
#include <vox/dsp/smoothed_value.hpp>
#include <vox/dsp/svf.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Approx;
using vox::dsp::BiquadType;

namespace {
constexpr double kButterworthQ = 0.70710678118654752;
constexpr double kSqrt2 = std::numbers::sqrt2;
} // namespace

TEST_CASE("Biquad designs hit their specified responses", "[dsp][biquad]") {
    constexpr double kFs = 48000.0;
    const auto lp = vox::dsp::designBiquad(BiquadType::Lowpass, kFs, 1000.0, kButterworthQ);
    CHECK(vox::dsp::biquadMagnitudeDb(lp, kFs, 1000.0) == Approx(-3.01).margin(0.05));
    CHECK(vox::dsp::biquadMagnitudeDb(lp, kFs, 100.0) == Approx(0.0).margin(0.05));
    CHECK(vox::dsp::biquadMagnitudeDb(lp, kFs, 8000.0) < -30.0);

    const auto hp = vox::dsp::designBiquad(BiquadType::Highpass, kFs, 200.0, kButterworthQ);
    CHECK(vox::dsp::biquadMagnitudeDb(hp, kFs, 200.0) == Approx(-3.01).margin(0.05));
    CHECK(vox::dsp::biquadMagnitudeDb(hp, kFs, 20.0) < -35.0);

    const auto peak = vox::dsp::designBiquad(BiquadType::Peak, kFs, 2500.0, 1.0, 6.0);
    CHECK(vox::dsp::biquadMagnitudeDb(peak, kFs, 2500.0) == Approx(6.0).margin(0.01));

    const auto shelf =
        vox::dsp::designBiquad(BiquadType::LowShelf, kFs, 300.0, kButterworthQ, -9.0);
    CHECK(vox::dsp::biquadMagnitudeDb(shelf, kFs, 30.0) == Approx(-9.0).margin(0.2));
    CHECK(vox::dsp::biquadMagnitudeDb(shelf, kFs, 10000.0) == Approx(0.0).margin(0.1));
}

TEST_CASE("Biquad processing matches its analytic response", "[dsp][biquad]") {
    vox::dsp::Biquad filter;
    filter.setCoefficients(
        vox::dsp::designBiquad(BiquadType::Lowpass, 48000.0, 2000.0, kButterworthQ));
    auto tone = vox::testing::sine(4000.0, 1.0);
    filter.process(tone);
    const double measured = vox::testing::toDb(
        vox::testing::rms(std::span<const float>(tone).subspan(4800)) / (0.5 / kSqrt2));
    const double expected = vox::dsp::biquadMagnitudeDb(filter.coefficients(), 48000.0, 4000.0);
    CHECK(measured == Approx(expected).margin(0.1));
}

TEST_CASE("State variable filter band output peaks at 0 dB on its centre", "[dsp][svf]") {
    vox::dsp::StateVariableFilter svf;
    svf.prepare(48000.0);
    svf.setParameters(1000.0F, 5.0F);
    auto tone = vox::testing::sine(1000.0, 0.5);
    for (float& s : tone) {
        s = svf.processBandNormalized(s);
    }
    const double level = vox::testing::rms(std::span<const float>(tone).subspan(12000));
    CHECK(vox::testing::toDb(level / (0.5 / kSqrt2)) == Approx(0.0).margin(0.2));
}

TEST_CASE("Delay line returns integer and fractional delays", "[dsp][delay]") {
    vox::dsp::DelayLine delay;
    delay.prepare(64);
    for (int i = 0; i < 40; ++i) {
        delay.push(static_cast<float>(i));
    }
    CHECK(delay.readInteger(1) == Approx(39.0F));
    CHECK(delay.readInteger(10) == Approx(30.0F));
    CHECK(delay.read(10.5F) == Approx(29.5F).margin(1e-4F)); // linear ramp is exact for Hermite
    CHECK(delay.read(1.25F) == Approx(38.75F).margin(1e-4F));
}

TEST_CASE("Smoothed value reaches its target linearly in the ramp time", "[dsp][smoothing]") {
    vox::dsp::SmoothedValue v;
    v.prepare(1000.0, 10.0F); // 10 samples
    v.setImmediate(0.0F);
    v.setTarget(1.0F);
    float last = 0.0F;
    for (int i = 0; i < 9; ++i) {
        const float x = v.next();
        CHECK(x > last);
        last = x;
    }
    CHECK(v.next() == 1.0F);
    CHECK_FALSE(v.isSmoothing());
}
