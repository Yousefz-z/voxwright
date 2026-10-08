#include "voice_controller.hpp"

#include <QSet>

#include <algorithm>

namespace vox::app {

VoiceController::VoiceController(engine::AudioEngine& engine,
                                 const plugins::EffectRegistry& registry,
                                 const std::vector<plugins::VoicePreset>& presets,
                                 AppSettings& settings, NotificationModel& notifications,
                                 QObject* parent)
    : QObject(parent)
    , engine_(engine)
    , registry_(registry)
    , presets_(presets)
    , settings_(settings)
    , notifications_(notifications) {
    list_.setVoices(&presets_);
    list_.setFavorites(QSet<QString>(settings_.favorites.begin(), settings_.favorites.end()));
    filter_.setSourceModel(&list_);
}

void VoiceController::initialize() {
    if (presets_.empty()) {
        return;
    }
    if (!selectVoice(settings_.currentVoiceId)) {
        static_cast<void>(selectVoice(QString::fromStdString(presets_.front().id)));
    }
}

const plugins::VoicePreset* VoiceController::find(const QString& id) const {
    const std::string key = id.toStdString();
    const auto it = std::find_if(presets_.begin(), presets_.end(),
                                 [&key](const plugins::VoicePreset& p) { return p.id == key; });
    return it == presets_.end() ? nullptr : &*it;
}

VoiceUserSettings VoiceController::userSettings(const plugins::VoicePreset& preset) const {
    VoiceUserSettings s = settings_.voices.value(QString::fromStdString(preset.id));
    s.macroPositions.resize(preset.macros.size(), -1.0F);
    for (std::size_t i = 0; i < preset.macros.size(); ++i) {
        if (s.macroPositions[i] < 0.0F || s.macroPositions[i] > 1.0F) {
            s.macroPositions[i] = preset.macros[i].defaultPosition;
        }
    }
    return s;
}

bool VoiceController::selectVoice(const QString& id) {
    const plugins::VoicePreset* preset = find(id);
    if (preset == nullptr) {
        return false;
    }
    const VoiceUserSettings user = userSettings(*preset);
    plugins::VoiceSettings voiceSettings;
    voiceSettings.macroPositions = user.macroPositions;
    voiceSettings.bassDb = user.bassDb;
    voiceSettings.trebleDb = user.trebleDb;
    voiceSettings.backgroundEnabled = settings_.backgroundEnabled;
    if (auto set = engine_.setVoice(*preset, voiceSettings, registry_); !set) {
        notifications_.post(QStringLiteral("voice"), NotificationModel::Level::Error,
                            tr("Voice could not be loaded"),
                            QString::fromStdString(set.error().message));
        return false;
    }
    notifications_.dismiss(QStringLiteral("voice"));
    settings_.currentVoiceId = id;
    list_.setActive(id);
    emit currentVoiceChanged();
    emit macrosChanged();
    emit toneChanged();
    emit favoritesChanged();
    emit settingsChanged();
    return true;
}

void VoiceController::setMacro(int index, double position) {
    const plugins::VoicePreset* preset = current();
    if (preset == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= preset->macros.size()) {
        return;
    }
    const auto pos = static_cast<float>(std::clamp(position, 0.0, 1.0));
    VoiceUserSettings user = userSettings(*preset);
    user.macroPositions[static_cast<std::size_t>(index)] = pos;
    settings_.voices.insert(settings_.currentVoiceId, user);
    for (const auto& change :
         plugins::evaluateMacro(*preset, static_cast<std::size_t>(index), pos, registry_)) {
        if (auto sent = engine_.setVoiceParameter(change.block, change.param, change.value);
            !sent) {
            notifications_.post(QStringLiteral("engine-busy"), NotificationModel::Level::Warning,
                                tr("A change was not applied"),
                                QString::fromStdString(sent.error().message));
            break;
        }
    }
    emit macrosChanged();
    emit settingsChanged();
}

void VoiceController::setTone(double bassDb, double trebleDb) {
    const plugins::VoicePreset* preset = current();
    if (preset == nullptr) {
        return;
    }
    VoiceUserSettings user = userSettings(*preset);
    user.bassDb = static_cast<float>(std::clamp(bassDb, -12.0, 12.0));
    user.trebleDb = static_cast<float>(std::clamp(trebleDb, -12.0, 12.0));
    settings_.voices.insert(settings_.currentVoiceId, user);
    if (auto sent = engine_.setVoiceTone(user.bassDb, user.trebleDb); !sent) {
        notifications_.post(QStringLiteral("engine-busy"), NotificationModel::Level::Warning,
                            tr("A change was not applied"),
                            QString::fromStdString(sent.error().message));
    }
    emit toneChanged();
    emit settingsChanged();
}

void VoiceController::resetCurrentVoice() {
    settings_.voices.remove(settings_.currentVoiceId);
    static_cast<void>(selectVoice(settings_.currentVoiceId));
}

void VoiceController::toggleFavorite(const QString& id) {
    if (find(id) == nullptr) {
        return;
    }
    if (settings_.favorites.contains(id)) {
        settings_.favorites.removeAll(id);
    } else {
        settings_.favorites.append(id);
    }
    list_.setFavorites(QSet<QString>(settings_.favorites.begin(), settings_.favorites.end()));
    emit favoritesChanged();
    emit settingsChanged();
}

bool VoiceController::isFavorite(const QString& id) const {
    return settings_.favorites.contains(id);
}

QStringList VoiceController::categories() const {
    QStringList out;
    for (const auto& c : plugins::voiceCategories()) {
        const bool used =
            std::any_of(presets_.begin(), presets_.end(),
                        [&c](const plugins::VoicePreset& p) { return p.category == c; });
        if (used) {
            out.append(QString::fromStdString(c));
        }
    }
    return out;
}

QString VoiceController::currentName() const {
    const auto* p = current();
    return p != nullptr ? QString::fromStdString(p->name) : QString{};
}

QString VoiceController::currentDescription() const {
    const auto* p = current();
    return p != nullptr ? QString::fromStdString(p->description) : QString{};
}

QString VoiceController::currentCategory() const {
    const auto* p = current();
    return p != nullptr ? QString::fromStdString(p->category) : QString{};
}

QString VoiceController::currentIcon() const {
    const auto* p = current();
    return p != nullptr ? QString::fromStdString(p->icon) : QString{};
}

QString VoiceController::currentColor() const {
    const auto* p = current();
    return p != nullptr ? QString::fromStdString(p->color) : QString{};
}

QVariantList VoiceController::macros() const {
    QVariantList out;
    const auto* p = current();
    if (p == nullptr) {
        return out;
    }
    const VoiceUserSettings user = userSettings(*p);
    for (std::size_t i = 0; i < p->macros.size(); ++i) {
        out.append(
            QVariantMap{{QStringLiteral("name"), QString::fromStdString(p->macros[i].name)},
                        {QStringLiteral("value"), static_cast<double>(user.macroPositions[i])}});
    }
    return out;
}

double VoiceController::bassDb() const {
    const auto* p = current();
    return p != nullptr ? static_cast<double>(userSettings(*p).bassDb) : 0.0;
}

double VoiceController::trebleDb() const {
    const auto* p = current();
    return p != nullptr ? static_cast<double>(userSettings(*p).trebleDb) : 0.0;
}

} // namespace vox::app
