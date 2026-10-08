#include "miniaudio_errors.hpp"
#include "vox/devices/audio_backend.hpp"

#include <miniaudio.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vox::devices {
namespace {

std::string idToString(const ma_device_id& id) {
    // The id is an opaque union; hex-encode its bytes so it can be stored.
    std::string out;
    out.reserve(sizeof(id) * 2);
    std::array<unsigned char, sizeof(ma_device_id)> bytes{};
    std::memcpy(bytes.data(), &id, sizeof(id));
    std::size_t last = bytes.size();
    while (last > 0 && bytes[last - 1] == 0) {
        --last;
    }
    constexpr std::string_view kHex = "0123456789abcdef";
    for (std::size_t i = 0; i < last; ++i) {
        out.push_back(kHex[bytes[i] >> 4U]);
        out.push_back(kHex[bytes[i] & 0x0FU]);
    }
    return out;
}

class MiniaudioBackend;

class MiniaudioStream final : public Stream {
public:
    MiniaudioStream(MiniaudioBackend& backend, DeviceKind kind, CaptureHandler* capture,
                    PlaybackHandler* playback)
        : backend_(backend)
        , kind_(kind)
        , capture_(capture)
        , playback_(playback) {}
    MiniaudioStream(const MiniaudioStream&) = delete;
    MiniaudioStream& operator=(const MiniaudioStream&) = delete;
    MiniaudioStream(MiniaudioStream&&) = delete;
    MiniaudioStream& operator=(MiniaudioStream&&) = delete;
    ~MiniaudioStream() override {
        if (initialized_) {
            stopRequested_.store(true);
            ma_device_uninit(&device_);
        }
    }

    Status open(ma_context& context, const StreamConfig& config, const ma_device_id* id,
                const std::string& name);

    [[nodiscard]] Status start() override {
        stopRequested_.store(false);
        const ma_result r = ma_device_start(&device_);
        if (r != MA_SUCCESS) {
            return mapOpenError(r, kind_, info_.deviceName, info_.exclusive);
        }
        return {};
    }
    void stop() noexcept override {
        // miniaudio reports intentional stops through the same notification
        // as a lost device; the flag tells them apart.
        stopRequested_.store(true);
        static_cast<void>(ma_device_stop(&device_));
    }
    [[nodiscard]] const StreamInfo& info() const noexcept override { return info_; }

private:
    static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frames);
    static void notificationCallback(const ma_device_notification* notification);

    MiniaudioBackend& backend_;
    DeviceKind kind_;
    CaptureHandler* capture_;
    PlaybackHandler* playback_;
    ma_device device_{};
    bool initialized_ = false;
    std::atomic<bool> stopRequested_{false};
    StreamInfo info_;
};

class MiniaudioBackend final : public AudioBackend {
public:
    Status init() {
        const ma_context_config config = ma_context_config_init();
        const ma_result r = ma_context_init(nullptr, 0, &config, &context_);
        if (r != MA_SUCCESS) {
            return makeError(
                ErrorCode::BackendInitFailed,
                "No audio system could be started. On Windows, check that the Windows Audio "
                "service is running; on macOS, restart the computer if sound does not work in "
                "other apps either.",
                std::string("miniaudio: ") + ma_result_description(r));
        }
        initialized_ = true;
        watcher_ = std::thread([this] { watchDevices(); });
        return {};
    }
    MiniaudioBackend() = default;
    MiniaudioBackend(const MiniaudioBackend&) = delete;
    MiniaudioBackend& operator=(const MiniaudioBackend&) = delete;
    MiniaudioBackend(MiniaudioBackend&&) = delete;
    MiniaudioBackend& operator=(MiniaudioBackend&&) = delete;
    ~MiniaudioBackend() override {
        {
            const std::scoped_lock lock(watchMutex_);
            stopWatching_ = true;
        }
        watchWake_.notify_all();
        if (watcher_.joinable()) {
            watcher_.join();
        }
        if (initialized_) {
            ma_context_uninit(&context_);
        }
    }

    [[nodiscard]] std::string name() const override {
        return ma_get_backend_name(context_.backend);
    }

