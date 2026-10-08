#pragma once

#include <vox/engine/processing_graph.hpp>
#include <vox/plugins/registry.hpp>
#include <vox/plugins/voice_chain.hpp>
#include <vox/plugins/voice_preset.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace vox::engine::test {

inline constexpr double kFs = 48000.0;

/// A voice that only changes the level, so its output is easy to predict.
inline plugins::VoicePreset gainVoice(float db) {
    const std::string json = R"({"version": 1, "id": "gain-test", "name": "Gain Test",
        "category": "Utility", "blocks": [{"effect": "gain", "params": {"gain": )" +
                             std::to_string(db) + "}}]}";
    auto preset = plugins::parseVoicePreset(json, plugins::EffectRegistry::builtin());
    if (!preset) {
        std::abort();
    }
    return std::move(preset).value();
}

inline std::unique_ptr<plugins::VoiceChain> buildChain(const plugins::VoicePreset& preset,
                                                       std::size_t maxBlock = 512) {
    plugins::PrepareContext context;
    context.sampleRate = kFs;
    context.maxBlockSize = maxBlock;
    auto chain = plugins::VoiceChain::build(preset, plugins::VoiceSettings{},
                                            plugins::EffectRegistry::builtin(), context);
    if (!chain) {
        std::abort();
    }
    return std::move(chain).value();
}

/// Largest absolute difference between consecutive samples.
inline float maxStep(std::span<const float> x) {
    float m = 0.0F;
    for (std::size_t i = 1; i < x.size(); ++i) {
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    }
    return m;
}

/// Steepest step of a sine with this amplitude and frequency.
inline float sineSlope(double amplitude, double hz) {
    return static_cast<float>(amplitude * 2.0 * std::numbers::pi * hz / kFs);
}

/// Runs the graph over `input` in blocks; returns {virtual mic, monitor}.
struct GraphOutput {
    std::vector<float> mic;
    std::vector<float> monitor;
};

inline GraphOutput runGraph(ProcessingGraph& graph, std::span<const float> input,
                            std::size_t block = 128) {
    GraphOutput out;
    out.mic.assign(input.size(), 0.0F);
    out.monitor.assign(input.size(), 0.0F);
    for (std::size_t pos = 0; pos < input.size(); pos += block) {
        const std::size_t n = std::min(block, input.size() - pos);
        graph.process(input.subspan(pos, n), std::span<float>(out.mic).subspan(pos, n),
                      std::span<float>(out.monitor).subspan(pos, n));
    }
    return out;
}

} // namespace vox::engine::test
