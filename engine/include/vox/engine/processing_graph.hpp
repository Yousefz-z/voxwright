#pragma once

#include "vox/engine/soundboard_player.hpp"
#include "vox/engine/transmit_control.hpp"
#include "vox/engine/types.hpp"

#include <vox/core/object_channel.hpp>
#include <vox/core/spsc_queue.hpp>
#include <vox/dsp/dynamics.hpp>
#include <vox/dsp/meters.hpp>
#include <vox/dsp/noise_suppressor.hpp>
#include <vox/dsp/one_pole.hpp>
#include <vox/dsp/smoothed_value.hpp>
#include <vox/plugins/voice_chain.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace vox::engine {

/// A command from the control thread to the audio thread. Plain data so the
/// queue never allocates.
struct Command {
    enum class Type : std::uint8_t {
        VoiceParameter,   ///< a = block, b = param, x = value
        VoiceTone,        ///< x = bass dB, y = treble dB
        VoiceEnabled,     ///< a = 0/1
        Background,       ///< a = 0/1
        BlockBypass,      ///< a = block, b = 0/1
        InputGain,        ///< x = dB
        NoiseSuppression, ///< a = 0/1, x = strength
        Gate,             ///< a = 0/1, x = threshold dB
        HearMyself,       ///< a = 0/1
        FeedbackGuard,    ///< a = 0/1
        Mix,              ///< x = voice, y = sounds, z = speech, w = monitor (dB)
        TriggerSound,     ///< a = sound id, b = key down, options
        StopSound,        ///< a = sound id
        StopAllSounds,
        StopSpeech,
    };
    Type type = Type::StopAllSounds;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
    SoundOptions options;
};

/// A sound slot change. The audio thread moves `clip` into the slot (a null
/// clip empties it) and puts the clip it replaced into `replaced`, then hands
/// the record back so both are destroyed on the control thread.
struct ClipInstall {
    std::uint32_t id = 0;
    std::unique_ptr<Clip> clip;
    std::unique_ptr<const Clip> replaced;
};

/// Speech queued for playback.
struct SpeechClip {
    std::vector<float> samples;
    bool throughVoice = false;
    std::size_t position = 0;
};

/// The whole real-time signal path at the engine rate, independent of any
/// device: tests drive it directly with buffers. One thread at a time may
/// call process(); all set* methods queue commands from one control thread.
class ProcessingGraph {
public:
    explicit ProcessingGraph(const EngineConfig& config);
    ProcessingGraph(const ProcessingGraph&) = delete;
    ProcessingGraph& operator=(const ProcessingGraph&) = delete;
    ProcessingGraph(ProcessingGraph&&) = delete;
    ProcessingGraph& operator=(ProcessingGraph&&) = delete;
    ~ProcessingGraph();

    // ------------------------------------------------ control thread
    /// Hands a prepared chain to the audio thread (crossfaded in over 20 ms).
    /// Returns the chain back if the hand-off queue is full.
    std::unique_ptr<plugins::VoiceChain> setVoiceChain(std::unique_ptr<plugins::VoiceChain> chain);
    /// False if the command queue is full (the command is dropped).
    bool send(const Command& command) noexcept;
    std::unique_ptr<ClipInstall> installClip(std::unique_ptr<ClipInstall> install);
    std::unique_ptr<SpeechClip> playSpeech(std::unique_ptr<SpeechClip> speech);
    /// Frees objects the audio thread has finished with.
    void collectGarbage();
    /// Applies queued commands and objects while no audio thread is running,
    /// so changes made while the engine is stopped neither pile up nor get lost.
    void applyWhileStopped() noexcept;

    TransmitControl& transmit() noexcept { return transmit_; }

    // ------------------------------------------------ audio thread
    /// Processes one block. `input` may be empty (no microphone).
    void process(std::span<const float> input, std::span<float> virtualMic,
                 std::span<float> monitor) noexcept;

    // ------------------------------------------------ any thread
    [[nodiscard]] EngineStats meters() const noexcept;
    [[nodiscard]] std::size_t voiceLatencySamples() const noexcept {
        return voiceLatency_.load(std::memory_order_relaxed);
    }
    /// Noise suppression (while on) plus the limiter look-ahead.
    [[nodiscard]] std::size_t fixedLatencySamples() const noexcept;
    [[nodiscard]] std::size_t suppressorLatencySamples() const noexcept;
    [[nodiscard]] std::size_t limiterLatencySamples() const noexcept {
        return micLimiter_.latencySamples();
    }
    /// Events from the audio thread (feedback, sounds finished).
    bool popEvent(EngineEvent& event) noexcept;
    [[nodiscard]] std::uint32_t maxBlock() const noexcept { return maxBlock_; }

private:
    struct PendingEvent {
        EngineEventKind kind = EngineEventKind::SoundFinished;
        std::uint32_t value = 0;
    };

