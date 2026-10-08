#pragma once

#include <cstdint>
#include <string>

namespace vox::engine {

inline constexpr double kEngineRate = 48000.0;

enum class TransmitMode {
    AlwaysOn,   ///< Voice is sent unless muted.
    PushToTalk, ///< Voice is sent only while the talk key is held.
    PushToMute, ///< Voice is sent except while the talk key is held.
};

/// What a sound does when its trigger (button or hotkey) fires.
enum class PlayMode {
    Restart, ///< Plays; pressing again restarts from the beginning.
    Toggle,  ///< Plays; pressing again stops.
    Pause,   ///< Plays; pressing again pauses, then resumes.
    Overlap, ///< Every press starts another copy.
    Hold,    ///< Loops while the key is held, stops on release.
};

struct SoundOptions {
    PlayMode mode = PlayMode::Restart;
    bool loop = false;
    bool muteOthers = false; ///< Duck other sounds while this one plays.
    bool stopOthers = false; ///< Stop other sounds when this one starts.
    bool muteVoice = false;  ///< Duck the microphone voice while this one plays.
    bool muteForMe = false;  ///< Send to the virtual microphone but not to the monitor.
    float gainDb = 0.0F;
};

struct MixLevels {
    float voiceDb = 0.0F;   ///< Processed voice into the virtual microphone and monitor.
    float soundsDb = -6.0F; ///< Soundboard.
    float speechDb = -3.0F; ///< Text to speech.
    float monitorDb = 0.0F; ///< Overall headphone level.
};

struct DeviceSelection {
    bool useInput = true;
    std::string inputId; ///< Empty = system default.
    bool useMonitor = true;
    std::string monitorId;
    bool useVirtualMic = true;
    std::string virtualMicId;
};

struct EngineConfig {
    std::uint32_t periodFrames = 128; ///< Requested device buffer size.
    bool exclusive = false;
    float minVoiceHz = 75.0F;     ///< Sets PSOLA latency (two periods).
    std::uint32_t maxBlock = 512; ///< Largest internal processing block.
};

/// Which stream an event refers to (EngineEvent::value for device events).
enum class DeviceRole : std::uint32_t {
    Input = 0,      ///< The microphone.
    VirtualMic = 1, ///< The output that feeds the virtual microphone.
    Monitor = 2,    ///< Headphones.
};

enum class EngineEventKind {
    DeviceLost,       ///< value = DeviceRole, detail = device name. The engine keeps
                      ///< running on the other devices.
    DeviceRestored,   ///< value = DeviceRole. The lost device came back and was reopened.
    RestartFailed,    ///< Reopening the devices failed; detail = the error message.
                      ///< The engine is stopped until start() is called again.
    DevicesChanged,   ///< Devices were added or removed; refresh device lists.
    FeedbackDetected, ///< Howl detected; hear-myself was switched off. value = Hz.
    SoundFinished,    ///< value = sound id.
    SpeechFinished,
};

struct EngineEvent {
    EngineEventKind kind = EngineEventKind::SoundFinished;
    std::uint32_t value = 0;
    std::string detail;
};

/// Where the delay from the microphone to one output comes from, in
/// milliseconds. Covers everything the engine controls; buffering inside
/// drivers and the OS comes on top and needs a hardware loopback to measure.
struct LatencyBreakdown {
    double captureMs = 0.0;          ///< One capture period.
    double inputResamplerMs = 0.0;   ///< Only when the microphone is not at 48 kHz.
    double noiseSuppressionMs = 0.0; ///< 20 ms while noise reduction is on.
    double voiceMs = 0.0;            ///< The active voice's algorithmic latency.
    double limiterMs = 0.0;          ///< Output limiter look-ahead.
    double bufferMs = 0.0;           ///< Ring buffer target (scheduling margin).
    double outputResamplerMs = 0.0;  ///< Device-rate conversion and drift trim.
    double playbackMs = 0.0;         ///< One playback period.

    [[nodiscard]] double totalMs() const noexcept {
        return captureMs + inputResamplerMs + noiseSuppressionMs + voiceMs + limiterMs + bufferMs +
               outputResamplerMs + playbackMs;
    }
};

/// Snapshot of meters and counters for the UI (read with relaxed atomics).
struct EngineStats {
    float inputPeak = 0.0F;
    float inputRms = 0.0F;
    float outputPeak = 0.0F;
    bool gateOpen = false;
    bool transmitting = false;
    std::uint64_t underruns = 0;
    std::uint64_t overruns = 0;
    LatencyBreakdown virtualMicLatency; ///< Zero when no virtual mic output is open.
    LatencyBreakdown monitorLatency;    ///< Zero when no headphones are open.
    double estimatedLatencyMs = 0.0;    ///< virtualMicLatency.totalMs()
    double monitorLatencyMs = 0.0;      ///< monitorLatency.totalMs()
    double processingLoad = 0.0;        ///< Fraction of real time spent processing.
};

} // namespace vox::engine
