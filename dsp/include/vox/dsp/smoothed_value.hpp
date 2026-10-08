#pragma once

#include <algorithm>
#include <cmath>

namespace vox::dsp {

/// Linear parameter ramp. setTarget() is real-time safe; the value glides to
/// the target over the configured ramp time so parameter changes never click.
class SmoothedValue {
public:
    void prepare(double sampleRate, float rampMs) noexcept {
        rampSamples_ =
            std::max(1, static_cast<int>(sampleRate * static_cast<double>(rampMs) * 0.001));
        setImmediate(target_);
    }

    void setTarget(float target) noexcept {
        if (target == target_) {
            return;
        }
        target_ = target;
        remaining_ = rampSamples_;
        step_ = (target_ - current_) / static_cast<float>(rampSamples_);
    }

    void setImmediate(float value) noexcept {
        current_ = value;
        target_ = value;
        remaining_ = 0;
        step_ = 0.0F;
    }

    [[nodiscard]] float next() noexcept {
        if (remaining_ > 0) {
            --remaining_;
            current_ = remaining_ == 0 ? target_ : current_ + step_;
        }
        return current_;
    }

    /// Advances by n samples and returns the value reached.
    float skip(int n) noexcept {
        if (remaining_ <= 0) {
            return current_;
        }
        if (n >= remaining_) {
            current_ = target_;
            remaining_ = 0;
        } else {
            current_ += step_ * static_cast<float>(n);
            remaining_ -= n;
        }
        return current_;
    }

    [[nodiscard]] float current() const noexcept { return current_; }
    [[nodiscard]] float target() const noexcept { return target_; }
    [[nodiscard]] bool isSmoothing() const noexcept { return remaining_ > 0; }

private:
    float current_ = 0.0F;
    float target_ = 0.0F;
    float step_ = 0.0F;
    int remaining_ = 0;
    int rampSamples_ = 1;
};

} // namespace vox::dsp
