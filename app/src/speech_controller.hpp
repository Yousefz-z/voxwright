#pragma once

#include "notification_model.hpp"
#include "settings.hpp"

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <span>
#include <vector>

class QTextToSpeech;

namespace vox::engine {
class AudioEngine;
}

namespace vox::app {

/// Text to speech into the virtual microphone: the system's voices render
/// the text to audio (QTextToSpeech::synthesize), which the engine plays on
/// its speech channel, optionally through the current voice effect.
class SpeechController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// Why speech is unavailable, in words for the user.
    Q_PROPERTY(QString problem READ problem NOTIFY availableChanged)
    Q_PROPERTY(QStringList voices READ voiceNames NOTIFY availableChanged)
    Q_PROPERTY(int voiceIndex READ voiceIndex WRITE setVoiceIndex NOTIFY voiceIndexChanged)
    Q_PROPERTY(bool throughVoice READ throughVoice WRITE setThroughVoice NOTIFY throughVoiceChanged)
    /// Rendering or playing.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    /// Longest text spoken at once.
    static constexpr int kMaxCharacters = 1000;

    /// `engineName` picks a QTextToSpeech engine; empty for the system's.
    SpeechController(engine::AudioEngine& engine, AppSettings& settings,
                     NotificationModel& notifications, const QString& engineName = {},
                     QObject* parent = nullptr);
    SpeechController(const SpeechController&) = delete;
    SpeechController& operator=(const SpeechController&) = delete;
    SpeechController(SpeechController&&) = delete;
    SpeechController& operator=(SpeechController&&) = delete;
    ~SpeechController() override;

    /// Starts speaking; returns "" or why not.
    Q_INVOKABLE QString speak(const QString& text);
    Q_INVOKABLE void stop();

    [[nodiscard]] bool available() const;
    [[nodiscard]] QString problem() const { return problem_; }
    [[nodiscard]] QStringList voiceNames() const;
    [[nodiscard]] int voiceIndex() const;
    void setVoiceIndex(int index);
    [[nodiscard]] bool throughVoice() const { return settings_.speechThroughVoice; }
    void setThroughVoice(bool on);
    [[nodiscard]] bool busy() const { return rendering_ || playing_; }
    /// The speech engine is still producing audio for the last text.
    [[nodiscard]] bool rendering() const { return rendering_; }

    /// The engine finished playing everything queued.
    void onSpeechFinished();

    /// Converts rendered audio (any QAudioFormat) to mono float at 48 kHz.
    [[nodiscard]] static std::vector<float> toEngineFormat(const QAudioFormat& format,
                                                           const QByteArray& pcm);

signals:
    void availableChanged();
    void voiceIndexChanged();
    void throughVoiceChanged();
    void busyChanged();
    void settingsChanged();

private:
    void onStateChanged();
    void finishRendering();
    void setBusy(bool rendering, bool playing);

    engine::AudioEngine& engine_;
    AppSettings& settings_;
    NotificationModel& notifications_;
    std::unique_ptr<QTextToSpeech> tts_;
    QString problem_;
    QAudioFormat format_;
    QByteArray pcm_;
    bool rendering_ = false;
    bool playing_ = false;
};

} // namespace vox::app
