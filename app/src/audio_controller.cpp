#include "audio_controller.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPermissions>

#include <algorithm>
#include <cmath>

namespace vox::app {
namespace {

constexpr int kPollIntervalMs = 33;
constexpr double kMeterFloorDb = -60.0;

QString roleTitle(engine::DeviceRole role) {
    switch (role) {
    case engine::DeviceRole::Input:
        return QObject::tr("Microphone disconnected");
    case engine::DeviceRole::VirtualMic:
        return QObject::tr("Virtual microphone output disconnected");
    case engine::DeviceRole::Monitor:
        return QObject::tr("Headphones disconnected");
    }
    return {};
}

QString deviceKey(engine::DeviceRole role) {
    return QStringLiteral("device-%1").arg(static_cast<int>(role));
}

double toDb(float linear) {
    return linear > 1e-6F ? 20.0 * std::log10(static_cast<double>(linear)) : -120.0;
}

} // namespace

AudioController::AudioController(engine::AudioEngine& engine, AppSettings& settings,
                                 NotificationModel& notifications, QObject* parent)
    : QObject(parent)
    , engine_(engine)
    , settings_(settings)
    , notifications_(notifications)
    , inputDevices_({.systemDefaultEntry = true, .noneEntry = false, .virtualCablesFirst = false})
    , virtualMicDevices_(
          {.systemDefaultEntry = false, .noneEntry = true, .virtualCablesFirst = true})
    , monitorDevices_(
          {.systemDefaultEntry = true, .noneEntry = false, .virtualCablesFirst = false}) {
    pollTimer_.setInterval(kPollIntervalMs);
    connect(&pollTimer_, &QTimer::timeout, this, &AudioController::poll);
}

void AudioController::initialize() {
    refreshDevices();
    pickVirtualCableIfUnset();
    applySettingsToEngine();
    startEngine();
    pollTimer_.start();
}

// ---------------------------------------------------------------- devices

void AudioController::refreshDevices() {
    const auto capture = engine_.devices(devices::DeviceKind::Capture);
    const auto playback = engine_.devices(devices::DeviceKind::Playback);
    if (!capture || !playback) {
        const Error& e = !capture ? capture.error() : playback.error();
        notifications_.post(QStringLiteral("device-list"), NotificationModel::Level::Error,
                            tr("Audio devices unavailable"), QString::fromStdString(e.message));
        return;
    }
    notifications_.dismiss(QStringLiteral("device-list"));
    inputDevices_.setDevices(capture.value());
    virtualMicDevices_.setDevices(playback.value());
    monitorDevices_.setDevices(playback.value());
    emit devicesChanged();
}

bool AudioController::pickVirtualCableIfUnset() {
    if (settings_.virtualMicChosen) {
        return false;
    }
    if (const auto* cable = virtualMicDevices_.firstVirtualCable()) {
        settings_.virtualMicId = QString::fromStdString(cable->id);
        settings_.virtualMicName = QString::fromStdString(cable->name);
        settings_.useVirtualMic = true;
        settings_.virtualMicChosen = true;
        notifications_.post(QStringLiteral("virtual-mic-picked"), NotificationModel::Level::Info,
                            tr("Virtual microphone ready"),
                            tr("Voxwright plays into \"%1\". In Discord, Zoom, or your game, "
                               "choose \"%2\" as the microphone.")
                                .arg(settings_.virtualMicName, chatAppMicrophoneName()));
        emit settingsChanged();
        emit devicesChanged();
        return true;
    }
    return false;
}

bool AudioController::virtualCableFound() const {
    return virtualMicDevices_.firstVirtualCable() != nullptr;
}

QString AudioController::chatAppMicrophoneName() const {
    // VB-CABLE's render side is "CABLE Input"; apps record from "CABLE Output".
    // Loopback drivers such as BlackHole use one name for both sides.
    QString name = settings_.virtualMicName;
    if (name.contains(QStringLiteral("CABLE Input"), Qt::CaseInsensitive)) {
        name.replace(QStringLiteral("CABLE Input"), QStringLiteral("CABLE Output"),
                     Qt::CaseInsensitive);
    }
    return name;
}

QString AudioController::virtualCableProduct() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("BlackHole 2ch");
#else
    return QStringLiteral("VB-CABLE");
#endif
}