    [[nodiscard]] Result<std::vector<DeviceInfo>> enumerate(DeviceKind kind) override {
        ma_device_info* playback = nullptr;
        ma_uint32 playbackCount = 0;
        ma_device_info* capture = nullptr;
        ma_uint32 captureCount = 0;
        const std::scoped_lock lock(mutex_);
        const ma_result r =
            ma_context_get_devices(&context_, &playback, &playbackCount, &capture, &captureCount);
        if (r != MA_SUCCESS) {
            return makeError(
                ErrorCode::BackendInitFailed,
                "The list of audio devices could not be read. Reconnect your devices and try "
                "again.",
                std::string("miniaudio: ") + ma_result_description(r));
        }
        std::vector<DeviceInfo> out;
        const ma_device_info* infos = kind == DeviceKind::Capture ? capture : playback;
        const ma_uint32 count = kind == DeviceKind::Capture ? captureCount : playbackCount;
        for (ma_uint32 i = 0; i < count; ++i) {
            DeviceInfo info;
            info.id = idToString(infos[i].id);
            info.name = infos[i].name;
            info.kind = kind;
            info.isDefault = infos[i].isDefault != 0;
            info.isVirtualCable = looksLikeVirtualCable(info.name);
            ids_[info.id] = infos[i].id;
            out.push_back(std::move(info));
        }
        return out;
    }

    [[nodiscard]] Result<std::unique_ptr<Stream>> openCapture(const StreamConfig& config,
                                                              CaptureHandler& handler) override {
        return open(config, DeviceKind::Capture, &handler, nullptr);
    }
    [[nodiscard]] Result<std::unique_ptr<Stream>> openPlayback(const StreamConfig& config,
                                                               PlaybackHandler& handler) override {
        return open(config, DeviceKind::Playback, nullptr, &handler);
    }

    void setEventCallback(EventCallback callback) override {
        // Waits for a running callback, so once this returns the old one
        // is never called again (its owner may be about to be destroyed).
        const std::scoped_lock lock(eventMutex_);
        callback_ = std::move(callback);
    }

    void notify(const DeviceEvent& event) {
        const std::scoped_lock lock(eventMutex_);
        if (callback_) {
            callback_(event);
        }
    }

private:
    Result<std::unique_ptr<Stream>> open(const StreamConfig& config, DeviceKind kind,
                                         CaptureHandler* capture, PlaybackHandler* playback) {
        ma_device_id id{};
        const ma_device_id* idPtr = nullptr;
        std::string name;
        if (!config.deviceId.empty()) {
            auto devices = enumerate(kind);
            if (!devices) {
                return devices.error();
            }
            const std::scoped_lock lock(mutex_);
            const auto it = ids_.find(config.deviceId);
            if (it == ids_.end()) {
                return makeError(
                    ErrorCode::DeviceNotFound,
                    "The selected " + kindWord(kind) +
                        " is not connected. Plug it in or choose another one in Settings > Audio.");
            }
            id = it->second;
            idPtr = &id;
            for (const auto& d : devices.value()) {
                if (d.id == config.deviceId) {
                    name = d.name;
                }
            }
        }
        auto stream = std::make_unique<MiniaudioStream>(*this, kind, capture, playback);
        if (auto opened = stream->open(context_, config, idPtr, name); !opened) {
            return opened.error();
        }
        return std::unique_ptr<Stream>(std::move(stream));
    }

    ma_context context_{};
    bool initialized_ = false;
    std::mutex mutex_;
    std::map<std::string, ma_device_id> ids_;
    std::mutex eventMutex_;
    EventCallback callback_;

    // miniaudio reports events per open stream only, not devices being
    // added or removed, so a watcher compares the device lists every 2 s.
    void watchDevices();
    std::thread watcher_;
    std::mutex watchMutex_;
    std::condition_variable watchWake_;
    bool stopWatching_ = false;
};

