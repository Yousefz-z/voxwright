#include "vox/devices/fake_backend.hpp"

#include <algorithm>

namespace vox::devices {

class FakeBackend::FakeStream final : public Stream {
public:
    FakeStream(FakeBackend& backend, OpenStream state)
        : backend_(backend)
        , state_(std::move(state)) {
        state_.callbackMutex = &callbackMutex_;
        const std::scoped_lock lock(backend_.mutex_);
        backend_.streams_[state_.info.deviceId] = &state_;
    }
    FakeStream(const FakeStream&) = delete;
    FakeStream& operator=(const FakeStream&) = delete;
    FakeStream(FakeStream&&) = delete;
    FakeStream& operator=(FakeStream&&) = delete;
    ~FakeStream() override {
        {
            const std::scoped_lock lock(backend_.mutex_);
            const auto it = backend_.streams_.find(state_.info.deviceId);
            if (it != backend_.streams_.end() && it->second == &state_) {
                backend_.streams_.erase(it);
            }
        }
        waitForCallback();
    }

    [[nodiscard]] Status start() override {
        const std::scoped_lock lock(backend_.mutex_);
        state_.running = true;
        return {};
    }
    void stop() noexcept override {
        {
            const std::scoped_lock lock(backend_.mutex_);
            state_.running = false;
        }
        waitForCallback();
    }
    [[nodiscard]] const StreamInfo& info() const noexcept override { return state_.info; }

private:
    // A pump that passed the running check holds callbackMutex_ until its
    // callback returns; taking it here waits for that callback.
    void waitForCallback() noexcept { const std::scoped_lock lock(callbackMutex_); }