QString AudioController::virtualCableUrl() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("https://existential.audio/blackhole/");
#else
    return QStringLiteral("https://vb-audio.com/Cable/");
#endif
}

QString AudioController::virtualMicDeviceId() const {
    return settings_.useVirtualMic ? settings_.virtualMicId : DeviceListModel::noneId();
}

engine::DeviceSelection AudioController::resolveSelection() {
    engine::DeviceSelection s;
    s.inputId = settings_.inputId.toStdString();
    if (!settings_.inputId.isEmpty() && !inputDevices_.hasDevice(settings_.inputId)) {
        notifications_.post(deviceKey(engine::DeviceRole::Input), NotificationModel::Level::Warning,
                            tr("Microphone not found"),
                            tr("\"%1\" is not connected, so the system default microphone is in "
                               "use. Reconnect it or choose another one in Audio settings.")
                                .arg(settings_.inputName),
                            tr("Audio settings"), QStringLiteral("open-audio"));
        s.inputId.clear();
    }
    s.useVirtualMic = settings_.useVirtualMic && !settings_.virtualMicId.isEmpty();
    s.virtualMicId = settings_.virtualMicId.toStdString();
    if (s.useVirtualMic && !virtualMicDevices_.hasDevice(settings_.virtualMicId)) {
        notifications_.post(deviceKey(engine::DeviceRole::VirtualMic),
                            NotificationModel::Level::Warning, tr("Virtual microphone not found"),
                            tr("\"%1\" is not available, so other apps cannot hear your voice "
                               "yet. Reinstall %2 or choose another output in Audio settings.")
                                .arg(settings_.virtualMicName, virtualCableProduct()),
                            tr("Audio settings"), QStringLiteral("open-audio"));
        s.useVirtualMic = false;
    }
    if (!s.useVirtualMic && !virtualCableFound()) {
        notifications_.post(QStringLiteral("no-virtual-cable"), NotificationModel::Level::Warning,
                            tr("No virtual microphone installed"),
                            tr("To use your changed voice in other apps, install %1 (free), "
                               "then select it here.")
                                .arg(virtualCableProduct()),
                            tr("Get %1").arg(virtualCableProduct()),
                            QStringLiteral("get-virtual-cable"));
    } else {
        notifications_.dismiss(QStringLiteral("no-virtual-cable"));
    }
    s.monitorId = settings_.monitorId.toStdString();
    if (!settings_.monitorId.isEmpty() && !monitorDevices_.hasDevice(settings_.monitorId)) {
        notifications_.post(deviceKey(engine::DeviceRole::Monitor),
                            NotificationModel::Level::Warning, tr("Headphones not found"),
                            tr("\"%1\" is not connected, so the system default output is in "
                               "use.")
                                .arg(settings_.monitorName),
                            tr("Audio settings"), QStringLiteral("open-audio"));
        s.monitorId.clear();
    }
    return s;
}

