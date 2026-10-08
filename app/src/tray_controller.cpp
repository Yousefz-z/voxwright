#include "tray_controller.hpp"

#include "audio_controller.hpp"
#include "soundboard_controller.hpp"
#include "voice_controller.hpp"

#include <QAction>
#include <QMenu>
#include <QSystemTrayIcon>

namespace vox::app {

TrayController::TrayController(AudioController& audio, VoiceController& voices,
                               SoundboardController& soundboard, QObject* parent)
    : QObject(parent)
    , audio_(audio)
    , voices_(voices)
    , menu_(std::make_unique<QMenu>()) {
    QAction* open = menu_->addAction(tr("Open Voxwright"));
    connect(open, &QAction::triggered, this, &TrayController::showWindowRequested);
    menu_->addSeparator();

    const auto addSwitch = [this](const QString& text, auto setter) {
        QAction* action = menu_->addAction(text);
        action->setCheckable(true);
        connect(action, &QAction::toggled, &audio_, setter);
        return action;
    };
    voiceChanger_ = addSwitch(tr("Voice changer"), &AudioController::setVoiceEnabled);
    hearMyself_ = addSwitch(tr("Hear myself"), &AudioController::setHearMyself);
    background_ = addSwitch(tr("Background effects"), &AudioController::setBackgroundEnabled);
    mute_ = addSwitch(tr("Mute microphone"), &AudioController::setMuted);
    voiceMenu_ = menu_->addMenu(tr("Favorite voices"));
    menu_->addSeparator();
    QAction* stop = menu_->addAction(tr("Stop all sounds"));
    connect(stop, &QAction::triggered, &soundboard, &SoundboardController::stopAll);
    menu_->addSeparator();
    QAction* quit = menu_->addAction(tr("Quit Voxwright"));
    connect(quit, &QAction::triggered, this, &TrayController::quitRequested);

    connect(&audio_, &AudioController::settingsChanged, this, &TrayController::syncChecks);
    connect(&audio_, &AudioController::mutedChanged, this, &TrayController::syncChecks);
    connect(&voices_, &VoiceController::favoritesChanged, this, &TrayController::rebuildVoices);
    connect(&voices_, &VoiceController::currentVoiceChanged, this, &TrayController::rebuildVoices);
    connect(&voices_, &VoiceController::voicesChanged, this, &TrayController::rebuildVoices);
    syncChecks();
    rebuildVoices();

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        icon_ =
            new QSystemTrayIcon(QIcon(QStringLiteral(":/qt/qml/Voxwright/icons/app.svg")), this);
        icon_->setToolTip(QStringLiteral("Voxwright"));
        icon_->setContextMenu(menu_.get());
        connect(icon_, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger ||
                        reason == QSystemTrayIcon::DoubleClick) {
                        emit showWindowRequested();
                    }
                });
    }
}

TrayController::~TrayController() = default;

void TrayController::show() {
    if (icon_ != nullptr) {
        icon_->show();
    }
}

void TrayController::notify(const QString& title, const QString& message) {
    if (icon_ != nullptr && QSystemTrayIcon::supportsMessages()) {
        icon_->showMessage(title, message, QSystemTrayIcon::Information, 4000);
    }
}

void TrayController::syncChecks() {
    // Setting a checkmark must not echo back as a user toggle.
    const auto set = [](QAction* action, bool on) {
        const QSignalBlocker blocker(action);
        action->setChecked(on);
    };
    set(voiceChanger_, audio_.voiceEnabled());
    set(hearMyself_, audio_.hearMyself());
    set(background_, audio_.backgroundEnabled());
    set(mute_, audio_.muted());
}

void TrayController::rebuildVoices() {
    voiceMenu_->clear();
    const QString current = voices_.currentVoiceId();
    for (const auto& p : voices_.presets()) {
        const QString id = QString::fromStdString(p.id);
        if (!voices_.isFavorite(id)) {
            continue;
        }
        QAction* action = voiceMenu_->addAction(QString::fromStdString(p.name));
        action->setCheckable(true);
        action->setChecked(id == current);
        connect(action, &QAction::triggered, this,
                [this, id] { static_cast<void>(voices_.selectVoice(id)); });
    }
    if (voiceMenu_->isEmpty()) {
        QAction* hint = voiceMenu_->addAction(tr("Star voices to list them here"));
        hint->setEnabled(false);
    }
}

} // namespace vox::app
