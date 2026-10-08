#pragma once

#include "hotkeys/global_hotkeys.hpp"
#include "notification_model.hpp"
#include "settings.hpp"

#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>

namespace vox::engine {
class TransmitControl;
}

namespace vox::app {

/// Global hotkeys: which key does what, conflict checks, and dispatch.
/// System actions and voice hotkeys are saved in AppSettings; sound hotkeys
/// belong to the soundboard, which registers them here.
class HotkeyController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY bindingsChanged)

public:
    enum class Action {
        VoiceChanger,
        HearMyself,
        Mute,
        Talk,
        Background,
        StopSounds,
        NextVoice,
        PreviousVoice,
        RandomVoice,
        Censor,
    };
    Q_ENUM(Action)

    HotkeyController(GlobalHotkeys& keys, engine::TransmitControl& transmit, AppSettings& settings,
                     NotificationModel& notifications, QObject* parent = nullptr);

    /// Registers the saved hotkeys.
    void initialize();

    [[nodiscard]] bool supported() const { return keys_.supported(); }
    /// [{action, label, sequence, display}] for the Hotkeys page.
    [[nodiscard]] QVariantList actions() const;

    /// Assigns `sequence` (portable text, "" to clear) to an action. Returns
    /// "" on success or a message saying why not.
    Q_INVOKABLE QString assignAction(const QString& action, const QString& sequence);
    /// The same for a soundboard sound.
    Q_INVOKABLE QString assignSound(quint32 soundId, const QString& sequence);
    /// The same for a voice: the key switches to it.
    Q_INVOKABLE QString assignVoice(const QString& voiceId, const QString& sequence);
    [[nodiscard]] Q_INVOKABLE QString voiceHotkey(const QString& voiceId) const;
    /// Names voices in conflict messages.
    void setVoiceNamer(std::function<QString(const QString&)> namer) {
        voiceName_ = std::move(namer);
    }
    void forgetSound(quint32 soundId);
    /// Restores saved sound hotkeys at startup; conflicting ones are dropped
    /// with a notification.
    void restoreSoundHotkeys(const QHash<quint32, QString>& hotkeys);
    /// Names sounds in conflict messages.
    void setSoundNamer(std::function<QString(quint32)> namer) { soundName_ = std::move(namer); }
    [[nodiscard]] QString soundHotkey(quint32 soundId) const { return soundKeys_.value(soundId); }

    /// Portable text for a key event, or "" for a lone modifier.
    Q_INVOKABLE static QString sequenceFromKey(int key, int modifiers);
    /// Text to show for a portable sequence (uses the platform's symbols).
    Q_INVOKABLE static QString displayText(const QString& sequence);

signals:
    void bindingsChanged();
    void actionTriggered(vox::app::HotkeyController::Action action);
    void soundTriggered(quint32 soundId, bool pressed);
    void voiceTriggered(const QString& voiceId);

private:
    static constexpr int kVoiceIdBase = 5000;
    static constexpr int kSoundIdBase = 10000;
    [[nodiscard]] static QString actionKey(Action action);
    [[nodiscard]] static QString actionLabel(Action action);
    [[nodiscard]] static int bindingId(Action action) { return static_cast<int>(action) + 1; }
    /// Who already uses `sequence` ("" if nobody), ignoring `except`.
    [[nodiscard]] QString ownerOf(const QString& sequence, const QString& except) const;
    [[nodiscard]] QString validate(const QString& sequence) const;
    /// Checks `sequence` for `owner`, stores it with `store`, and registers
    /// every binding; on failure stores `previous` again. Returns "" or why not.
    QString assignKey(const QString& owner, const QString& sequence, const QString& previous,
                      const std::function<void(const QString&)>& store);
    [[nodiscard]] Status registerAll();
    void onActivated(int id, bool pressed);

    GlobalHotkeys& keys_;
    AppSettings& settings_;
    NotificationModel& notifications_;
    QHash<quint32, QString> soundKeys_;
    std::function<QString(quint32)> soundName_;
    std::function<QString(const QString&)> voiceName_;
    /// Voice ids by binding id (kVoiceIdBase + index), rebuilt on register.
    QStringList voiceOrder_;
};

} // namespace vox::app
