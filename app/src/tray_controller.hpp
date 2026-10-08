#pragma once

#include <QIcon>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;

namespace vox::app {

class AudioController;
class SoundboardController;
class VoiceController;

/// The icon in the system tray (Windows notification area, macOS menu bar)
/// and its menu: show the window, the main switches, favorite voices, stop
/// sounds, and quit. Its checkmarks follow the controllers.
class TrayController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    /// False where the desktop has no tray (then closing the window quits).
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    TrayController(AudioController& audio, VoiceController& voices,
                   SoundboardController& soundboard, QObject* parent = nullptr);
    TrayController(const TrayController&) = delete;
    TrayController& operator=(const TrayController&) = delete;
    TrayController(TrayController&&) = delete;
    TrayController& operator=(TrayController&&) = delete;
    ~TrayController() override;

    /// Shows the icon (when the system has a tray).
    void show();
    [[nodiscard]] bool available() const { return icon_ != nullptr; }
    /// Shows a tray balloon, if the tray supports one.
    Q_INVOKABLE void notify(const QString& title, const QString& message);

    [[nodiscard]] QMenu* menu() const { return menu_.get(); }

signals:
    void showWindowRequested();
    void quitRequested();

private:
    void rebuildVoices();
    void syncChecks();

    AudioController& audio_;
    VoiceController& voices_;
    std::unique_ptr<QMenu> menu_;
    QMenu* voiceMenu_ = nullptr;
    QAction* voiceChanger_ = nullptr;
    QAction* hearMyself_ = nullptr;
    QAction* mute_ = nullptr;
    QAction* background_ = nullptr;
    QSystemTrayIcon* icon_ = nullptr;
};

} // namespace vox::app
