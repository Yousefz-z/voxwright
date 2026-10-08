#include <vox/plugins/registry.hpp>
#include <vox/testing/alloc_trap.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

using vox::plugins::EffectRegistry;
using vox::plugins::ParamKind;

TEST_CASE("Built-in effect descriptors are complete and consistent", "[plugins][registry]") {
    const EffectRegistry& registry = EffectRegistry::builtin();
    REQUIRE(registry.descriptors().size() >= 20);
    std::set<std::string> ids;
    for (const auto& d : registry.descriptors()) {
        INFO("effect " << d.id);
        CHECK(ids.insert(d.id).second);
        CHECK_FALSE(d.name.empty());
        CHECK_FALSE(d.category.empty());
        CHECK_FALSE(d.description.empty());
        std::set<std::string> params;
        for (const auto& p : d.params) {
            INFO("param " << p.id);
            CHECK(params.insert(p.id).second);
            CHECK(p.min < p.max);
            CHECK(p.defaultValue >= p.min);
            CHECK(p.defaultValue <= p.max);
            if (p.kind == ParamKind::Choice) {
                CHECK(p.choices.size() >= 2);
            }
            const float roundTrip = p.fromNormalized(p.toNormalized(p.defaultValue));
            CHECK(std::abs(roundTrip - p.defaultValue) <=
                  1e-4F * std::max(1.0F, std::abs(p.defaultValue)));
        }
    }
}

TEST_CASE("Unknown effects produce a specific error", "[plugins][registry]") {
    const auto node = EffectRegistry::builtin().create("does-not-exist");
    REQUIRE_FALSE(node.hasValue());
    CHECK(node.error().code == vox::ErrorCode::UnknownEffect);
    CHECK(node.error().message.find("does-not-exist") != std::string::npos);
}

TEST_CASE("Every effect processes cleanly across its parameter range", "[plugins][registry]") {
    const EffectRegistry& registry = EffectRegistry::builtin();
    vox::testing::VoiceSpec spec;
    spec.seconds = 0.4;
    const auto voice = vox::testing::synthVoice(spec);
    for (const auto& d : registry.descriptors()) {
        INFO("effect " << d.id);
        auto created = registry.create(d.id);
        REQUIRE(created.hasValue());
        auto node = std::move(created).value();
        node->prepare({48000.0, 256, 75.0F});
        for (std::size_t p = 0; p < d.params.size(); ++p) {
            for (const float position : {0.0F, 0.5F, 1.0F}) {
                node->setParameter(p, d.params[p].fromNormalized(position));
                auto block = voice;
                for (std::size_t pos = 0; pos + 256 <= block.size(); pos += 256) {
                    node->process(std::span<float>(block).subspan(pos, 256));
                }
                INFO("param " << d.params[p].id << " at " << position);
                REQUIRE(vox::testing::allFinite(block));
                CHECK(vox::testing::peakAbs(block) < 16.0F);
            }
            node->setParameter(p, d.params[p].defaultValue);
        }
    }
}

TEST_CASE("Parameter changes on the audio thread do not allocate", "[plugins][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    const EffectRegistry& registry = EffectRegistry::builtin();
    auto voice = vox::testing::synthVoice({});
    for (const auto& d : registry.descriptors()) {
        auto node = registry.create(d.id).value();
        node->prepare({48000.0, 256, 75.0F});
        std::size_t allocations = 0;
        {
            const vox::testing::AllocationTrap trap;
            for (std::size_t pos = 0; pos + 256 <= 48000; pos += 256) {
                const std::size_t p = (pos / 256) % d.params.size();
                node->setParameter(
                    p, d.params[p].fromNormalized(static_cast<float>(pos % 1000) / 1000.0F));
                node->process(std::span<float>(voice).subspan(pos, 256));
            }
            allocations = trap.allocations() + trap.deallocations();
        }
        INFO("effect " << d.id);
        CHECK(allocations == 0);
    }
}
