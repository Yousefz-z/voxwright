#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vox::plugins {

/// How a parameter's slider maps to its value.
enum class ParamScale {
    Linear,
    Logarithmic, ///< Equal slider steps are equal ratios (frequencies, times).
};

enum class ParamKind {
    Continuous,
    Toggle, ///< 0 = off, 1 = on.
    Choice, ///< Integer index into `choices`.
};

/// Metadata for one effect parameter. The voice designer builds its controls
/// from these, so a new effect needs no UI code.
struct ParamSpec {
    std::string id;
    std::string name;
    std::string unit; ///< "st", "Hz", "ms", "dB", "%", "s", or empty.
    float min = 0.0F;
    float max = 1.0F;
    float defaultValue = 0.0F;
    ParamScale scale = ParamScale::Linear;
    ParamKind kind = ParamKind::Continuous;
    std::vector<std::string> choices;

    /// Clamps (and for choices and toggles rounds) a value into range.
    [[nodiscard]] float clamp(float value) const noexcept;
    /// Maps a slider position in [0, 1] to a value and back.
    [[nodiscard]] float fromNormalized(float position) const noexcept;
    [[nodiscard]] float toNormalized(float value) const noexcept;
};

struct EffectDescriptor {
    std::string id;
    std::string name;
    std::string category;
    std::string description;
    std::vector<ParamSpec> params;

    /// Index of a parameter by id, or params.size() if absent.
    [[nodiscard]] std::size_t indexOf(std::string_view paramId) const noexcept;
};

/// Settings every node needs at preparation time.
struct PrepareContext {
    double sampleRate = 48000.0;
    std::size_t maxBlockSize = 1024;
    /// Lowest speaking pitch expected from the user; sets the PSOLA latency
    /// (two periods of this frequency).
    float minVoiceHz = 75.0F;
};

/// Real-time effect instance. prepare() runs on the control thread and may
/// allocate; every other call is real-time safe.
class EffectNode {
public:
    EffectNode() = default;
    EffectNode(const EffectNode&) = delete;
    EffectNode& operator=(const EffectNode&) = delete;
    EffectNode(EffectNode&&) = delete;
    EffectNode& operator=(EffectNode&&) = delete;
    virtual ~EffectNode() = default;

    virtual void prepare(const PrepareContext& context) = 0;
    virtual void reset() noexcept = 0;
    /// Value is clamped by the caller (see ParamSpec::clamp).
    virtual void setParameter(std::size_t index, float value) noexcept = 0;
    [[nodiscard]] virtual float parameter(std::size_t index) const noexcept = 0;
    virtual void process(std::span<float> block) noexcept = 0;
    [[nodiscard]] virtual std::size_t latencySamples() const noexcept { return 0; }
    /// Global "background effects" switch; only ambience blocks react.
    virtual void setBackgroundEnabled(bool /*enabled*/) noexcept {}
};

} // namespace vox::plugins
