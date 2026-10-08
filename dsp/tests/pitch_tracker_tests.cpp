#include <vox/dsp/pitch_tracker.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <numbers>
#include <span>

namespace {

struct TrackStats {
    double maxCentsError = 0.0;
    double voicedFraction = 0.0;
};

TrackStats trackSustained(double f0) {
    vox::dsp::PitchTracker tracker;
    tracker.prepare({48000.0, 70.0F, 800.0F});
    vox::testing::VoiceSpec spec;
    spec.f0Hz = f0;
    spec.seconds = 1.0;
    const auto voice = vox::testing::synthVoice(spec);
    TrackStats stats;
    int analysed = 0;
    int voiced = 0;
    constexpr std::size_t kBlock = 128;
    for (std::size_t pos = 0; pos + kBlock <= voice.size(); pos += kBlock) {
        tracker.push(std::span<const float>(voice).subspan(pos, kBlock));
        if (pos < 9600 || pos > voice.size() - 4800) {
            continue; // skip onset and fade-out
        }
        ++analysed;
        if (tracker.voiced()) {
            ++voiced;
            stats.maxCentsError =
                std::max(stats.maxCentsError,
                         std::abs(vox::testing::centsBetween(tracker.frequencyHz(), f0)));
        }
    }
    stats.voicedFraction = static_cast<double>(voiced) / static_cast<double>(analysed);
    return stats;
}

} // namespace

TEST_CASE("Pitch tracker is accurate to within 2 cents on sustained vowels", "[dsp][pitch]") {
    const double f0 = GENERATE(82.0, 110.0, 147.0, 220.0, 330.0, 523.0);
    const TrackStats stats = trackSustained(f0);
    INFO("f0 = " << f0 << " Hz, max error " << stats.maxCentsError << " cents");
    CHECK(stats.voicedFraction > 0.99);
    CHECK(stats.maxCentsError < 2.0);
}

TEST_CASE("Pitch tracker follows a glide with vibrato", "[dsp][pitch]") {
    vox::dsp::PitchTracker tracker;
    tracker.prepare({48000.0, 70.0F, 800.0F});
    vox::testing::VoiceSpec spec;
    spec.f0Hz = 100.0;
    spec.f0EndHz = 200.0;
    spec.vibratoHz = 5.0;
    spec.vibratoCents = 30.0;
    spec.seconds = 2.0;
    const auto voice = vox::testing::synthVoice(spec);
    double worst = 0.0;
    constexpr std::size_t kBlock = 64;
    for (std::size_t pos = 0; pos + kBlock <= voice.size(); pos += kBlock) {
        tracker.push(std::span<const float>(voice).subspan(pos, kBlock));
        if (pos < 9600 || pos > voice.size() - 4800 || !tracker.voiced()) {
            continue;
        }
        // Ground truth at the centre of the tracker's analysis window (~13 ms back).
        const double t = (static_cast<double>(pos + kBlock) - 640.0) / 48000.0;
        const double truth = 100.0 * std::pow(2.0, t / 2.0) *
                             std::exp2(30.0 / 1200.0 * std::sin(2.0 * std::numbers::pi * 5.0 * t));
        worst = std::max(worst, std::abs(vox::testing::centsBetween(tracker.frequencyHz(), truth)));
    }
    INFO("worst deviation " << worst << " cents");
    CHECK(worst < 25.0);
}

TEST_CASE("Pitch tracker reports noise and silence as unvoiced", "[dsp][pitch]") {
    vox::dsp::PitchTracker tracker;
    tracker.prepare({48000.0, 70.0F, 800.0F});
    const auto noise = vox::testing::whiteNoise(1.0, 48000.0, 0.3F);
    int voiced = 0;
    int total = 0;
    for (std::size_t pos = 0; pos + 256 <= noise.size(); pos += 256) {
        tracker.push(std::span<const float>(noise).subspan(pos, 256));
        if (pos > 4800) {
            ++total;
            voiced += tracker.voiced() ? 1 : 0;
        }
    }
    CHECK(static_cast<double>(voiced) / static_cast<double>(total) < 0.05);

    const auto quiet = vox::testing::silence(0.5);
    tracker.push(quiet);
    CHECK_FALSE(tracker.voiced());
}
