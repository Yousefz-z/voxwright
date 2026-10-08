#include "speech_controller.hpp"

#include <vox/dsp/resampler.hpp>
#include <vox/engine/audio_engine.hpp>
#include <vox/engine/types.hpp>

#include <QTextToSpeech>
#include <QVoice>

#include <algorithm>
#include <cstring>

namespace vox::app {
namespace {

template <class Sample>
float sampleAt(const char* data, std::size_t index, float scale, float offset) {
    Sample s{};
    std::memcpy(&s, data + index * sizeof(Sample), sizeof(Sample));
    return (static_cast<float>(s) - offset) * scale;
}

} // namespace

SpeechController::SpeechController(engine::AudioEngine& engine, AppSettings& settings,
                                   NotificationModel& notifications, const QString& engineName,
                                   QObject* parent)
    : QObject(parent)
    , engine_(engine)
    , settings_(settings)
    , notifications_(notifications)
    , tts_(engineName.isEmpty() ? std::make_unique<QTextToSpeech>()
                                : std::make_unique<QTextToSpeech>(engineName)) {
    connect(tts_.get(), &QTextToSpeech::stateChanged, this, &SpeechController::onStateChanged);
    onStateChanged();
}

SpeechController::~SpeechController() = default;

bool SpeechController::available() const {
    return problem_.isEmpty() && tts_->state() != QTextToSpeech::Error &&
           tts_->engineCapabilities().testFlag(QTextToSpeech::Capability::Synthesize) &&
           !tts_->availableVoices().isEmpty();
}

QStringList SpeechController::voiceNames() const {
    QStringList names;
    for (const QVoice& v : tts_->availableVoices()) {
        names.append(
            QStringLiteral("%1 (%2)").arg(v.name(), QLocale::languageToString(v.language())));
    }
    return names;
}

int SpeechController::voiceIndex() const {
    const auto voices = tts_->availableVoices();
    for (qsizetype i = 0; i < voices.size(); ++i) {
        if (voices[i].name() == settings_.speechVoice) {
            return static_cast<int>(i);
        }
    }
    return voices.isEmpty() ? -1 : 0;
}

void SpeechController::setVoiceIndex(int index) {
    const auto voices = tts_->availableVoices();
    if (index < 0 || index >= voices.size() || voices[index].name() == settings_.speechVoice) {
        return;
    }
    settings_.speechVoice = voices[index].name();
    tts_->setVoice(voices[index]);
    emit voiceIndexChanged();
    emit settingsChanged();
}

void SpeechController::setThroughVoice(bool on) {
    if (on == settings_.speechThroughVoice) {
        return;
    }
    settings_.speechThroughVoice = on;
    emit throughVoiceChanged();
    emit settingsChanged();
}

void SpeechController::onStateChanged() {
    const QTextToSpeech::State state = tts_->state();
    QString problem;
    if (state == QTextToSpeech::Error) {
        problem = tr("Text to speech stopped working: %1").arg(tts_->errorString());
    } else if (state != QTextToSpeech::Ready && state != QTextToSpeech::Speaking &&
               state != QTextToSpeech::Synthesizing && state != QTextToSpeech::Paused) {
        return;
    } else if (!tts_->engineCapabilities().testFlag(QTextToSpeech::Capability::Synthesize)) {
        problem = tr("The system's speech engine (%1) cannot render speech for the virtual "
                     "microphone.")
                      .arg(tts_->engine());
    } else if (tts_->availableVoices().isEmpty()) {
        problem = tr("No speech voices are installed. Add a voice in the system's language "
                     "settings, then restart Voxwright.");
    }
    if (problem != problem_) {
        problem_ = problem;
        if (problem_.isEmpty()) {
            const int index = voiceIndex();
            if (index >= 0) {
                tts_->setVoice(tts_->availableVoices()[index]);
            }
        }
        emit availableChanged();
        emit voiceIndexChanged();
    }
    if (rendering_ && state == QTextToSpeech::Ready) {
        finishRendering();
    } else if (rendering_ && state == QTextToSpeech::Error) {
        setBusy(false, playing_);
        notifications_.post(QStringLiteral("speech"), NotificationModel::Level::Error,
                            tr("Text could not be spoken"), problem_);
    }
}

QString SpeechController::speak(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return tr("Type something to say.");
    }
    if (trimmed.size() > kMaxCharacters) {
        return tr("That is too long to say at once. Keep it under %1 characters.")
            .arg(kMaxCharacters);
    }
    if (!available()) {
        return problem_.isEmpty() ? tr("Text to speech is still starting. Try again in a moment.")
                                  : problem_;
    }
    if (rendering_) {
        return tr("Still preparing the last text.");
    }
    pcm_.clear();
    format_ = {};
    setBusy(true, playing_);
    tts_->synthesize(trimmed, this, [this](const QAudioFormat& format, const QByteArray& chunk) {
        format_ = format;
        pcm_.append(chunk);
    });
    return {};
}

