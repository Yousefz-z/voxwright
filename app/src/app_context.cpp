#include "app_context.hpp"

#include <vox/plugins/voice_library.hpp>

#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace vox::app {
namespace {

constexpr int kSaveDelayMs = 500;

QString inDir(const QString& dir, const QString& fallback, const QString& name) {
    return dir.isEmpty() ? fallback : dir + QLatin1Char('/') + name;
}

} // namespace

AppContext::AppContext(Options options, QObject* parent)
    : QObject(parent)
    , store_(inDir(options.dataDir, SettingsStore::defaultPath(), QStringLiteral("settings.json")))
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

    globalHotkeys_ = options.hotkeys ? std::move(options.hotkeys) : createPlatformHotkeys();
    hotkeyController_ = std::make_unique<HotkeyController>(*globalHotkeys_, engine_->transmit(),
                                                           settings_, notifications_);
    connect(hotkeyController_.get(), &HotkeyController::bindingsChanged, this,
            &AppContext::scheduleSave);
    connect(hotkeyController_.get(), &HotkeyController::actionTriggered, this,
            &AppContext::runHotkeyAction);
    connect(hotkeyController_.get(), &HotkeyController::voiceTriggered, voices_.get(),
            &VoiceController::selectVoice);
    hotkeyController_->setVoiceNamer([this](const QString& id) {
        const auto found = std::ranges::find_if(
            presets_, [&](const plugins::VoicePreset& p) { return p.id == id.toStdString(); });
        return found == presets_.end() ? QString{} : QString::fromStdString(found->name);
    });

    const QString dataFallback = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    soundboard_ = std::make_unique<SoundboardController>(
        *engine_, *hotkeyController_, notifications_,
        SoundboardController::Paths{inDir(options.dataDir, SoundboardStore::defaultPath(),
                                          QStringLiteral("soundboards.json")),
                                    inDir(options.dataDir, dataFallback + QStringLiteral("/sounds"),
                                          QStringLiteral("sounds"))});
    connect(audio_.get(), &AudioController::soundFinished, soundboard_.get(),
            &SoundboardController::onSoundFinished);

    // The voice goes in first so the engine starts with it already built.
    voices_->initialize();
    audio_->initialize();
    hotkeyController_->initialize();
    soundboard_->initialize();
}

void AppContext::runHotkeyAction(HotkeyController::Action action) {
    using Action = HotkeyController::Action;
    switch (action) {
    case Action::VoiceChanger:
        audio_->setVoiceEnabled(!audio_->voiceEnabled());
        break;
    case Action::HearMyself:
        audio_->setHearMyself(!audio_->hearMyself());
        break;
    case Action::Mute:
        audio_->setMuted(!audio_->muted());
        break;
    case Action::Background:
        audio_->setBackgroundEnabled(!audio_->backgroundEnabled());
        break;
    case Action::StopSounds:
        soundboard_->stopAll();
        break;
    case Action::NextVoice:
        voices_->selectRelative(1);
        break;
    case Action::PreviousVoice:
        voices_->selectRelative(-1);
        break;
    case Action::RandomVoice:
        voices_->selectRandom();
        break;
    case Action::Talk:
    case Action::Censor:
        break; // handled the moment the key moves (HotkeyController)
    }
}

AppContext::~AppContext() {
    static_cast<void>(saveNow());
    static_cast<void>(soundboard_->saveNow());
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
