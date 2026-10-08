#include <vox/dsp/meters.hpp>
#include <vox/plugins/sound_pack.hpp>
#include <vox/testing/analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>
#include <string>

namespace {

struct Measured {
    std::string id;
    double seconds = 0.0;
    double centroid = 0.0;
};

} // namespace

TEST_CASE("Every built-in sound renders cleanly at the pack loudness", "[plugins][sounds]") {
    const auto sounds = vox::plugins::builtinSounds();
    REQUIRE(sounds.size() >= 16);
    std::set<std::string> ids;
    std::vector<Measured> all;
    for (const auto& s : sounds) {
        INFO(s.id);
        CHECK(ids.insert(std::string(s.id)).second); // unique ids
        CHECK_FALSE(s.name.empty());
        CHECK(s.color.size() == 7);
        const auto rendered = vox::plugins::renderBuiltinSound(s.id);
        REQUIRE(rendered);
        const auto& x = rendered.value();
        const double seconds = static_cast<double>(x.size()) / 48000.0;
        CHECK(seconds > 0.4);
        CHECK(seconds < 5.0);
        CHECK(vox::testing::allFinite(x));
        CHECK(vox::testing::peakAbs(x) <= 0.8913F);
        const double lufs = vox::dsp::integratedLoudness(x, 48000.0);
        CHECK(std::abs(lufs - vox::plugins::kBuiltinSoundLufs) < 1.0);
        double mean = 0.0;
        for (const float v : x) {
            mean += static_cast<double>(v);
        }
        CHECK(std::abs(mean / static_cast<double>(x.size())) < 1e-3);
        // Starts and ends quietly: no click when played or cut.
        CHECK(vox::testing::peakAbs(std::span<const float>(x).first(24)) < 0.05F);
        CHECK(vox::testing::peakAbs(std::span<const float>(x).last(24)) < 0.05F);
        all.push_back({std::string(s.id), seconds, vox::testing::spectralCentroidHz(x, 48000.0)});
    }
    // No two sounds are near-duplicates in length and brightness.
    for (std::size_t a = 0; a < all.size(); ++a) {
        for (std::size_t b = a + 1; b < all.size(); ++b) {
            const double dt = std::abs(all[a].seconds - all[b].seconds);
            const double dc = std::abs(std::log2(all[a].centroid / all[b].centroid));
            INFO(all[a].id << " / " << all[b].id);
            CHECK((dt > 0.05 || dc > 0.1));
        }
    }
}

TEST_CASE("Built-in sounds render at other rates and reject unknown ids", "[plugins][sounds]") {
    const auto at44 = vox::plugins::renderBuiltinSound("whoosh", 44100.0);
    const auto at48 = vox::plugins::renderBuiltinSound("whoosh", 48000.0);
    REQUIRE(at44);
    REQUIRE(at48);
    const double s44 = static_cast<double>(at44.value().size()) / 44100.0;
    const double s48 = static_cast<double>(at48.value().size()) / 48000.0;
    CHECK(std::abs(s44 - s48) < 0.01);
    const auto missing = vox::plugins::renderBuiltinSound("no-such-sound");
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == vox::ErrorCode::InvalidArgument);
}