    FakeBackend& backend_;
    OpenStream state_;
    std::mutex callbackMutex_;
};

void FakeBackend::addDevice(DeviceInfo info) {
    {
        const std::scoped_lock lock(mutex_);
        info.isVirtualCable = info.isVirtualCable || looksLikeVirtualCable(info.name);
        devices_.push_back(std::move(info));
    }
    notify({DeviceEventKind::DeviceListChanged, {}});
}

void FakeBackend::removeDevice(const std::string& id) {
    {
        const std::scoped_lock lock(mutex_);
        std::erase_if(devices_, [&id](const DeviceInfo& d) { return d.id == id; });
    }
    notify({DeviceEventKind::DeviceListChanged, id});
}

void FakeBackend::failNextOpen(const std::string& deviceId, Error error) {
    const std::scoped_lock lock(mutex_);
    failures_.insert_or_assign(deviceId, std::move(error));
}

Result<std::vector<DeviceInfo>> FakeBackend::enumerate(DeviceKind kind) {
    const std::scoped_lock lock(mutex_);
    std::vector<DeviceInfo> out;
    std::copy_if(devices_.begin(), devices_.end(), std::back_inserter(out),
                 [kind](const DeviceInfo& d) { return d.kind == kind; });
    return out;
}

Result<DeviceInfo> FakeBackend::resolve(const StreamConfig& config, DeviceKind kind) {
    const std::scoped_lock lock(mutex_);
    if (const auto failure = failures_.find(config.deviceId); failure != failures_.end()) {
        Error error = failure->second;
        failures_.erase(failure);
        return error;
    }
    for (const DeviceInfo& d : devices_) {
        if (d.kind == kind && (config.deviceId.empty() ? d.isDefault : d.id == config.deviceId)) {
            return d;
        }
    }
    return makeError(ErrorCode::DeviceNotFound,
                     "Device \"" + config.deviceId + "\" is not connected.");
}

Result<std::unique_ptr<Stream>> FakeBackend::openCapture(const StreamConfig& config,
                                                         CaptureHandler& handler) {
    auto device = resolve(config, DeviceKind::Capture);
    if (!device) {
        return device.error();
    }
    OpenStream state;
    state.info = {device.value().id,
                  device.value().name,
                  config.sampleRate != 0 ? config.sampleRate : device.value().nativeSampleRate,
                  config.channels != 0 ? config.channels : device.value().nativeChannels,
                  config.periodFrames != 0 ? config.periodFrames : 128U,
                  config.exclusive};
    state.capture = &handler;
    return std::unique_ptr<Stream>(std::make_unique<FakeStream>(*this, std::move(state)));
}

Result<std::unique_ptr<Stream>> FakeBackend::openPlayback(const StreamConfig& config,
                                                          PlaybackHandler& handler) {
    auto device = resolve(config, DeviceKind::Playback);
    if (!device) {
        return device.error();
    }
    OpenStream state;
    state.info = {device.value().id,
                  device.value().name,
                  config.sampleRate != 0 ? config.sampleRate : device.value().nativeSampleRate,
                  config.channels != 0 ? config.channels : device.value().nativeChannels,
                  config.periodFrames != 0 ? config.periodFrames : 128U,
                  config.exclusive};
    state.playback = &handler;
    return std::unique_ptr<Stream>(std::make_unique<FakeStream>(*this, std::move(state)));
}

void FakeBackend::setEventCallback(EventCallback callback) {
    // Same guarantee as the real backend: once this returns, the previous
    // callback is neither running nor called again.
    const std::scoped_lock lock(eventMutex_);
    callback_ = std::move(callback);
}

void FakeBackend::notify(const DeviceEvent& event) {
    const std::scoped_lock lock(eventMutex_);
    if (callback_) {
        callback_(event);
    }
}

bool FakeBackend::pumpCapture(const std::string& deviceId, const std::vector<float>& interleaved) {
    std::unique_lock<std::mutex> callbackLock;
    CaptureHandler* handler = nullptr;
    std::uint32_t channels = 1;
    {
        const std::scoped_lock lock(mutex_);
        const auto it = streams_.find(deviceId);
        if (it == streams_.end() || !it->second->running || it->second->capture == nullptr) {
            return false;
        }
        // Lock order: backend mutex, then the stream's callback mutex.
        callbackLock = std::unique_lock<std::mutex>(*it->second->callbackMutex);
        handler = it->second->capture;
        channels = std::max<std::uint32_t>(1, it->second->info.channels);
    }
    handler->onCapture(interleaved.data(), interleaved.size() / channels, channels);
    return true;
}

std::optional<std::vector<float>> FakeBackend::pumpPlayback(const std::string& deviceId,
                                                            std::size_t frames) {
    std::unique_lock<std::mutex> callbackLock;
    PlaybackHandler* handler = nullptr;
    std::uint32_t channels = 1;
    {
        const std::scoped_lock lock(mutex_);
        const auto it = streams_.find(deviceId);
        if (it == streams_.end() || !it->second->running || it->second->playback == nullptr) {
            return std::nullopt;
        }
        callbackLock = std::unique_lock<std::mutex>(*it->second->callbackMutex);
        handler = it->second->playback;
        channels = std::max<std::uint32_t>(1, it->second->info.channels);
    }
    std::vector<float> out(frames * channels, 0.0F);
    handler->onPlayback(out.data(), frames, channels);
    return out;
}

void FakeBackend::disconnect(const std::string& deviceId) {
    {
        const std::scoped_lock lock(mutex_);
        if (const auto it = streams_.find(deviceId); it != streams_.end()) {
            it->second->running = false;
        }
        std::erase_if(devices_, [&deviceId](const DeviceInfo& d) { return d.id == deviceId; });
    }
    notify({DeviceEventKind::StreamStopped, deviceId});
    notify({DeviceEventKind::DeviceListChanged, deviceId});
}

void FakeBackend::setTime(double seconds) noexcept {
    time_.store(seconds);
    simulatedTime_.store(true);
}

double FakeBackend::now() const noexcept {
    return simulatedTime_.load() ? time_.load() : AudioBackend::now();
}

bool FakeBackend::isRunning(const std::string& deviceId) const {
    const std::scoped_lock lock(mutex_);
    const auto it = streams_.find(deviceId);
    return it != streams_.end() && it->second->running;
}

} // namespace vox::devices
