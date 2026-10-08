#pragma once

#include "vox/engine/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace vox::engine {

/// Immutable decoded sound at the engine rate. Created and destroyed on the
/// control thread; the audio thread only reads it.
struct Clip {
    std::vector<float> samples;
};

/// Real-time soundboard: a pool of 32 playback voices over a table of clips.
/// Everything here runs on the audio thread and never allocates.
class SoundboardPlayer {
public:
    static constexpr std::size_t kMaxVoices = 32;
    static constexpr std::size_t kMaxClips = 1024;

    void prepare(double sampleRate);

    /// Installs `clip` under `id`; returns the clip previously in that slot
    /// (to be handed back to the control thread for deletion).
    const Clip* install(std::uint32_t id, const Clip* clip) noexcept;
    /// Clears the slot, stopping any voice using it; returns the old clip.
    const Clip* remove(std::uint32_t id) noexcept;

    /// A press (keyDown = true) or release of the sound's trigger.
    void trigger(std::uint32_t id, const SoundOptions& options, bool keyDown) noexcept;
    void stop(std::uint32_t id) noexcept;
    void stopAll() noexcept;

    /// Renders the next block. `all` gets every sound (virtual microphone),
    /// `monitor` every sound except "mute for me" ones. Both are overwritten.
    void process(std::span<float> all, std::span<float> monitor) noexcept;

    /// Gain to apply to the microphone voice (ducked while a "mute voice"
    /// sound plays), smoothed.
    [[nodiscard]] float voiceDuck() const noexcept { return voiceDuck_; }
    [[nodiscard]] std::size_t activeVoices() const noexcept;
    [[nodiscard]] bool isPlaying(std::uint32_t id) const noexcept;

    /// Ids of sounds that finished since the last call (up to 16).
    std::span<const std::uint32_t> takeFinished() noexcept;

private:
    enum class State { Idle, Playing, Paused, Stopping };
    struct Voice {
        State state = State::Idle;
        std::uint32_t id = 0;
        const Clip* clip = nullptr;
        std::size_t position = 0;
        float fade = 0.0F;
        float gain = 1.0F;
        float duck = 1.0F;
        SoundOptions options;
    };

    Voice* findVoice(std::uint32_t id) noexcept;
    Voice* allocateVoice() noexcept;
    void start(std::uint32_t id, const SoundOptions& options) noexcept;
    void finish(Voice& voice) noexcept;

    std::array<const Clip*, kMaxClips> clips_{};
    std::array<Voice, kMaxVoices> voices_{};
    std::array<std::uint32_t, 16> finished_{};
    std::size_t finishedCount_ = 0;
    float fadeStep_ = 0.0F;
    float duckStep_ = 0.0F;
    float voiceDuck_ = 1.0F;
};

} // namespace vox::engine
