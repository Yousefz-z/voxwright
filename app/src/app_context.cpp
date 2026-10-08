#include "app_context.hpp"

#include <vox/plugins/voice_library.hpp>

#include <QDesktopServices>
#include <QUrl>

namespace vox::app {
namespace {

constexpr int kSaveDelayMs = 500;

} // namespace

AppContext::AppContext(Options options, QObject* parent)
    : QObject(parent)
    , store_(options.settingsPath.isEmpty() ? SettingsStore::defaultPath() : options.settingsPath)
    , backend_(std::move(options.backend)) {
    auto loaded = store_.load();
    settings_ = loaded.settings;
    if (options.backendError) {
        notifications_.post(QStringLiteral("backend"), NotificationModel::Level::Error,
                            tr("No audio system"),
                            QString::fromStdString(options.backendError->message));
    }
    if (loaded.problem) {
        notifications_.post(QStringLiteral("settings"), NotificationModel::Level::Warning,
                            tr("Settings reset"), QString::fromStdString(loaded.problem->message));
    }

    engine::EngineConfig config;
    config.periodFrames = static_cast<std::uint32_t>(settings_.periodFrames);
    config.exclusive = settings_.exclusive;
    engine_ = std::make_unique<engine::AudioEngine>(*backend_, config);

    const auto& registry = plugins::EffectRegistry::builtin();
    if (auto voices = plugins::loadBuiltinVoices(registry)) {
        presets_ = std::move(voices).value();
    } else {
        notifications_.post(QStringLiteral("voices"), NotificationModel::Level::Error,
                            tr("Voices could not be loaded"),
                            QString::fromStdString(voices.error().message));
    }

    audio_ = std::make_unique<AudioController>(*engine_, settings_, notifications_);
    audio_->setMicrophonePermissionCheck(options.checkMicrophonePermission);
    voices_ =
        std::make_unique<VoiceController>(*engine_, registry, presets_, settings_, notifications_);
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(kSaveDelayMs);
    connect(&saveTimer_, &QTimer::timeout, this, [this] {
        if (auto saved = saveNow(); !saved) {
            notifications_.post(QStringLiteral("settings-save"), NotificationModel::Level::Error,
                                tr("Settings not saved"),
                                QString::fromStdString(saved.error().message));
        }
    });
    connect(audio_.get(), &AudioController::settingsChanged, this, &AppContext::scheduleSave);
    connect(voices_.get(), &VoiceController::settingsChanged, this, &AppContext::scheduleSave);

    // The voice goes in first so the engine starts with it already built.
    voices_->initialize();
    audio_->initialize();
}

AppContext::~AppContext() {
    static_cast<void>(saveNow());
    engine_->stop();
}

QString AppContext::version() {
    return QStringLiteral(VOX_APP_VERSION);
}

Status AppContext::saveNow() {
    saveTimer_.stop();
    auto saved = store_.save(settings_);
    if (saved) {
        notifications_.dismiss(QStringLiteral("settings-save"));
    }
    return saved;
}

void AppContext::scheduleSave() {
    saveTimer_.start();
}

void AppContext::runAction(const QString& action) {
    if (action == QStringLiteral("restart-audio")) {
        audio_->restart();
    } else if (action == QStringLiteral("get-virtual-cable")) {
        QDesktopServices::openUrl(QUrl(AudioController::virtualCableUrl()));
    } else {
        emit uiActionRequested(action);
    }
}

} // namespace vox::app