void SpeechController::finishRendering() {
    std::vector<float> samples = toEngineFormat(format_, pcm_);
    pcm_.clear();
    if (samples.empty()) {
        setBusy(false, playing_);
        notifications_.post(QStringLiteral("speech"), NotificationModel::Level::Warning,
                            tr("Nothing was spoken"),
                            tr("The speech engine returned no audio for this text. Try another "
                               "voice."));
        return;
    }
    if (auto played = engine_.playSpeech(std::move(samples), settings_.speechThroughVoice);
        !played) {
        setBusy(false, playing_);
        notifications_.post(QStringLiteral("speech"), NotificationModel::Level::Error,
                            tr("Text could not be spoken"),
                            QString::fromStdString(played.error().message));
        return;
    }
    notifications_.dismiss(QStringLiteral("speech"));
    setBusy(false, true);
}

void SpeechController::stop() {
    if (rendering_) {
        tts_->stop(QTextToSpeech::BoundaryHint::Immediate);
    }
    static_cast<void>(engine_.stopSpeech());
    pcm_.clear();
    setBusy(false, false);
}

void SpeechController::onSpeechFinished() {
    setBusy(rendering_, false);
}

void SpeechController::setBusy(bool rendering, bool playing) {
    const bool was = busy();
    rendering_ = rendering;
    playing_ = playing;
    if (busy() != was) {
        emit busyChanged();
    }
}

std::vector<float> SpeechController::toEngineFormat(const QAudioFormat& format,
                                                    const QByteArray& pcm) {
    const int channels = format.channelCount();
    const int bytes = format.bytesPerSample();
    if (!format.isValid() || channels <= 0 || bytes <= 0 || pcm.isEmpty()) {
        return {};
    }
    const auto frames =
        static_cast<std::size_t>(pcm.size()) / static_cast<std::size_t>(channels * bytes);
    std::vector<float> mono(frames, 0.0F);
    const char* data = pcm.constData();
    const float share = 1.0F / static_cast<float>(channels);
    for (std::size_t f = 0; f < frames; ++f) {
        float sum = 0.0F;
        for (int c = 0; c < channels; ++c) {
            const std::size_t i =
                f * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c);
            switch (format.sampleFormat()) {
            case QAudioFormat::UInt8:
                sum += sampleAt<std::uint8_t>(data, i, 1.0F / 128.0F, 128.0F);
                break;
            case QAudioFormat::Int16:
                sum += sampleAt<std::int16_t>(data, i, 1.0F / 32768.0F, 0.0F);
                break;
            case QAudioFormat::Int32:
                sum += sampleAt<std::int32_t>(data, i, 1.0F / 2147483648.0F, 0.0F);
                break;
            case QAudioFormat::Float:
                sum += sampleAt<float>(data, i, 1.0F, 0.0F);
                break;
            default:
                return {};
            }
        }
        mono[f] = sum * share;
    }
    const double sourceRate = format.sampleRate();
    if (sourceRate <= 0.0) {
        return {};
    }
    if (sourceRate == engine::kEngineRate) {
        return mono;
    }
    return dsp::resample(mono, sourceRate, engine::kEngineRate);
}

} // namespace vox::app
