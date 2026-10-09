#pragma once

#include "vox/devices/audio_backend.hpp"

#include <atomic>
#include <map>
#include <mutex>
#include <optional>

namespace vox::devices {

/// Deterministic backend for tests. Devices are declared up front; callbacks
/// run only when the test calls pumpCapture() or pumpPlayback(), and failures
/// (open errors, disconnects) are injected explicitly.
class FakeBackend final : public AudioBackend {
public:
    void addDevice(DeviceInfo info);
    void removeDevice(const std::string& id);
    /// Makes `id` the system default of its kind, as when the user or an
    /// installer changes it in the system settings.
    void setDefault(DeviceKind kind, const std::string& id);
    /// The next open of `deviceId` fails with `error`.
    void failNextOpen(const std::string& deviceId, Error error);

    [[nodiscard]] std::string name() const override { return "Fake"; }
    [[nodiscard]] Result<std::vector<DeviceInfo>> enumerate(DeviceKind kind) override;
    [[nodiscard]] Result<std::unique_ptr<Stream>> openCapture(const StreamConfig& config,
                                                              CaptureHandler& handler) override;
    [[nodiscard]] Result<std::unique_ptr<Stream>> openPlayback(const StreamConfig& config,
                                                               PlaybackHandler& handler) override;
    void setEventCallback(EventCallback callback) override;

    /// Delivers interleaved input to the started capture stream of `deviceId`.
    /// Returns false if no such stream is running. Like a real device thread,
    /// pumps of different streams may run concurrently from different threads.
    bool pumpCapture(const std::string& deviceId, const std::vector<float>& interleaved);
    /// Requests `frames` from the started playback stream of `deviceId`.
    [[nodiscard]] std::optional<std::vector<float>> pumpPlayback(const std::string& deviceId,
                                                                 std::size_t frames);
    /// Simulates the device disappearing: streams stop and an event fires.
    void disconnect(const std::string& deviceId);

    [[nodiscard]] bool isRunning(const std::string& deviceId) const;

    /// Switches now() to simulated time, for tests that pump faster or
    /// slower than real time. Until the first call it is the real clock.
    void setTime(double seconds) noexcept;
    [[nodiscard]] double now() const noexcept override;

private:
    struct OpenStream {
        StreamInfo info;
        CaptureHandler* capture = nullptr;
        PlaybackHandler* playback = nullptr;
        bool running = false;
        /// Held while the stream's callback runs, so stop() can wait for it.
        /// One per stream: callbacks of different streams run concurrently.
        std::mutex* callbackMutex = nullptr;
    };
    class FakeStream;
    friend class FakeStream;

    Result<DeviceInfo> resolve(const StreamConfig& config, DeviceKind kind);
    void notify(const DeviceEvent& event);

    std::atomic<bool> simulatedTime_{false};
    std::atomic<double> time_{0.0};
    mutable std::mutex mutex_;
    std::vector<DeviceInfo> devices_;
    std::map<std::string, Error> failures_;
    std::map<std::string, OpenStream*> streams_;
    std::mutex eventMutex_; ///< Held while the event callback runs or is replaced.
    EventCallback callback_;
};

} // namespace vox::devices
