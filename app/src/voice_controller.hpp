#pragma once

#include "notification_model.hpp"
#include "settings.hpp"
#include "voice_models.hpp"

#include <vox/engine/audio_engine.hpp>
#include <vox/plugins/registry.hpp>
#include <vox/plugins/voice_preset.hpp>

#include <QObject>
#include <QRandomGenerator>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace vox::app {

/// Voice browsing and live switching: the filtered voice grid, the active
/// voice and its macro and tone sliders, and favorites.
class VoiceController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")

    Q_PROPERTY(vox::app::VoiceFilterModel* voices READ voices CONSTANT)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(int voiceCount READ voiceCount CONSTANT)
    Q_PROPERTY(QString currentVoiceId READ currentVoiceId NOTIFY currentVoiceChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentVoiceChanged)
    Q_PROPERTY(QString currentDescription READ currentDescription NOTIFY currentVoiceChanged)
    Q_PROPERTY(QString currentCategory READ currentCategory NOTIFY currentVoiceChanged)
    Q_PROPERTY(QString currentIcon READ currentIcon NOTIFY currentVoiceChanged)
    Q_PROPERTY(QString currentColor READ currentColor NOTIFY currentVoiceChanged)
    Q_PROPERTY(bool currentFavorite READ currentFavorite NOTIFY favoritesChanged)
    Q_PROPERTY(QVariantList macros READ macros NOTIFY macrosChanged)
    Q_PROPERTY(double bassDb READ bassDb NOTIFY toneChanged)
    Q_PROPERTY(double trebleDb READ trebleDb NOTIFY toneChanged)

public:
    VoiceController(engine::AudioEngine& engine, const plugins::EffectRegistry& registry,
                    const std::vector<plugins::VoicePreset>& presets, AppSettings& settings,
                    NotificationModel& notifications, QObject* parent = nullptr);

    /// Activates the saved voice (or the first one if it no longer exists).
    void initialize();

    Q_INVOKABLE bool selectVoice(const QString& id);
    /// Moves macro slider `index` of the active voice to `position` (0..1).
    Q_INVOKABLE void setMacro(int index, double position);
    Q_INVOKABLE void setTone(double bassDb, double trebleDb);
    /// Macros and tone of the active voice back to the preset's defaults.
    Q_INVOKABLE void resetCurrentVoice();
    /// Steps through the voices in list order (wrapping); `delta` is +1 or -1.
    Q_INVOKABLE void selectRelative(int delta);
    /// Any voice other than the current one.
    Q_INVOKABLE void selectRandom();
    Q_INVOKABLE void toggleFavorite(const QString& id);
    Q_INVOKABLE bool isFavorite(const QString& id) const;

    [[nodiscard]] VoiceFilterModel* voices() { return &filter_; }
    [[nodiscard]] QStringList categories() const;
    [[nodiscard]] int voiceCount() const { return static_cast<int>(presets_.size()); }
    [[nodiscard]] QString currentVoiceId() const { return settings_.currentVoiceId; }
    [[nodiscard]] QString currentName() const;
    [[nodiscard]] QString currentDescription() const;
    [[nodiscard]] QString currentCategory() const;
    [[nodiscard]] QString currentIcon() const;
    [[nodiscard]] QString currentColor() const;
    [[nodiscard]] bool currentFavorite() const { return isFavorite(settings_.currentVoiceId); }
    [[nodiscard]] QVariantList macros() const;
    [[nodiscard]] double bassDb() const;
    [[nodiscard]] double trebleDb() const;

signals:
    void currentVoiceChanged();
    void macrosChanged();
    void toneChanged();
    void favoritesChanged();
    void settingsChanged();

private:
    [[nodiscard]] const plugins::VoicePreset* find(const QString& id) const;
    [[nodiscard]] const plugins::VoicePreset* current() const {
        return find(settings_.currentVoiceId);
    }
    /// The user's settings for `preset`, filled with defaults where missing.
    [[nodiscard]] VoiceUserSettings userSettings(const plugins::VoicePreset& preset) const;

    engine::AudioEngine& engine_;
    const plugins::EffectRegistry& registry_;
    const std::vector<plugins::VoicePreset>& presets_;
    AppSettings& settings_;
    NotificationModel& notifications_;
    VoiceListModel list_;
    VoiceFilterModel filter_;
    QRandomGenerator random_{QRandomGenerator::securelySeeded()};
};

} // namespace vox::app
