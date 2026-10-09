#pragma once

#include "app_context.hpp"

#include <vox/devices/fake_backend.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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
        /// The cable's recording side gets what is played into the cable.
        bool cableLoopback = false;
        /// The recording side exists (implied by cableLoopback).
        bool cableRecordingSide = false;
        /// "mock" keeps tests independent of the system's speech engines.
        QString speechEngine = QStringLiteral("mock");
        /// The name of the cable's playback side.
        std::string cableName = "CABLE Input (VB-Audio Virtual Cable)";
        /// The cable's sides are the system defaults, as after VB-CABLE's
        /// installer made them so.
        bool cableIsDefault = false;
    };

    TestApp()
        : TestApp(Devices{.cable = true}) {}

    /// `files` are written into the data folder first (path relative to it).
    explicit TestApp(const Devices& devices, const QString& settingsJson = {},
                     const QHash<QString, QByteArray>& files = {}) {
        auto backend = std::make_unique<devices::FakeBackend>();
        backend->setTime(0.0);
        const bool cableDefault = devices.cable && devices.cableIsDefault;
        backend->addDevice(
            {"mic", "Studio Microphone", DeviceKind::Capture, !cableDefault, 48000, 1, false});
        backend->addDevice(
            {"phones", "Headphones", DeviceKind::Playback, !cableDefault, 48000, 2, false});
        if (devices.cable) {
            backend->addDevice(
                {"cable", devices.cableName, DeviceKind::Playback, cableDefault, 48000, 2, false});
            if (devices.cableLoopback || devices.cableRecordingSide) {
                backend->addDevice({"cable-out", "CABLE Output (VB-Audio Virtual Cable)",
                                    DeviceKind::Capture, cableDefault, 48000, 2, false});
            }
        }
        loopback_ = devices.cableLoopback;
        backend_ = backend.get();
        // The setup guide would cover the window in UI tests; tests that
        // want it pass their own settings.
        const QString json =
            settingsJson.isEmpty()
                ? QStringLiteral(R"({"version": 1, "app": {"firstRunDone": true}})")
                : settingsJson;
        {
            QFile file(settingsPath());
            if (file.open(QIODevice::WriteOnly)) {
                file.write(json.toUtf8());
            }
        }
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString path = dir_.filePath(it.key());
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(it.value());
            }
        }
        auto hotkeys = std::make_unique<ManualHotkeys>(true);
        hotkeys_ = hotkeys.get();
        AppContext::Options options;
        options.backend = std::move(backend);
        options.dataDir = dir_.path();
        options.hotkeys = std::move(hotkeys);
        options.checkMicrophonePermission = false; // fake devices, no app bundle
        options.autostart = makeXdgAutostart(dir_.filePath(QStringLiteral("autostart")));
        options.speechEngine = devices.speechEngine;
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
        run(seconds, input, nullptr);
    }

    /// Like pump(), returning what reached the virtual cable (left channel).
    std::vector<float> record(double seconds, const std::vector<float>& input) {
        std::vector<float> out;
        run(seconds, input, &out);
        return out;
    }

private:
    void run(double seconds, const std::vector<float>& input, std::vector<float>* cable) {
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
            const auto played = backend_->pumpPlayback("cable", kBlock);
            if (played.has_value() && (cable != nullptr || loopback_)) {
                const std::vector<float>& frames = played.value();
                std::vector<float> left(frames.size() / 2);
                for (std::size_t i = 0; i < left.size(); ++i) {
                    left[i] = frames[2 * i];
                }
                if (cable != nullptr) {
                    cable->insert(cable->end(), left.begin(), left.end());
                }
                if (loopback_) {
                    static_cast<void>(backend_->pumpCapture("cable-out", left));
                }
            }
            static_cast<void>(backend_->pumpPlayback("phones", kBlock));
            time_ += static_cast<double>(kBlock) / 48000.0;
            if (b % 64 == 0) {
                context_->audio()->poll();
            }
        }
        context_->audio()->poll();
        QCoreApplication::processEvents();
    }

    QTemporaryDir dir_;
    devices::FakeBackend* backend_ = nullptr;
    ManualHotkeys* hotkeys_ = nullptr;
    std::unique_ptr<AppContext> context_;
    bool loopback_ = false;
    double time_ = 0.0;
    std::size_t position_ = 0;
};

} // namespace vox::app::test
