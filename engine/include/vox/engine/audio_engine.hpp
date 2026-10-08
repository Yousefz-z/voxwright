#pragma once

#include "vox/engine/processing_graph.hpp"
#include "vox/engine/types.hpp"

#include <vox/core/result.hpp>
#include <vox/devices/audio_backend.hpp>
#include <vox/plugins/registry.hpp>
#include <vox/plugins/voice_chain.hpp>
#include <vox/plugins/voice_preset.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace vox::engine {

/// Connects the processing graph to real (or fake) devices.
///
/// Threads: every method except transmit() belongs to one control thread
/// (the UI thread). Audio runs on the device threads. While a
/// microphone is open, its capture callback drives processing ("push mode")
/// and the outputs read from drift-compensated ring buffers. Without a
/// microphone (none selected, or it was unplugged) the virtual microphone's
/// playback callback drives processing instead ("pull mode"), so sounds and
/// speech keep working.
///
/// Device loss is reported through poll(): the engine keeps running on the
/// remaining devices and reopens the lost one when it reappears.
class AudioEngine {
public:
    explicit AudioEngine(devices::AudioBackend& backend, const EngineConfig& config = {});
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;
    ~AudioEngine();

    // ------------------------------------------------ devices
    [[nodiscard]] Result<std::vector<devices::DeviceInfo>> devices(devices::DeviceKind kind) const;
    /// Opens and starts the selected devices. On failure nothing is left
    /// running and the error names the device role and the cause.
    [[nodiscard]] Status start(const DeviceSelection& selection);
    void stop() noexcept;
    [[nodiscard]] bool isRunning() const noexcept { return running_; }
    [[nodiscard]] const DeviceSelection& selection() const noexcept { return selection_; }

    struct ActiveDevices {
        std::optional<devices::StreamInfo> input;
        std::optional<devices::StreamInfo> virtualMic;
        std::optional<devices::StreamInfo> monitor;
        bool pullMode = false; ///< True when an output, not the microphone, drives processing.
    };
    [[nodiscard]] ActiveDevices activeDevices() const;

    // ------------------------------------------------ voice
    /// Builds the voice on this thread and crossfades to it over 20 ms.
    [[nodiscard]] Status setVoice(const plugins::VoicePreset& preset,
                                  const plugins::VoiceSettings& settings,
                                  const plugins::EffectRegistry& registry);
    [[nodiscard]] Status setVoiceParameter(std::size_t block, std::size_t param, float value);
    [[nodiscard]] Status setVoiceTone(float bassDb, float trebleDb);
    /// Off passes the conditioned microphone through unchanged.
    [[nodiscard]] Status setVoiceEnabled(bool enabled);
    [[nodiscard]] Status setBackgroundEnabled(bool enabled);
    [[nodiscard]] Status setBlockBypassed(std::size_t block, bool bypassed);

    // ------------------------------------------------ input and monitoring
    [[nodiscard]] Status setInputGainDb(float db);
    [[nodiscard]] Status setNoiseSuppression(bool enabled, float strength);
    [[nodiscard]] Status setGate(bool enabled, float thresholdDb);
    [[nodiscard]] Status setHearMyself(bool enabled);
    [[nodiscard]] Status setFeedbackGuard(bool enabled);
    [[nodiscard]] Status setMixLevels(const MixLevels& levels);
    /// Mute, push-to-talk, and the censor beep. Safe from any thread.
    TransmitControl& transmit() noexcept { return graph_.transmit(); }

    // ------------------------------------------------ soundboard
    /// `samples` are mono at 48 kHz (see devices::decodeAudioFile).
    [[nodiscard]] Status loadSound(std::uint32_t id, std::vector<float> samples);
    [[nodiscard]] Status unloadSound(std::uint32_t id);
    [[nodiscard]] Status triggerSound(std::uint32_t id, const SoundOptions& options, bool keyDown);
    [[nodiscard]] Status stopSound(std::uint32_t id);
    [[nodiscard]] Status stopAllSounds();

    // ------------------------------------------------ text to speech
    /// `samples` are mono at 48 kHz. Queued behind speech already playing.
    [[nodiscard]] Status playSpeech(std::vector<float> samples, bool throughVoice);
    [[nodiscard]] Status stopSpeech();

    // ------------------------------------------------ housekeeping
    /// Call regularly (the app uses a 30 Hz timer): frees retired objects,
    /// handles device changes, and returns events for the UI.
    std::vector<EngineEvent> poll();
    /// Meters, counters, and the latency estimate.
    [[nodiscard]] EngineStats stats() const;

private:
    class InputPort;
    class OutputPort;
    struct Lost {
        bool input = false;
        bool virtualMic = false;
        bool monitor = false;
        [[nodiscard]] bool any() const noexcept { return input || virtualMic || monitor; }
    };

    [[nodiscard]] Status openStreams(const DeviceSelection& selection);
    void closeStreams() noexcept;
    [[nodiscard]] DeviceSelection effectiveSelection() const;
    [[nodiscard]] Status send(const Command& command);
    void afterSend() noexcept;
    void onDeviceEvent(const devices::DeviceEvent& event);
    void handleDeviceEvents(std::vector<EngineEvent>& out);
    void tryRecover(std::vector<EngineEvent>& out);

    // Audio threads.
    void processCapture(const float* interleaved, std::size_t frames,
                        std::uint32_t channels) noexcept;
    void renderForPull(OutputPort& driver, std::size_t frames, double now) noexcept;
    void runGraph(std::span<const float> input, std::size_t frames, double now) noexcept;

    devices::AudioBackend& backend_;
    EngineConfig config_;
    ProcessingGraph graph_;
    DeviceSelection selection_;
    bool running_ = false;
    Lost lost_;

    std::unique_ptr<InputPort> input_;
    std::unique_ptr<OutputPort> virtualMic_;
    std::unique_ptr<OutputPort> monitor_;
    OutputPort* pullDriver_ = nullptr;

    // Engine-rate buffers, sized when streams open.
    std::vector<float> engineIn_;
    std::vector<float> micOut_;
    std::vector<float> monitorOut_;
    std::size_t engineChunk_ = 0;

    std::atomic<float> load_{0.0F};

    std::mutex deviceEventsMutex_;
    std::vector<devices::DeviceEvent> deviceEvents_;
};

} // namespace vox::engine
