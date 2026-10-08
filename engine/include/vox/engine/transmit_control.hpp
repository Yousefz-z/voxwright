#pragma once

#include "vox/engine/types.hpp"

#include <atomic>
#include <span>

namespace vox::engine {

/// Decides whether the voice is sent (mute, push-to-talk, push-to-mute) and
/// produces the censor beep. Key state is written with atomics, so a hotkey
/// thread can update it without going through the control thread.
class TransmitControl {
public:
    void prepare(double sampleRate);

    // Any thread.
    void setMode(TransmitMode mode) noexcept { mode_.store(mode, std::memory_order_relaxed); }
    void setTalkKeyDown(bool down) noexcept { talkKey_.store(down, std::memory_order_relaxed); }
    void setMuted(bool muted) noexcept { muted_.store(muted, std::memory_order_relaxed); }
    void setCensorKeyDown(bool down) noexcept { censorKey_.store(down, std::memory_order_relaxed); }
    void setReleaseDelayMs(float ms) noexcept {
        releaseDelayMs_.store(ms, std::memory_order_relaxed);
    }

    // Audio thread.
    void process(std::span<float> voice) noexcept;
    [[nodiscard]] bool transmitting() const noexcept { return gain_ > 0.5F; }

private:
    [[nodiscard]] bool wantsTransmit() const noexcept;

    double sampleRate_ = 48000.0;
    std::atomic<TransmitMode> mode_{TransmitMode::AlwaysOn};
    std::atomic<bool> talkKey_{false};
    std::atomic<bool> muted_{false};
    std::atomic<bool> censorKey_{false};
    std::atomic<float> releaseDelayMs_{150.0F};
    float gain_ = 1.0F;
    float censor_ = 0.0F;
    float attackStep_ = 0.0F;
    float releaseStep_ = 0.0F;
    int holdRemaining_ = 0;
    bool lastWanted_ = true;
    bool lastKey_ = false;
    double beepPhase_ = 0.0;
};

} // namespace vox::engine
