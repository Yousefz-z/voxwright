#include "vox/engine/transmit_control.hpp"

#include <vox/dsp/math.hpp>

#include <algorithm>
#include <cmath>

namespace vox::engine {
namespace {

constexpr float kAttackMs = 10.0F;
constexpr float kReleaseMs = 20.0F;
constexpr double kBeepHz = 1000.0;
constexpr float kBeepLevel = 0.25F; // -12 dBFS

} // namespace

void TransmitControl::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    attackStep_ = 1000.0F / (kAttackMs * static_cast<float>(sampleRate));
    releaseStep_ = 1000.0F / (kReleaseMs * static_cast<float>(sampleRate));
    gain_ = wantsTransmit() ? 1.0F : 0.0F;
    lastWanted_ = gain_ > 0.0F;
}

bool TransmitControl::wantsTransmit() const noexcept {
    if (muted_.load(std::memory_order_relaxed)) {
        return false;
    }
    switch (mode_.load(std::memory_order_relaxed)) {
    case TransmitMode::AlwaysOn:
        return true;
    case TransmitMode::PushToTalk:
        return talkKey_.load(std::memory_order_relaxed);
    case TransmitMode::PushToMute:
        return !talkKey_.load(std::memory_order_relaxed);
    }
    return true;
}

void TransmitControl::process(std::span<float> voice) noexcept {
    bool wanted = wantsTransmit();
    // Release delay keeps word endings after the talk key comes up. Only a
    // real key release starts it, not switching modes or unmuting.
    const bool key = talkKey_.load(std::memory_order_relaxed);
    if (lastKey_ && !key && !wanted &&
        mode_.load(std::memory_order_relaxed) == TransmitMode::PushToTalk &&
        !muted_.load(std::memory_order_relaxed)) {
        holdRemaining_ =
            static_cast<int>(static_cast<double>(releaseDelayMs_.load(std::memory_order_relaxed)) *
                             0.001 * sampleRate_);
    }
    if (wanted || muted_.load(std::memory_order_relaxed)) {
        holdRemaining_ = 0;
    }
    lastKey_ = key;
    lastWanted_ = wanted;
    if (!wanted && holdRemaining_ > 0) {
        wanted = true;
    }
    const bool censor = censorKey_.load(std::memory_order_relaxed);
    const double beepIncrement = kBeepHz / sampleRate_;
    for (float& s : voice) {
        if (holdRemaining_ > 0 && !lastWanted_) {
            --holdRemaining_;
            wanted = holdRemaining_ > 0;
        }
        gain_ = wanted ? std::min(1.0F, gain_ + attackStep_) : std::max(0.0F, gain_ - releaseStep_);
        censor_ =
            censor ? std::min(1.0F, censor_ + attackStep_) : std::max(0.0F, censor_ - releaseStep_);
        float out = s * gain_;
        if (censor_ > 0.0F) {
            beepPhase_ += beepIncrement;
            beepPhase_ -= std::floor(beepPhase_);
            const auto beep =
                static_cast<float>(std::sin(dsp::kTwoPiD * beepPhase_)) * kBeepLevel * gain_;
            out = out * (1.0F - censor_) + beep * censor_;
        }
        s = out;
    }
}

} // namespace vox::engine
