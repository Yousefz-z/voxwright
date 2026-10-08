#pragma once

#include "vox/plugins/effect.hpp"
#include "vox/plugins/registry.hpp"
#include "vox/plugins/voice_preset.hpp"

#include <vox/core/result.hpp>
#include <vox/dsp/equalizer.hpp>
#include <vox/dsp/smoothed_value.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vox::plugins {

/// User-adjustable state of a voice that is not part of the preset itself.
struct VoiceSettings {
    std::vector<float> macroPositions; ///< One per preset macro; missing = default.
    float bassDb = 0.0F;
    float trebleDb = 0.0F;
    bool backgroundEnabled = true;
};

/// A running instance of a voice: its effect blocks in order, followed by a
/// finishing tone stage (bass and treble shelves) and the output gain.
///
/// build() runs on the control thread and allocates. Everything else is
/// real-time safe and is called from the audio thread only.
class VoiceChain {
public:
    [[nodiscard]] static Result<std::unique_ptr<VoiceChain>> build(const VoicePreset& preset,
                                                                   const VoiceSettings& settings,
                                                                   const EffectRegistry& registry,
                                                                   const PrepareContext& context);

    void process(std::span<float> block) noexcept;
    void reset() noexcept;

    void setParameter(std::size_t block, std::size_t param, float value) noexcept;
    void setBlockBypassed(std::size_t block, bool bypassed) noexcept;
    void setTone(float bassDb, float trebleDb) noexcept;
    void setBackgroundEnabled(bool enabled) noexcept;

    [[nodiscard]] std::size_t latencySamples() const noexcept;
    [[nodiscard]] std::size_t blockCount() const noexcept { return blocks_.size(); }
    [[nodiscard]] const std::string& voiceId() const noexcept { return voiceId_; }
    [[nodiscard]] float parameter(std::size_t block, std::size_t param) const noexcept;

private:
    VoiceChain() = default;

    struct Block {
        std::unique_ptr<EffectNode> node;
        bool bypassed = false;
        float wet = 1.0F; ///< Crossfade position between dry (0) and processed (1).
    };

    std::string voiceId_;
    std::vector<Block> blocks_;
    std::vector<float> dry_;
    std::size_t maxBlock_ = 1024;
    float fadeStep_ = 0.0F;
    dsp::Equalizer tone_;
    dsp::SmoothedValue outputGain_;
};

} // namespace vox::plugins
