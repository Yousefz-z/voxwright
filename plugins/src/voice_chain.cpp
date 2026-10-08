#include "vox/plugins/voice_chain.hpp"

#include <vox/dsp/math.hpp>

#include <algorithm>

namespace vox::plugins {
namespace {

constexpr float kBassHz = 150.0F;
constexpr float kTrebleHz = 5000.0F;
constexpr float kBypassFadeMs = 10.0F;

} // namespace

Result<std::unique_ptr<VoiceChain>> VoiceChain::build(const VoicePreset& preset,
                                                      const VoiceSettings& settings,
                                                      const EffectRegistry& registry,
                                                      const PrepareContext& context) {
    std::unique_ptr<VoiceChain> chain(new VoiceChain());
    chain->voiceId_ = preset.id;
    chain->maxBlock_ = context.maxBlockSize;
    chain->dry_.assign(context.maxBlockSize, 0.0F);
    chain->fadeStep_ = 1000.0F / (kBypassFadeMs * static_cast<float>(context.sampleRate));
    for (std::size_t b = 0; b < preset.blocks.size(); ++b) {
        auto node = registry.create(preset.blocks[b].effect);
        if (!node) {
            return node.error();
        }
        Block block;
        block.node = std::move(node).value();
        const std::vector<float> values =
            resolveBlockParameters(preset, b, settings.macroPositions, registry);
        // Stored in the node now; applied to the DSP object by prepare().
        for (std::size_t p = 0; p < values.size(); ++p) {
            block.node->setParameter(p, values[p]);
        }
        block.node->setBackgroundEnabled(settings.backgroundEnabled); // applies without a fade
        block.node->prepare(context);
        block.bypassed = preset.blocks[b].bypassed;
        block.wet = block.bypassed ? 0.0F : 1.0F;
        chain->blocks_.push_back(std::move(block));
    }
    chain->tone_.prepare(context.sampleRate);
    chain->tone_.setFrequency(dsp::Equalizer::LowShelf, kBassHz);
    chain->tone_.setFrequency(dsp::Equalizer::HighShelf, kTrebleHz);
    chain->tone_.setQ(dsp::Equalizer::LowShelf, 0.707F);
    chain->tone_.setQ(dsp::Equalizer::HighShelf, 0.707F);
    chain->tone_.setGainDb(dsp::Equalizer::LowShelf, settings.bassDb);
    chain->tone_.setGainDb(dsp::Equalizer::HighShelf, settings.trebleDb);
    chain->outputGain_.prepare(context.sampleRate, 30.0F);
    chain->outputGain_.setImmediate(dsp::dbToGain(preset.outputGainDb));
    chain->settle(context);
    return chain;
}

void VoiceChain::settle(const PrepareContext& context) {
    // Effects ramp parameter changes (30 to 50 ms) and treat the preset's
    // values as changes from their defaults. Running silence through the
    // chain lets every ramp finish; reset() then clears the signal state but
    // not the parameters, so the voice starts exactly as designed instead of
    // sweeping in from the defaults.
    constexpr double kSettleSeconds = 0.06;
    std::vector<float> silence(maxBlock_, 0.0F);
    const auto total = static_cast<std::size_t>(kSettleSeconds * context.sampleRate);
    for (std::size_t done = 0; done < total; done += maxBlock_) {
        std::fill(silence.begin(), silence.end(), 0.0F);
        process(std::span<float>(silence).first(std::min(maxBlock_, total - done)));
    }
    reset();
}

void VoiceChain::reset() noexcept {
    for (Block& b : blocks_) {
        b.node->reset();
    }
    tone_.reset();
}

void VoiceChain::setParameter(std::size_t block, std::size_t param, float value) noexcept {
    if (block < blocks_.size()) {
        blocks_[block].node->setParameter(param, value);
    }
}

float VoiceChain::parameter(std::size_t block, std::size_t param) const noexcept {
    return block < blocks_.size() ? blocks_[block].node->parameter(param) : 0.0F;
}

void VoiceChain::setBlockBypassed(std::size_t block, bool bypassed) noexcept {
    if (block < blocks_.size()) {
        blocks_[block].bypassed = bypassed;
    }
}

void VoiceChain::setTone(float bassDb, float trebleDb) noexcept {
    tone_.setGainDb(dsp::Equalizer::LowShelf, bassDb);
    tone_.setGainDb(dsp::Equalizer::HighShelf, trebleDb);
}

void VoiceChain::setBackgroundEnabled(bool enabled) noexcept {
    for (Block& b : blocks_) {
        b.node->setBackgroundEnabled(enabled);
    }
}

std::size_t VoiceChain::latencySamples() const noexcept {
    std::size_t total = 0;
    for (const Block& b : blocks_) {
        if (!b.bypassed) {
            total += b.node->latencySamples();
        }
    }
    return total;
}

void VoiceChain::process(std::span<float> block) noexcept {
    for (std::size_t pos = 0; pos < block.size(); pos += maxBlock_) {
        const auto chunk = block.subspan(pos, std::min(maxBlock_, block.size() - pos));
        for (Block& b : blocks_) {
            const float target = b.bypassed ? 0.0F : 1.0F;
            if (b.wet == target) {
                if (target > 0.0F) {
                    b.node->process(chunk);
                }
                continue;
            }
            // Crossfade between the unprocessed and processed signal.
            auto dry = std::span<float>(dry_).first(chunk.size());
            std::copy(chunk.begin(), chunk.end(), dry.begin());
            b.node->process(chunk);
            for (std::size_t i = 0; i < chunk.size(); ++i) {
                b.wet = target > b.wet ? std::min(1.0F, b.wet + fadeStep_)
                                       : std::max(0.0F, b.wet - fadeStep_);
                chunk[i] = dry[i] + b.wet * (chunk[i] - dry[i]);
            }
        }
        tone_.process(chunk);
        for (float& s : chunk) {
            s *= outputGain_.next();
        }
    }
}

} // namespace vox::plugins