Status MiniaudioStream::open(ma_context& context, const StreamConfig& config,
                             const ma_device_id* id, const std::string& name) {
    const bool isCapture = kind_ == DeviceKind::Capture;
    ma_device_config dc =
        ma_device_config_init(isCapture ? ma_device_type_capture : ma_device_type_playback);
    ma_device_config* c = &dc;
    if (isCapture) {
        c->capture.pDeviceID = id;
        c->capture.format = ma_format_f32;
        c->capture.channels = config.channels;
        c->capture.shareMode = config.exclusive ? ma_share_mode_exclusive : ma_share_mode_shared;
    } else {
        c->playback.pDeviceID = id;
        c->playback.format = ma_format_f32;
        c->playback.channels = config.channels;
        c->playback.shareMode = config.exclusive ? ma_share_mode_exclusive : ma_share_mode_shared;
    }
    c->sampleRate = config.sampleRate;
    c->periodSizeInFrames = config.periodFrames;
    c->performanceProfile = ma_performance_profile_low_latency;
    c->noPreSilencedOutputBuffer = MA_FALSE;
    c->noClip = MA_TRUE;
    c->dataCallback = &MiniaudioStream::dataCallback;
    c->notificationCallback = &MiniaudioStream::notificationCallback;
    c->pUserData = this;
    // The engine converts rates itself with a better resampler.
    c->wasapi.noAutoConvertSRC = MA_TRUE;
    c->wasapi.noDefaultQualitySRC = MA_TRUE;
    const ma_result r = ma_device_init(&context, c, &device_);
    if (r != MA_SUCCESS) {
        return mapOpenError(r, kind_, name, config.exclusive);
    }
    initialized_ = true;
    info_.deviceId = config.deviceId;
    info_.deviceName = isCapture ? device_.capture.name : device_.playback.name;
    info_.sampleRate = device_.sampleRate;
    info_.channels = isCapture ? device_.capture.channels : device_.playback.channels;
    info_.periodFrames = isCapture ? device_.capture.internalPeriodSizeInFrames
                                   : device_.playback.internalPeriodSizeInFrames;
    info_.exclusive = config.exclusive;
    return {};
}

void MiniaudioStream::dataCallback(ma_device* device, void* output, const void* input,
                                   ma_uint32 frames) {
    auto* self = static_cast<MiniaudioStream*>(device->pUserData);
    if (self->capture_ != nullptr && input != nullptr) {
        self->capture_->onCapture(static_cast<const float*>(input), frames,
                                  device->capture.channels);
    }
    if (self->playback_ != nullptr && output != nullptr) {
        self->playback_->onPlayback(static_cast<float*>(output), frames, device->playback.channels);
    }
}

void MiniaudioStream::notificationCallback(const ma_device_notification* notification) {
    auto* self = static_cast<MiniaudioStream*>(notification->pDevice->pUserData);
    switch (notification->type) {
    case ma_device_notification_type_stopped:
        if (!self->stopRequested_.load()) {
            self->backend_.notify({DeviceEventKind::StreamStopped, self->info_.deviceId});
        }
        break;
    case ma_device_notification_type_rerouted:
        self->backend_.notify({DeviceEventKind::StreamRerouted, self->info_.deviceId});
        break;
    default:
        break;
    }
}

void MiniaudioBackend::watchDevices() {
    constexpr auto kInterval = std::chrono::seconds(2);
    const auto snapshot = [this] {
        std::vector<std::string> ids;
        for (const DeviceKind kind : {DeviceKind::Capture, DeviceKind::Playback}) {
            if (auto list = enumerate(kind)) {
                for (const auto& d : list.value()) {
                    ids.push_back(d.id + (d.isDefault ? "*" : ""));
                }
            }
        }
        return ids;
    };
    std::vector<std::string> last = snapshot();
    std::unique_lock lock(watchMutex_);
    while (!watchWake_.wait_for(lock, kInterval, [this] { return stopWatching_; })) {
        lock.unlock();
        std::vector<std::string> now = snapshot();
        if (now != last) {
            last = std::move(now);
            notify({DeviceEventKind::DeviceListChanged, {}});
        }
        lock.lock();
    }
}

} // namespace

Result<std::unique_ptr<AudioBackend>> createSystemBackend() {
    auto backend = std::make_unique<MiniaudioBackend>();
    if (auto ok = backend->init(); !ok) {
        return ok.error();
    }
    return std::unique_ptr<AudioBackend>(std::move(backend));
}

} // namespace vox::devices
