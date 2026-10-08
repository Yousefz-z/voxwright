#pragma once

#include "device_list_model.hpp"
#include "notification_model.hpp"
#include "settings.hpp"

#include <vox/engine/audio_engine.hpp>

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace vox::app {

/// The UI's view of the audio engine: device pickers, input and monitor
/// settings, meters, the latency readout, and engine events turned into
/// notifications. Every change is applied to the engine at once and saved
/// in AppSettings.
class AudioController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    /// True while the voice is being played into a virtual microphone.
    Q_PROPERTY(bool sendingToVirtualMic READ sendingToVirtualMic NOTIFY runningChanged)
    Q_PROPERTY(vox::app::DeviceListModel* inputDevices READ inputDevices CONSTANT)
    Q_PROPERTY(vox::app::DeviceListModel* virtualMicDevices READ virtualMicDevices CONSTANT)
    Q_PROPERTY(vox::app::DeviceListModel* monitorDevices READ monitorDevices CONSTANT)
    Q_PROPERTY(
        QString inputDeviceId READ inputDeviceId WRITE setInputDeviceId NOTIFY devicesChanged)
    Q_PROPERTY(QString virtualMicDeviceId READ virtualMicDeviceId WRITE setVirtualMicDeviceId NOTIFY
                   devicesChanged)
    Q_PROPERTY(
        QString monitorDeviceId READ monitorDeviceId WRITE setMonitorDeviceId NOTIFY devicesChanged)
    Q_PROPERTY(bool virtualCableFound READ virtualCableFound NOTIFY devicesChanged)
    Q_PROPERTY(QString chatAppMicrophoneName READ chatAppMicrophoneName NOTIFY devicesChanged)
    Q_PROPERTY(QString virtualCableProduct READ virtualCableProduct CONSTANT)
    Q_PROPERTY(QString virtualCableUrl READ virtualCableUrl CONSTANT)

    Q_PROPERTY(bool hearMyself READ hearMyself WRITE setHearMyself NOTIFY settingsChanged)
    Q_PROPERTY(bool voiceEnabled READ voiceEnabled WRITE setVoiceEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool backgroundEnabled READ backgroundEnabled WRITE setBackgroundEnabled NOTIFY
                   settingsChanged)
    Q_PROPERTY(
        bool noiseReduction READ noiseReduction WRITE setNoiseReduction NOTIFY settingsChanged)
    Q_PROPERTY(double noiseReductionStrength READ noiseReductionStrength WRITE
                   setNoiseReductionStrength NOTIFY settingsChanged)
    Q_PROPERTY(bool gateEnabled READ gateEnabled WRITE setGateEnabled NOTIFY settingsChanged)
    Q_PROPERTY(
        double gateThresholdDb READ gateThresholdDb WRITE setGateThresholdDb NOTIFY settingsChanged)
    Q_PROPERTY(double inputGainDb READ inputGainDb WRITE setInputGainDb NOTIFY settingsChanged)
    Q_PROPERTY(double voiceLevelDb READ voiceLevelDb WRITE setVoiceLevelDb NOTIFY settingsChanged)
    Q_PROPERTY(
        double soundsLevelDb READ soundsLevelDb WRITE setSoundsLevelDb NOTIFY settingsChanged)
    Q_PROPERTY(
        double speechLevelDb READ speechLevelDb WRITE setSpeechLevelDb NOTIFY settingsChanged)
    Q_PROPERTY(
        double monitorLevelDb READ monitorLevelDb WRITE setMonitorLevelDb NOTIFY settingsChanged)
    Q_PROPERTY(int bufferFrames READ bufferFrames WRITE setBufferFrames NOTIFY settingsChanged)
    Q_PROPERTY(bool exclusiveMode READ exclusiveMode WRITE setExclusiveMode NOTIFY settingsChanged)

    Q_PROPERTY(double inputLevel READ inputLevel NOTIFY metersChanged)
    Q_PROPERTY(double inputLevelDb READ inputLevelDb NOTIFY metersChanged)
    Q_PROPERTY(double outputLevel READ outputLevel NOTIFY metersChanged)
    Q_PROPERTY(bool gateOpen READ gateOpen NOTIFY metersChanged)
    Q_PROPERTY(bool transmitting READ transmitting NOTIFY metersChanged)
    Q_PROPERTY(double latencyMs READ latencyMs NOTIFY metersChanged)
    Q_PROPERTY(QVariantList latencyBreakdown READ latencyBreakdown NOTIFY metersChanged)
    Q_PROPERTY(double processingLoad READ processingLoad NOTIFY metersChanged)

public:
    AudioController(engine::AudioEngine& engine, AppSettings& settings,
                    NotificationModel& notifications, QObject* parent = nullptr);

    /// Lists devices, resolves the saved selection, and starts the engine.
    void initialize();
    /// See AppContext::Options::checkMicrophonePermission.
    void setMicrophonePermissionCheck(bool check) { checkPermission_ = check; }
    /// One step of the 30 Hz housekeeping (public for tests).
    void poll();

    Q_INVOKABLE void restart();
    Q_INVOKABLE void refreshDevices();

    [[nodiscard]] bool running() const { return engine_.isRunning(); }
    [[nodiscard]] bool sendingToVirtualMic() const {
        return engine_.isRunning() && engine_.activeDevices().virtualMic.has_value();
    }
    [[nodiscard]] DeviceListModel* inputDevices() { return &inputDevices_; }
    [[nodiscard]] DeviceListModel* virtualMicDevices() { return &virtualMicDevices_; }
    [[nodiscard]] DeviceListModel* monitorDevices() { return &monitorDevices_; }
    [[nodiscard]] QString inputDeviceId() const { return settings_.inputId; }
    void setInputDeviceId(const QString& id);
    [[nodiscard]] QString virtualMicDeviceId() const;
    void setVirtualMicDeviceId(const QString& id);
    [[nodiscard]] QString monitorDeviceId() const { return settings_.monitorId; }
    void setMonitorDeviceId(const QString& id);
    [[nodiscard]] bool virtualCableFound() const;
    [[nodiscard]] QString chatAppMicrophoneName() const;
    [[nodiscard]] static QString virtualCableProduct();
    [[nodiscard]] static QString virtualCableUrl();

    [[nodiscard]] bool hearMyself() const { return settings_.hearMyself; }
    void setHearMyself(bool on);
    [[nodiscard]] bool voiceEnabled() const { return settings_.voiceEnabled; }
    void setVoiceEnabled(bool on);
    [[nodiscard]] bool backgroundEnabled() const { return settings_.backgroundEnabled; }
    void setBackgroundEnabled(bool on);
    [[nodiscard]] bool noiseReduction() const { return settings_.noiseReduction; }
    void setNoiseReduction(bool on);
    [[nodiscard]] double noiseReductionStrength() const {
        return static_cast<double>(settings_.noiseReductionStrength);
    }
    void setNoiseReductionStrength(double strength);
    [[nodiscard]] bool gateEnabled() const { return settings_.gate; }
    void setGateEnabled(bool on);
    [[nodiscard]] double gateThresholdDb() const {
        return static_cast<double>(settings_.gateThresholdDb);
    }
    void setGateThresholdDb(double db);
    [[nodiscard]] double inputGainDb() const { return static_cast<double>(settings_.inputGainDb); }
    void setInputGainDb(double db);
    [[nodiscard]] double voiceLevelDb() const { return static_cast<double>(settings_.mix.voiceDb); }
    void setVoiceLevelDb(double db);
    [[nodiscard]] double soundsLevelDb() const {
        return static_cast<double>(settings_.mix.soundsDb);
    }
    void setSoundsLevelDb(double db);
    [[nodiscard]] double speechLevelDb() const {
        return static_cast<double>(settings_.mix.speechDb);
    }
    void setSpeechLevelDb(double db);
    [[nodiscard]] double monitorLevelDb() const {
        return static_cast<double>(settings_.mix.monitorDb);
    }
    void setMonitorLevelDb(double db);
    [[nodiscard]] int bufferFrames() const { return settings_.periodFrames; }
    void setBufferFrames(int frames);
    [[nodiscard]] bool exclusiveMode() const { return settings_.exclusive; }
    void setExclusiveMode(bool on);

    [[nodiscard]] double inputLevel() const { return meterPosition(inputDb_); }
    [[nodiscard]] double inputLevelDb() const { return inputDb_; }
    [[nodiscard]] double outputLevel() const { return meterPosition(outputDb_); }
    [[nodiscard]] bool gateOpen() const { return stats_.gateOpen; }
    [[nodiscard]] bool transmitting() const { return stats_.transmitting; }
    [[nodiscard]] double latencyMs() const { return stats_.estimatedLatencyMs; }
    [[nodiscard]] QVariantList latencyBreakdown() const;
    [[nodiscard]] double processingLoad() const { return stats_.processingLoad; }

signals:
    void runningChanged();
    void devicesChanged();
    void settingsChanged();
    void metersChanged();
    void soundFinished(quint32 id);
    void speechFinished();

private:
    /// Maps -60..0 dBFS to 0..1 for meter bars.
    [[nodiscard]] static double meterPosition(double db);
    [[nodiscard]] engine::DeviceSelection resolveSelection();
    void startEngine();
    void applySettingsToEngine();
    void check(const Status& status);
    void handleEvent(const engine::EngineEvent& event);
    /// Selects the first virtual cable on first use; true if it picked one.
    bool pickVirtualCableIfUnset();

    engine::AudioEngine& engine_;
    AppSettings& settings_;
    NotificationModel& notifications_;
    DeviceListModel inputDevices_;
    DeviceListModel virtualMicDevices_;
    DeviceListModel monitorDevices_;
    QTimer pollTimer_;
    bool checkPermission_ = true;
    engine::EngineStats stats_;
    double inputDb_ = -120.0;
    double outputDb_ = -120.0;
};

} // namespace vox::app
