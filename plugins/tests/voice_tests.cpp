#include "voice_metrics.hpp"

#include <vox/plugins/voice_chain.hpp>
#include <vox/plugins/voice_library.hpp>
#include <vox/testing/alloc_trap.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>
#include <vox/testing/timing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

using vox::plugins::EffectRegistry;
using vox::plugins::VoicePreset;
using vox::plugins::test::measureVoice;
using vox::plugins::test::VoiceMetrics;

namespace {

const std::vector<VoicePreset>& voices() {
    static const std::vector<VoicePreset> kAll = [] {
        auto loaded = vox::plugins::loadBuiltinVoices(EffectRegistry::builtin());
        if (!loaded) {
            FAIL(loaded.error().message);
        }
        return std::move(loaded).value();
    }();
    return kAll;
}

const std::vector<VoiceMetrics>& metrics() {
    static const std::vector<VoiceMetrics> kAll = [] {
        std::vector<VoiceMetrics> m;
        for (const auto& v : voices()) {
            m.push_back(measureVoice(v, EffectRegistry::builtin()));
        }
        return m;
    }();
    return kAll;
}

} // namespace

TEST_CASE("The built-in library has at least 40 valid, uniquely named voices",
          "[plugins][voices]") {
    REQUIRE(voices().size() >= 40);
    std::set<std::string> ids;
    std::set<std::string> names;
    std::set<std::string> categories;
    for (const auto& v : voices()) {
        INFO(v.id);
        CHECK(ids.insert(v.id).second);
        CHECK(names.insert(v.name).second);
        CHECK_FALSE(v.description.empty());
        CHECK_FALSE(v.blocks.empty());
        CHECK_FALSE(v.macros.empty());
        CHECK(v.color.size() == 7);
        categories.insert(v.category);
    }
    CHECK(categories.size() == vox::plugins::voiceCategories().size());
}

TEST_CASE("Every voice renders cleanly within its latency budget", "[plugins][voices]") {
    for (const auto& m : metrics()) {
        INFO(m.id << ": " << m.error);
        REQUIRE(m.built);
        CHECK(m.finite);
        CHECK(m.peak < 2.0);
        CHECK(m.realtimeFactor < 0.25 * vox::testing::kTimingScale);
    }
    for (std::size_t i = 0; i < voices().size(); ++i) {
        INFO(voices()[i].id << " latency " << metrics()[i].latencyMs << " ms");
        CHECK(metrics()[i].latencyMs <= static_cast<double>(voices()[i].expect.maxLatencyMs));
    }
}

TEST_CASE("Voices keep the input loudness within 2 LU", "[plugins][voices]") {
    for (const auto& m : metrics()) {
        INFO(m.id << " loudness change " << m.loudnessChangeLu << " LU");
        CHECK(std::abs(m.loudnessChangeLu) <= 2.0);
    }
}

TEST_CASE("Voices meet their measured pitch expectations", "[plugins][voices]") {
    for (std::size_t i = 0; i < voices().size(); ++i) {
        const auto& v = voices()[i];
        const auto& m = metrics()[i];
        INFO(v.id << ": shift " << m.pitchShiftSemitones << " st, median f0 " << m.outputMedianF0
                  << " Hz, aperiodicity " << m.aperiodicity << ", voiced " << m.voicedFraction);
        if (v.expect.pitchShiftSemitones) {
            CHECK(std::abs(m.pitchShiftSemitones -
                           static_cast<double>(*v.expect.pitchShiftSemitones)) < 0.35);
        }
        if (v.expect.fixedPitchHz) {
            CHECK(std::abs(vox::testing::centsBetween(
                      m.outputMedianF0, static_cast<double>(*v.expect.fixedPitchHz))) < 15.0);
        }
        if (v.expect.unpitched) {
            CHECK(m.voicedFraction < 0.2);
        }
    }
}

TEST_CASE("Voices are measurably distinct from each other", "[plugins][voices]") {
    const auto& m = metrics();
    double closest = 1e9;
    std::string pair;
    for (std::size_t a = 0; a < m.size(); ++a) {
        for (std::size_t b = a + 1; b < m.size(); ++b) {
            const double d = vox::plugins::test::voiceDistance(m[a], m[b]);
            if (d < closest) {
                closest = d;
                pair = m[a].id + " / " + m[b].id;
            }
        }
    }
    INFO("closest pair " << pair << " at distance " << closest);
    CHECK(closest > 0.25);
}

TEST_CASE("Voice chains do not allocate while processing", "[plugins][voices][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    auto audio = vox::testing::synthVoice({});
    for (const auto& v : voices()) {
        auto chain =
            vox::plugins::VoiceChain::build(v, {}, EffectRegistry::builtin(), {48000.0, 512, 75.0F})
                .value();
        std::size_t allocations = 0;
        {
            const vox::testing::AllocationTrap trap;
            for (std::size_t pos = 0; pos + 480 <= audio.size(); pos += 480) {
                chain->process(std::span<float>(audio).subspan(pos, 480));
                chain->setTone(static_cast<float>(pos % 7) - 3.0F, 1.0F);
                chain->setBlockBypassed(0, (pos / 480) % 10 == 5);
            }
            allocations = trap.allocations() + trap.deallocations();
        }
        INFO(v.id);
        CHECK(allocations == 0);
    }
}

TEST_CASE("The background switch silences ambience blocks", "[plugins][voices]") {
    const auto& all = voices();
    const auto it = std::find_if(all.begin(), all.end(),
                                 [](const VoicePreset& v) { return v.id == "rainy-window"; });
    REQUIRE(it != all.end());
    vox::plugins::VoiceSettings settings;
    settings.backgroundEnabled = false;
    auto chain = vox::plugins::VoiceChain::build(*it, settings, EffectRegistry::builtin(),
                                                 {48000.0, 512, 75.0F})
                     .value();
    std::vector<float> quiet(48000, 0.0F);
    chain->process(quiet);
    CHECK(vox::testing::peakAbs(quiet) < 1e-6F);
    chain->setBackgroundEnabled(true);
    std::vector<float> more(48000, 0.0F);
    chain->process(more);
    CHECK(vox::testing::rms(std::span<const float>(more).subspan(24000)) > 1e-3);
}
