#pragma once

#include <vox/core/result.hpp>
#include <vox/engine/types.hpp>

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace vox::app {

/// A user's adjustments to one voice (macro sliders and tone).
struct VoiceUserSettings {
    std::vector<float> macroPositions;
    float bassDb = 0.0F;
    float trebleDb = 0.0F;
};

/// Everything the app remembers between runs.
struct AppSettings {
    // Devices. An empty id means the system default device.
    QString inputId;
    QString inputName; ///< For messages when the saved device is missing.
    QString virtualMicId;
    QString virtualMicName;
    QString monitorId;
    QString monitorName;
    bool useVirtualMic = true;
    bool virtualMicChosen = false; ///< False until the user (or auto-detection) picked one.
    int periodFrames = 128;
    bool exclusive = false;

    // Input and monitoring.
    bool hearMyself = false;
    bool voiceEnabled = true;
    bool backgroundEnabled = true;
    bool noiseReduction = false;
    float noiseReductionStrength = 1.0F;
    bool gate = false;
    float gateThresholdDb = -50.0F;
    float inputGainDb = 0.0F;
    engine::MixLevels mix;

    // Transmit (mute is deliberately not saved: starting muted surprises).
    engine::TransmitMode transmitMode = engine::TransmitMode::AlwaysOn;
    float releaseDelayMs = 150.0F;

    /// Global hotkeys for system actions: action key to portable key text.
    QHash<QString, QString> hotkeys;

    // Desktop integration.
    bool firstRunDone = false;
    bool closeToTray = true;
    bool startMinimized = false;

    // Neural voice (VOX_ENABLE_ML builds).
    QString neuralModelPath;

    // Text to speech.
    QString speechVoice; ///< Name of the system voice; empty for the default.
    bool speechThroughVoice = false;

    // Voices.
    QString currentVoiceId = QStringLiteral("clean-voice");
    QStringList favorites;
    QHash<QString, VoiceUserSettings> voices;
};

/// Reads and writes AppSettings as JSON. Writes are atomic (a temporary
/// file renamed over the old one), so a crash never leaves half a file.
class SettingsStore {
public:
    explicit SettingsStore(QString path);

    struct Loaded {
        AppSettings settings;
        /// Set when the file existed but could not be used; the app starts
        /// from defaults and tells the user.
        std::optional<Error> problem;
    };
    /// A missing file gives defaults without a problem (first run). A
    /// damaged file is kept next to the original as "<name>.damaged" so the
    /// user can recover it.
    [[nodiscard]] Loaded load() const;
    [[nodiscard]] Status save(const AppSettings& settings) const;

    [[nodiscard]] const QString& path() const noexcept { return path_; }
    /// settings.json in the platform's per-user configuration folder.
    [[nodiscard]] static QString defaultPath();

private:
    QString path_;
};

} // namespace vox::app
