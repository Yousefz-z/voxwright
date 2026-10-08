#include "vox/plugins/registry.hpp"

#include <algorithm>
#include <string>

namespace vox::plugins {

const EffectRegistry& EffectRegistry::builtin() {
    static const EffectRegistry kRegistry = [] {
        EffectRegistry r;
        registerBuiltinEffects(r);
        return r;
    }();
    return kRegistry;
}

void EffectRegistry::add(EffectDescriptor descriptor, Factory factory) {
    descriptors_.push_back(std::move(descriptor));
    factories_.push_back(std::move(factory));
}

const EffectDescriptor* EffectRegistry::find(std::string_view id) const noexcept {
    const auto it = std::find_if(descriptors_.begin(), descriptors_.end(),
                                 [id](const EffectDescriptor& d) { return d.id == id; });
    return it == descriptors_.end() ? nullptr : &*it;
}

Result<std::unique_ptr<EffectNode>> EffectRegistry::create(std::string_view id) const {
    for (std::size_t i = 0; i < descriptors_.size(); ++i) {
        if (descriptors_[i].id == id) {
            return factories_[i]();
        }
    }
    return makeError(ErrorCode::UnknownEffect,
                     "This voice uses an effect called \"" + std::string(id) +
                         "\" that this version of Voxwright does not have. Update the app or "
                         "remove that block from the voice.");
}

} // namespace vox::plugins
