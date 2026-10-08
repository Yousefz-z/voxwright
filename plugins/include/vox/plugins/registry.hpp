#pragma once

#include "vox/plugins/effect.hpp"

#include <vox/core/result.hpp>

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace vox::plugins {

/// Catalogue of effect blocks: descriptor plus factory per effect id.
class EffectRegistry {
public:
    using Factory = std::function<std::unique_ptr<EffectNode>()>;

    /// The built-in effects (pitch, vocoder, reverb, ...).
    [[nodiscard]] static const EffectRegistry& builtin();

    void add(EffectDescriptor descriptor, Factory factory);

    [[nodiscard]] const EffectDescriptor* find(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<EffectDescriptor>& descriptors() const noexcept {
        return descriptors_;
    }

    /// Creates a node with every parameter at its default. Not prepared yet.
    [[nodiscard]] Result<std::unique_ptr<EffectNode>> create(std::string_view id) const;

private:
    std::vector<EffectDescriptor> descriptors_;
    std::vector<Factory> factories_;
};

/// Registers the built-in effects into `registry` (used by builtin()).
void registerBuiltinEffects(EffectRegistry& registry);

} // namespace vox::plugins
