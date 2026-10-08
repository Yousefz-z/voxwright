#pragma once

#include "audio_controller.hpp"
#include "custom_voice_store.hpp"
#include "designer_controller.hpp"
#include "hotkey_controller.hpp"
#include "hotkeys/global_hotkeys.hpp"
#include "neural_controller.hpp"
#include "notification_model.hpp"
#include "platform/autostart.hpp"
#include "settings.hpp"
#include "soundboard_controller.hpp"
#include "speech_controller.hpp"
#include "system_controller.hpp"
#include "tray_controller.hpp"
#include "virtual_mic_check.hpp"
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
    Q_PROPERTY(vox::app::SoundboardController* soundboard READ soundboard CONSTANT)
    Q_PROPERTY(vox::app::HotkeyController* hotkeys READ hotkeys CONSTANT)
    Q_PROPERTY(vox::app::DesignerController* designer READ designer CONSTANT)
    Q_PROPERTY(vox::app::SystemController* system READ system CONSTANT)
    Q_PROPERTY(vox::app::TrayController* tray READ tray CONSTANT)
    Q_PROPERTY(vox::app::SpeechController* speech READ speech CONSTANT)
    Q_PROPERTY(vox::app::VirtualMicCheck* micCheck READ micCheck CONSTANT)
    Q_PROPERTY(vox::app::NeuralController* neural READ neural CONSTANT)
    Q_PROPERTY(vox::app::NotificationModel* notifications READ notifications CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    struct Options {
        std::unique_ptr<devices::AudioBackend> backend;
        /// Folder for settings, soundboards, and imported sounds; empty for
        /// the platform's per-user folders.
        QString dataDir;
        /// Global hotkeys; null for this platform's implementation.
        std::unique_ptr<GlobalHotkeys> hotkeys;
        /// Why the system audio backend is missing, if it is; `backend` is
        /// then one without devices and the user sees this message.
        std::optional<Error> backendError;
        /// Ask the OS for microphone access before recording (macOS). Off
        /// only for tests on fake devices, which run outside an app bundle.
        bool checkMicrophonePermission = true;
        /// Start at sign-in; null for this system's own mechanism.
        std::unique_ptr<Autostart> autostart;
        /// QTextToSpeech engine name; empty for the system's default.
        QString speechEngine;
    };

    explicit AppContext(Options options, QObject* parent = nullptr);
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;
    AppContext(AppContext&&) = delete;
    AppContext& operator=(AppContext&&) = delete;
    ~AppContext() override;

    [[nodiscard]] AudioController* audio() { return audio_.get(); }
    [[nodiscard]] VoiceController* voices() { return voices_.get(); }
    [[nodiscard]] SoundboardController* soundboard() { return soundboard_.get(); }
    [[nodiscard]] HotkeyController* hotkeys() { return hotkeyController_.get(); }
    [[nodiscard]] DesignerController* designer() { return designer_.get(); }
    [[nodiscard]] SystemController* system() { return system_.get(); }
    [[nodiscard]] TrayController* tray() { return tray_.get(); }
    [[nodiscard]] SpeechController* speech() { return speech_.get(); }
    [[nodiscard]] VirtualMicCheck* micCheck() { return micCheck_.get(); }
    [[nodiscard]] NeuralController* neural() { return neural_.get(); }
    /// The effects voices are built from: the built-in ones, plus the
    /// neural voice in VOX_ENABLE_ML builds.
    [[nodiscard]] const plugins::EffectRegistry& registry() const { return registry_; }
    [[nodiscard]] GlobalHotkeys& globalHotkeys() { return *globalHotkeys_; }
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
    void runHotkeyAction(HotkeyController::Action action);
    /// Puts every setting back to its default (the user's own voices and
    /// soundboards are separate files and stay) and offers a restart.
    void resetSettings();

    SettingsStore store_;
    AppSettings settings_;
    NotificationModel notifications_;
    std::unique_ptr<devices::AudioBackend> backend_;
    std::unique_ptr<engine::AudioEngine> engine_;
    std::unique_ptr<NeuralController> neural_;
    plugins::EffectRegistry registry_;
    std::vector<plugins::VoicePreset> presets_;
    CustomVoiceStore voiceStore_;
    std::unique_ptr<AudioController> audio_;
    std::unique_ptr<VoiceController> voices_;
    std::unique_ptr<GlobalHotkeys> globalHotkeys_;
    std::unique_ptr<HotkeyController> hotkeyController_;
    std::unique_ptr<SoundboardController> soundboard_;
    std::unique_ptr<DesignerController> designer_;
    std::unique_ptr<Autostart> autostart_;
    std::unique_ptr<SystemController> system_;
    std::unique_ptr<TrayController> tray_;
    std::unique_ptr<SpeechController> speech_;
    std::unique_ptr<VirtualMicCheck> micCheck_;
    QTimer saveTimer_;
};

} // namespace vox::app