void AudioController::startEngine() {
    engine::DeviceSelection selection = resolveSelection();
    // macOS asks the user before an app may record. Ask first; until the
    // answer arrives (or if it is no) the engine runs without the
    // microphone, so sounds and speech still work.
    const QMicrophonePermission microphone;
    const Qt::PermissionStatus permission =
        checkPermission_ ? qApp->checkPermission(microphone) : Qt::PermissionStatus::Granted;
    switch (permission) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(microphone, this, [this](const QPermission&) { restart(); });
        selection.useInput = false;
        break;
    case Qt::PermissionStatus::Denied:
        notifications_.post(QStringLiteral("microphone-permission"),
                            NotificationModel::Level::Error, tr("Microphone access denied"),
                            tr("Voxwright is not allowed to use the microphone. Allow it in "
                               "System Settings > Privacy & Security > Microphone, then restart "
                               "Voxwright."));
        selection.useInput = false;
        break;
    case Qt::PermissionStatus::Granted:
        notifications_.dismiss(QStringLiteral("microphone-permission"));
        break;
    }
    engine_.setDeviceOptions(static_cast<std::uint32_t>(settings_.periodFrames),
                             settings_.exclusive);
    if (auto started = engine_.start(selection); !started) {
        notifications_.post(QStringLiteral("engine"), NotificationModel::Level::Error,
                            tr("Audio could not start"),
                            QString::fromStdString(started.error().message), tr("Audio settings"),
                            QStringLiteral("open-audio"));
    } else {
        notifications_.dismiss(QStringLiteral("engine"));
    }
    emit runningChanged();
}

void AudioController::restart() {
    refreshDevices();
    startEngine();
}

void AudioController::setInputDeviceId(const QString& id) {
    if (id == settings_.inputId) {
        return;
    }
    settings_.inputId = id;
    settings_.inputName = inputDevices_.nameOf(id);
    notifications_.dismiss(deviceKey(engine::DeviceRole::Input));
    emit devicesChanged();
    emit settingsChanged();
    startEngine();
}

void AudioController::setVirtualMicDeviceId(const QString& id) {
    const bool none = id == DeviceListModel::noneId();
    if (none ? !settings_.useVirtualMic
             : (settings_.useVirtualMic && id == settings_.virtualMicId)) {
        return;
    }
    settings_.useVirtualMic = !none;
    settings_.virtualMicChosen = true;
    if (!none) {
        settings_.virtualMicId = id;
        settings_.virtualMicName = virtualMicDevices_.nameOf(id);
    }
    notifications_.dismiss(deviceKey(engine::DeviceRole::VirtualMic));
    notifications_.dismiss(QStringLiteral("virtual-mic-picked"));
    emit devicesChanged();
    emit settingsChanged();
    startEngine();
}

void AudioController::setMonitorDeviceId(const QString& id) {
    if (id == settings_.monitorId) {
        return;
    }
    settings_.monitorId = id;
    settings_.monitorName = monitorDevices_.nameOf(id);
    notifications_.dismiss(deviceKey(engine::DeviceRole::Monitor));
    emit devicesChanged();
    emit settingsChanged();
    startEngine();
}

// ---------------------------------------------------------------- settings

void AudioController::check(const Status& status) {
    if (!status) {
        notifications_.post(QStringLiteral("engine-busy"), NotificationModel::Level::Warning,
                            tr("A change was not applied"),
                            QString::fromStdString(status.error().message), tr("Restart audio"),
                            QStringLiteral("restart-audio"));
    }
}

void AudioController::setTransmitMode(int mode) {
    const auto m = static_cast<engine::TransmitMode>(std::clamp(mode, 0, 2));
    if (m == settings_.transmitMode) {
        return;
    }
    settings_.transmitMode = m;
    engine_.transmit().setMode(m);
    emit settingsChanged();
}

void AudioController::setReleaseDelayMs(double ms) {
    settings_.releaseDelayMs = static_cast<float>(std::clamp(ms, 0.0, 1000.0));
    engine_.transmit().setReleaseDelayMs(settings_.releaseDelayMs);
    emit settingsChanged();
}

void AudioController::setMuted(bool muted) {
    if (muted == muted_) {
        return;
    }
    muted_ = muted;
    engine_.transmit().setMuted(muted);
    emit mutedChanged();
}