    void applyCommands() noexcept;
    void apply(const Command& c) noexcept;
    void adoptObjects() noexcept;
    void adoptChain(plugins::VoiceChain* chain) noexcept;
    void retire(plugins::VoiceChain* chain) noexcept;
    void processBlock(std::span<const float> input, std::span<float> virtualMic,
                      std::span<float> monitor) noexcept;
    void conditionInput(std::span<const float> input, std::span<float> in) noexcept;
    /// Fades the microphone out while speech plays through the voice.
    void pauseMicForSpeech(std::span<float> in) noexcept;
    void renderSpeech(std::span<float> in, std::span<float> speech) noexcept;
    void renderVoice(std::span<const float> in, std::span<float> voice) noexcept;
    void publishMeters() noexcept;
    void pushEvent(EngineEventKind kind, std::uint32_t value) noexcept;

    EngineConfig config_;
    std::uint32_t maxBlock_;

    // Control to audio.
    SpscQueue<Command, 1024> commands_;
    ObjectChannel<plugins::VoiceChain, 16> chains_;
    ObjectChannel<ClipInstall, 64> clips_;
    ObjectChannel<SpeechClip, 16> speechQueue_;
    // Audio to control.
    SpscQueue<PendingEvent, 256> events_;
    // Objects whose retirement failed because the return queue was full.
    std::array<plugins::VoiceChain*, 8> pendingChains_{};

    // Input conditioning.
    dsp::SmoothedValue inputGain_;
    dsp::DcBlocker dcBlocker_;
    dsp::NoiseSuppressor suppressor_;
    bool suppressorOn_ = false;
    float suppressorMix_ = 0.0F;
    float suppressorStrength_ = 1.0F;
    std::atomic<bool> suppressorActive_{false};
    dsp::NoiseGate gate_;
    bool gateOn_ = false;
    TransmitControl transmit_;
    dsp::FeedbackDetector feedback_;
    bool feedbackGuard_ = true;

    // Voice.
    plugins::VoiceChain* current_ = nullptr;
    plugins::VoiceChain* next_ = nullptr;
    float crossfade_ = 0.0F;
    float crossfadeStep_ = 0.0F;
    bool voiceEnabled_ = true;
    float voiceWet_ = 1.0F;

    // Sources.
    SoundboardPlayer sounds_;
    float duck_ = 1.0F;
    SpeechClip* speechNow_ = nullptr;
    /// Speech through the voice pauses the microphone and puts the voice in
    /// the headphones; frames left of that, which outlast the clip by the
    /// voice chain's delay.
    std::size_t speechVoiceHold_ = 0;
    float micGain_ = 1.0F;         ///< 0 while speech plays through the voice.
    float speechVoiceGain_ = 0.0F; ///< 1 while speech plays through the voice.

    // Mix.
    bool hearMyself_ = false;
    float hearGain_ = 0.0F;
    bool soundsInMonitor_ = true;
    float soundsMonitorGain_ = 1.0F;
    dsp::SmoothedValue voiceLevel_;
    dsp::SmoothedValue soundsLevel_;
    dsp::SmoothedValue speechLevel_;
    dsp::SmoothedValue monitorLevel_;
    dsp::Limiter micLimiter_;
    dsp::Limiter monitorLimiter_;

    // Scratch (allocated once).
    std::vector<float> in_;
    std::vector<float> dry_;
    std::vector<float> voiceA_;
    std::vector<float> voiceB_;
    std::vector<float> soundsAll_;
    std::vector<float> soundsMonitor_;
    std::vector<float> speechBuffer_;

    // Meters.
    dsp::LevelMeter inputMeter_;
    dsp::LevelMeter outputMeter_;
    std::atomic<float> inputPeak_{0.0F};
    std::atomic<float> inputRms_{0.0F};
    std::atomic<float> outputPeak_{0.0F};
    std::atomic<bool> gateOpen_{false};
    std::atomic<bool> transmitting_{false};
    std::atomic<std::size_t> voiceLatency_{0};
};

} // namespace vox::engine
