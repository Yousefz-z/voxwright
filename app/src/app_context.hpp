#pragma once

#include "audio_controller.hpp"
#include "notification_model.hpp"
#include "settings.hpp"
#include "voice_controller.hpp"

#include <vox/devices/audio_backend.hpp>
#include <vox/engine/audio_engine.hpp>
#include <vox/plugins/voice_preset.hpp>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>
#include <vector>

namespace vox::app {

/// Owns everything behind the UI and wires it together. QML receives it as
/// the window's `app` property. Tests build it on a FakeBackend.
class AppContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application")
    Q_PROPERTY(vox::app::AudioController* audio READ audio CONSTANT)
    Q_PROPERTY(vox::app::VoiceController* voices READ voices CONSTANT)
    Q_PROPERTY(vox::app::NotificationModel* notifications READ notifications CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    struct Options {
        std::unique_ptr<devices::AudioBackend> backend;
        QString settingsPath;
        /// Why the system audio backend is missing, if it is; `backend` is
        /// then one without devices and the user sees this message.
        std::optional<Error> backendError;
        /// Ask the OS for microphone access before recording (macOS). Off
        /// only for tests on fake devices, which run outside an app bundle.
        bool checkMicrophonePermission = true;
    };

    explicit AppContext(Options options, QObject* parent = nullptr);
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;
    ~AppContext() override;

    [[nodiscard]] AudioController* audio() { return audio_.get(); }
    [[nodiscard]] VoiceController* voices() { return voices_.get(); }
    [[nodiscard]] NotificationModel* notifications() { return &notifications_; }
    [[nodiscard]] static QString version();

    [[nodiscard]] devices::AudioBackend& backend() { return *backend_; }
    [[nodiscard]] engine::AudioEngine& engine() { return *engine_; }
    [[nodiscard]] const AppSettings& settings() const { return settings_; }

    /// Saves now (normally saving waits 500 ms after the last change).
    [[nodiscard]] Status saveNow();

    /// Runs a notification's action. Actions the UI handles itself (such as
    /// navigation) are forwarded through uiActionRequested.
    Q_INVOKABLE void runAction(const QString& action);

signals:
    void uiActionRequested(const QString& action);

private:
    void scheduleSave();

    SettingsStore store_;
    AppSettings settings_;
    NotificationModel notifications_;
    std::unique_ptr<devices::AudioBackend> backend_;
    std::unique_ptr<engine::AudioEngine> engine_;
    std::vector<plugins::VoicePreset> presets_;
    std::unique_ptr<AudioController> audio_;
    std::unique_ptr<VoiceController> voices_;
    QTimer saveTimer_;
};

} // namespace vox::app