void AudioController::applySettingsToEngine() {
    engine_.transmit().setMode(settings_.transmitMode);
    engine_.transmit().setReleaseDelayMs(settings_.releaseDelayMs);
    check(engine_.setVoiceEnabled(settings_.voiceEnabled));
    check(engine_.setBackgroundEnabled(settings_.backgroundEnabled));
    check(engine_.setHearMyself(settings_.hearMyself));
    check(engine_.setNoiseSuppression(settings_.noiseReduction, settings_.noiseReductionStrength));
    check(engine_.setGate(settings_.gate, settings_.gateThresholdDb));
    check(engine_.setInputGainDb(settings_.inputGainDb));
    check(engine_.setMixLevels(settings_.mix));
}

void AudioController::setHearMyself(bool on) {
    if (on == settings_.hearMyself) {
        return;
    }
    settings_.hearMyself = on;
    if (on) {
        notifications_.dismiss(QStringLiteral("feedback"));
    }
    check(engine_.setHearMyself(on));
    emit settingsChanged();
}

void AudioController::setVoiceEnabled(bool on) {
    if (on == settings_.voiceEnabled) {
        return;
    }
    settings_.voiceEnabled = on;
    check(engine_.setVoiceEnabled(on));
    emit settingsChanged();
}

void AudioController::setBackgroundEnabled(bool on) {
    if (on == settings_.backgroundEnabled) {
        return;
    }
    settings_.backgroundEnabled = on;
    check(engine_.setBackgroundEnabled(on));
    emit settingsChanged();
}

void AudioController::setNoiseReduction(bool on) {
    if (on == settings_.noiseReduction) {
        return;
    }
    settings_.noiseReduction = on;
    check(engine_.setNoiseSuppression(on, settings_.noiseReductionStrength));
    emit settingsChanged();
}

void AudioController::setNoiseReductionStrength(double strength) {
    settings_.noiseReductionStrength = static_cast<float>(std::clamp(strength, 0.0, 1.0));
    check(engine_.setNoiseSuppression(settings_.noiseReduction, settings_.noiseReductionStrength));
    emit settingsChanged();
}

void AudioController::setGateEnabled(bool on) {
    if (on == settings_.gate) {
        return;
    }
    settings_.gate = on;
    check(engine_.setGate(on, settings_.gateThresholdDb));
    emit settingsChanged();
}

void AudioController::setGateThresholdDb(double db) {
    settings_.gateThresholdDb = static_cast<float>(std::clamp(db, -90.0, 0.0));
    check(engine_.setGate(settings_.gate, settings_.gateThresholdDb));
    emit settingsChanged();
}

void AudioController::setInputGainDb(double db) {
    settings_.inputGainDb = static_cast<float>(std::clamp(db, -24.0, 24.0));
    check(engine_.setInputGainDb(settings_.inputGainDb));
    emit settingsChanged();
}

void AudioController::setVoiceLevelDb(double db) {
    settings_.mix.voiceDb = static_cast<float>(std::clamp(db, -60.0, 12.0));
    check(engine_.setMixLevels(settings_.mix));
    emit settingsChanged();
}

void AudioController::setSoundsLevelDb(double db) {
    settings_.mix.soundsDb = static_cast<float>(std::clamp(db, -60.0, 12.0));
    check(engine_.setMixLevels(settings_.mix));
    emit settingsChanged();
}

void AudioController::setSoundsInHeadphones(bool on) {
    settings_.mix.soundsInMonitor = on;
    check(engine_.setMixLevels(settings_.mix));
    emit settingsChanged();
}

void AudioController::setSpeechLevelDb(double db) {
    settings_.mix.speechDb = static_cast<float>(std::clamp(db, -60.0, 12.0));
    check(engine_.setMixLevels(settings_.mix));
    emit settingsChanged();
}

void AudioController::setMonitorLevelDb(double db) {
    settings_.mix.monitorDb = static_cast<float>(std::clamp(db, -60.0, 12.0));
    check(engine_.setMixLevels(settings_.mix));
    emit settingsChanged();
}

void AudioController::setBufferFrames(int frames) {
    const int clamped = std::clamp(frames, 32, 2048);
    if (clamped == settings_.periodFrames) {
        return;
    }
    settings_.periodFrames = clamped;
    emit settingsChanged();
    startEngine();
}

