#include "vox/plugins/effect.hpp"

#include <algorithm>
#include <cmath>

namespace vox::plugins {

float ParamSpec::clamp(float value) const noexcept {
    if (!std::isfinite(value)) {
        return defaultValue;
    }
    switch (kind) {
    case ParamKind::Toggle:
        return value >= 0.5F ? 1.0F : 0.0F;
    case ParamKind::Choice: {
        const float last = choices.empty() ? 0.0F : static_cast<float>(choices.size() - 1);
        return std::clamp(std::round(value), 0.0F, last);
    }
    case ParamKind::Continuous:
        break;
    }
    return std::clamp(value, min, max);
}

float ParamSpec::fromNormalized(float position) const noexcept {
    const float p = std::clamp(position, 0.0F, 1.0F);
    if (scale == ParamScale::Logarithmic && min > 0.0F && max > min) {
        return clamp(min * std::pow(max / min, p));
    }
    return clamp(min + p * (max - min));
}

float ParamSpec::toNormalized(float value) const noexcept {
    const float v = clamp(value);
    if (max <= min) {
        return 0.0F;
    }
    if (scale == ParamScale::Logarithmic && min > 0.0F) {
        return std::log(v / min) / std::log(max / min);
    }
    return (v - min) / (max - min);
}

std::size_t EffectDescriptor::indexOf(std::string_view paramId) const noexcept {
    const auto it = std::find_if(params.begin(), params.end(),
                                 [paramId](const ParamSpec& p) { return p.id == paramId; });
    return static_cast<std::size_t>(it - params.begin());
}

} // namespace vox::plugins
