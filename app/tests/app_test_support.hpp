#pragma once

#include "app_context.hpp"

#include <vox/devices/fake_backend.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

#include <memory>
#include <string>
#include <vector>

namespace vox::app::test {

using devices::DeviceKind;

/// An AppContext on a FakeBackend with a microphone, headphones, and
/// (optionally) a VB-CABLE-style virtual cable, settings in a temp folder.
class TestApp {
public:
    struct Devices {
        bool cable;
    };

    TestApp()
        : TestApp(Devices{.cable = true}) {}

    explicit TestApp(Devices devices, const QString& settingsJson = {}) {
        auto backend = std::make_unique<devices::FakeBackend>();
        backend->setTime(0.0);
        backend->addDevice(
            {"mic", "Studio Microphone", DeviceKind::Capture, true, 48000, 1, false});
        backend->addDevice({"phones", "Headphones", DeviceKind::Playback, true, 48000, 2, false});
        if (devices.cable) {
            backend->addDevice({"cable", "CABLE Input (VB-Audio Virtual Cable)",
                                DeviceKind::Playback, false, 48000, 2, false});
        }
        backend_ = backend.get();
        if (!settingsJson.isEmpty()) {
            QFile file(settingsPath());
            if (file.open(QIODevice::WriteOnly)) {
                file.write(settingsJson.toUtf8());
            }
        }
        auto hotkeys = std::make_unique<ManualHotkeys>(true);
        hotkeys_ = hotkeys.get();
        AppContext::Options options;
        options.backend = std::move(backend);
        options.dataDir = dir_.path();
        options.hotkeys = std::move(hotkeys);
        options.checkMicrophonePermission = false; // fake devices, no app bundle
        context_ = std::make_unique<AppContext>(std::move(options));
    }

    [[nodiscard]] AppContext& context() { return *context_; }
    [[nodiscard]] devices::FakeBackend& backend() { return *backend_; }
    [[nodiscard]] ManualHotkeys& hotkeys() { return *hotkeys_; }
    [[nodiscard]] QString dataDir() const { return dir_.path(); }
    [[nodiscard]] QString settingsPath() const {
        return dir_.filePath(QStringLiteral("settings.json"));
    }

    /// Processes events until every sound has loaded (or `timeoutMs` passed).
    bool waitForSounds(int timeoutMs = 20000) {
        QElapsedTimer timer;
        timer.start();
        while (context_->soundboard()->loadingCount() > 0 && timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        QCoreApplication::processEvents();
        return context_->soundboard()->loadingCount() == 0;
    }

    /// Runs the devices in lock-step for `seconds` of simulated time, feeding
    /// `input` (looped) to the microphone, then lets the controllers poll.
    void pump(double seconds, const std::vector<float>& input = {}) {
        constexpr std::size_t kBlock = 128;
        std::vector<float> mono(kBlock, 0.0F);
        const auto blocks = static_cast<std::size_t>(seconds * 48000.0 / kBlock);
        for (std::size_t b = 0; b < blocks; ++b) {
            backend_->setTime(time_);
            for (std::size_t i = 0; i < kBlock; ++i) {
                mono[i] = input.empty() ? 0.0F : input[(position_ + i) % input.size()];
            }
            position_ += kBlock;
            static_cast<void>(backend_->pumpCapture("mic", mono));
            static_cast<void>(backend_->pumpPlayback("cable", kBlock));
            static_cast<void>(backend_->pumpPlayback("phones", kBlock));
            time_ += static_cast<double>(kBlock) / 48000.0;
            if (b % 64 == 0) {
                context_->audio()->poll();
            }
        }
        context_->audio()->poll();
        QCoreApplication::processEvents();
    }

private:
    QTemporaryDir dir_;
    devices::FakeBackend* backend_ = nullptr;
    ManualHotkeys* hotkeys_ = nullptr;
    std::unique_ptr<AppContext> context_;
    double time_ = 0.0;
    std::size_t position_ = 0;
};

} // namespace vox::app::test