void AudioController::setExclusiveMode(bool on) {
    if (on == settings_.exclusive) {
        return;
    }
    settings_.exclusive = on;
    emit settingsChanged();
    startEngine();
}

// ---------------------------------------------------------------- polling

double AudioController::meterPosition(double db) {
    return std::clamp((db - kMeterFloorDb) / -kMeterFloorDb, 0.0, 1.0);
}

void AudioController::poll() {
    for (const auto& event : engine_.poll()) {
        handleEvent(event);
    }
    stats_ = engine_.stats();
    inputDb_ = toDb(stats_.inputPeak);
    outputDb_ = toDb(stats_.outputPeak);
    emit metersChanged();
}

QVariantList AudioController::latencyBreakdown() const {
    const auto& b = stats_.virtualMicLatency;
    QVariantList rows;
    const auto add = [&rows](const QString& label, double ms) {
        if (ms > 0.0) {
            rows.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("ms"), ms}});
        }
    };
    add(tr("Microphone buffer"), b.captureMs);
    add(tr("Rate conversion in"), b.inputResamplerMs);
    add(tr("Noise reduction"), b.noiseSuppressionMs);
    add(tr("Voice"), b.voiceMs);
    add(tr("Limiter"), b.limiterMs);
    add(tr("Safety buffer"), b.bufferMs);
    add(tr("Rate conversion out"), b.outputResamplerMs);
    add(tr("Output buffer"), b.playbackMs);
    return rows;
}

void AudioController::handleEvent(const engine::EngineEvent& event) {
    using Kind = engine::EngineEventKind;
    switch (event.kind) {
    case Kind::DeviceLost: {
        const auto role = static_cast<engine::DeviceRole>(event.value);
        const QString name = QString::fromStdString(event.detail);
        QString message;
        switch (role) {
        case engine::DeviceRole::Input:
            message = tr("\"%1\" was disconnected. Sounds keep playing; reconnect it or choose "
                         "another microphone.")
                          .arg(name);
            break;
        case engine::DeviceRole::VirtualMic:
            message = tr("\"%1\" was disconnected, so other apps cannot hear you. Reconnect it "
                         "or choose another output.")
                          .arg(name);
            break;
        case engine::DeviceRole::Monitor:
            message = tr("\"%1\" was disconnected. Your voice still reaches other apps.").arg(name);
            break;
        }
        notifications_.post(deviceKey(role), NotificationModel::Level::Warning, roleTitle(role),
                            message, tr("Audio settings"), QStringLiteral("open-audio"));
        emit runningChanged();
        break;
    }
    case Kind::DeviceRestored:
        notifications_.dismiss(deviceKey(static_cast<engine::DeviceRole>(event.value)));
        emit runningChanged();
        break;
    case Kind::RestartFailed:
        notifications_.post(QStringLiteral("engine"), NotificationModel::Level::Error,
                            tr("Audio stopped"), QString::fromStdString(event.detail),
                            tr("Try again"), QStringLiteral("restart-audio"));
        emit runningChanged();
        break;
    case Kind::FeedbackDetected:
        settings_.hearMyself = false;
        notifications_.post(QStringLiteral("feedback"), NotificationModel::Level::Warning,
                            tr("Hear myself was turned off"),
                            tr("Your microphone picked up your own monitor (a howl at %1 Hz). "
                               "Use headphones or lower the volume before turning it on again.")
                                .arg(event.value));
        emit settingsChanged();
        break;
    case Kind::DevicesChanged:
        refreshDevices();
        if (pickVirtualCableIfUnset()) {
            startEngine(); // a cable was just installed: start using it
        }
        break;
    case Kind::SoundFinished:
        emit soundFinished(event.value);
        break;
    case Kind::SpeechFinished:
        emit speechFinished();
        break;
    }
}

} // namespace vox::app
