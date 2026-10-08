#pragma once

#include <vox/core/result.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vox::devices {

enum class DeviceKind { Capture, Playback };

struct DeviceInfo {
    std::string id; ///< Backend-specific, stable while the device is connected.
    std::string name;
    DeviceKind kind = DeviceKind::Capture;
    bool isDefault = false;
    std::uint32_t nativeSampleRate = 0; ///< 0 if unknown.
    std::uint32_t nativeChannels = 0;   ///< 0 if unknown.
    bool isVirtualCable = false;        ///< A loopback driver (VB-CABLE, BlackHole, ...).
};

struct StreamConfig {
    std::string deviceId;           ///< Empty selects the system default device.
    std::uint32_t sampleRate = 0;   ///< 0 uses the device's native rate.
    std::uint32_t channels = 0;     ///< 0 uses the device's native channel count.
    std::uint32_t periodFrames = 0; ///< 0 lets the backend choose.
    bool exclusive = false;         ///< WASAPI exclusive mode (Windows only).
};

/// What a stream actually got after opening.
struct StreamInfo {
    std::string deviceId;
    std::string deviceName;
    std::uint32_t sampleRate = 0;
    std::uint32_t channels = 0;
    std::uint32_t periodFrames = 0;
    bool exclusive = false;
};

/// Called on the device's real-time thread with interleaved float samples.
class CaptureHandler {
public:
    CaptureHandler() = default;
    CaptureHandler(const CaptureHandler&) = delete;
    CaptureHandler& operator=(const CaptureHandler&) = delete;
    CaptureHandler(CaptureHandler&&) = delete;
    CaptureHandler& operator=(CaptureHandler&&) = delete;
    virtual ~CaptureHandler() = default;
    virtual void onCapture(const float* interleaved, std::size_t frames,
                           std::uint32_t channels) noexcept = 0;
};

class PlaybackHandler {
public:
    PlaybackHandler() = default;
    PlaybackHandler(const PlaybackHandler&) = delete;
    PlaybackHandler& operator=(const PlaybackHandler&) = delete;
    PlaybackHandler(PlaybackHandler&&) = delete;
    PlaybackHandler& operator=(PlaybackHandler&&) = delete;
    virtual ~PlaybackHandler() = default;
    /// Must fill `frames * channels` interleaved samples.
    virtual void onPlayback(float* interleaved, std::size_t frames,
                            std::uint32_t channels) noexcept = 0;
};

class Stream {
public:
    Stream() = default;
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&&) = delete;
    Stream& operator=(Stream&&) = delete;
    virtual ~Stream() = default;

    [[nodiscard]] virtual Status start() = 0;
    virtual void stop() noexcept = 0;
    [[nodiscard]] virtual const StreamInfo& info() const noexcept = 0;
};

enum class DeviceEventKind {
    DeviceListChanged, ///< A device was added or removed.
    StreamStopped,     ///< An open stream's device disappeared or stopped by itself.
    StreamRerouted,    ///< The OS moved a default-device stream to another device.
};

struct DeviceEvent {
    DeviceEventKind kind = DeviceEventKind::DeviceListChanged;
    std::string deviceId;
};

/// Audio I/O abstraction. The real implementation wraps miniaudio; tests use
/// FakeBackend, which drives the callbacks deterministically.
class AudioBackend {
public:
    using EventCallback = std::function<void(const DeviceEvent&)>;

    AudioBackend() = default;
    AudioBackend(const AudioBackend&) = delete;
    AudioBackend& operator=(const AudioBackend&) = delete;
    AudioBackend(AudioBackend&&) = delete;
    AudioBackend& operator=(AudioBackend&&) = delete;
    virtual ~AudioBackend() = default;

    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual Result<std::vector<DeviceInfo>> enumerate(DeviceKind kind) = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<Stream>> openCapture(const StreamConfig& config,
                                                                      CaptureHandler& handler) = 0;
    [[nodiscard]] virtual Result<std::unique_ptr<Stream>>
    openPlayback(const StreamConfig& config, PlaybackHandler& handler) = 0;
    /// The callback may be invoked from any thread.
    virtual void setEventCallback(EventCallback callback) = 0;
    /// Monotonic time in seconds, the clock the device callbacks run on.
    /// Safe from any thread, including audio callbacks.
    [[nodiscard]] virtual double now() const noexcept;
};

/// True for names of known loopback drivers (VB-CABLE, BlackHole, VoiceMeeter,
/// Loopback, and Voxwright's own driver).
[[nodiscard]] bool looksLikeVirtualCable(const std::string& deviceName);

/// Creates the platform backend (WASAPI on Windows, CoreAudio on macOS,
/// PulseAudio or ALSA on Linux).
[[nodiscard]] Result<std::unique_ptr<AudioBackend>> createSystemBackend();

} // namespace vox::devices
