#include "hotkey_controller.hpp"

#include "hotkeys/hotkey_keys.hpp"

#include <vox/engine/transmit_control.hpp>

#include <QKeySequence>

#include <algorithm>
#include <array>

namespace vox::app {
namespace {

constexpr std::array kActions{
    HotkeyController::Action::VoiceChanger,  HotkeyController::Action::HearMyself,
    HotkeyController::Action::Mute,          HotkeyController::Action::Talk,
    HotkeyController::Action::Censor,        HotkeyController::Action::Background,
    HotkeyController::Action::StopSounds,    HotkeyController::Action::NextVoice,
    HotkeyController::Action::PreviousVoice, HotkeyController::Action::RandomVoice,
};

QKeyCombination combinationOf(const QString& sequence) {
    const QKeySequence keys = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    return keys.isEmpty() ? QKeyCombination() : keys[0];
}

bool sameKeys(const QString& a, const QString& b) {
    return !a.isEmpty() && !b.isEmpty() && combinationOf(a) == combinationOf(b);
}

/// Voice hotkeys share the settings map with actions, under this prefix.
QString voicePrefix() {
    return QStringLiteral("voice:");
}

} // namespace

HotkeyController::HotkeyController(GlobalHotkeys& keys, engine::TransmitControl& transmit,
                                   AppSettings& settings, NotificationModel& notifications,
                                   QObject* parent)
    : QObject(parent)
    , keys_(keys)
    , settings_(settings)
    , notifications_(notifications) {
    // Push-to-talk and the censor key act on the key's own thread so they
    // never wait for the UI.
    keys_.setImmediateHandler([&transmit](int id, bool pressed) {
        if (id == bindingId(Action::Talk)) {
            transmit.setTalkKeyDown(pressed);
        } else if (id == bindingId(Action::Censor)) {
            transmit.setCensorKeyDown(pressed);
        }
    });
    connect(&keys_, &GlobalHotkeys::activated, this, &HotkeyController::onActivated);
}

void HotkeyController::initialize() {
    if (auto registered = registerAll(); !registered) {
        notifications_.post(QStringLiteral("hotkeys"), NotificationModel::Level::Warning,
                            tr("Some hotkeys are not active"),
                            QString::fromStdString(registered.error().message), tr("Hotkeys"),
                            QStringLiteral("open-hotkeys"));
    }
}

QString HotkeyController::actionKey(Action action) {
    switch (action) {
    case Action::VoiceChanger:
        return QStringLiteral("voiceChanger");
    case Action::HearMyself:
        return QStringLiteral("hearMyself");
    case Action::Mute:
        return QStringLiteral("mute");
    case Action::Talk:
        return QStringLiteral("talk");
    case Action::Background:
        return QStringLiteral("background");
    case Action::StopSounds:
        return QStringLiteral("stopSounds");
    case Action::NextVoice:
        return QStringLiteral("nextVoice");
    case Action::PreviousVoice:
        return QStringLiteral("previousVoice");
    case Action::RandomVoice:
        return QStringLiteral("randomVoice");
    case Action::Censor:
        return QStringLiteral("censor");
    }
    return {};
}

QString HotkeyController::actionLabel(Action action) {
    switch (action) {
    case Action::VoiceChanger:
        return tr("Voice changer on or off");
    case Action::HearMyself:
        return tr("Hear myself on or off");
    case Action::Mute:
        return tr("Mute or unmute the microphone");
    case Action::Talk:
        return tr("Push-to-talk key (hold)");
    case Action::Background:
        return tr("Background effects on or off");
    case Action::StopSounds:
        return tr("Stop all sounds");
    case Action::NextVoice:
        return tr("Next voice");
    case Action::PreviousVoice:
        return tr("Previous voice");
    case Action::RandomVoice:
        return tr("Random voice");
    case Action::Censor:
        return tr("Censor beep (hold)");
    }
    return {};
}

QVariantList HotkeyController::actions() const {
    QVariantList out;
    for (const Action a : kActions) {
        const QString sequence = settings_.hotkeys.value(actionKey(a));
        out.append(QVariantMap{{QStringLiteral("action"), actionKey(a)},
                               {QStringLiteral("label"), actionLabel(a)},
                               {QStringLiteral("sequence"), sequence},
                               {QStringLiteral("display"), displayText(sequence)}});
    }
    return out;
}

QString HotkeyController::sequenceFromKey(int key, int modifiers) {
    const auto k = static_cast<Qt::Key>(key);
    if (isModifierKey(k) || key == 0 || key == Qt::Key_unknown) {
        return {};
    }
    const QKeyCombination combination(
        Qt::KeyboardModifiers(static_cast<Qt::KeyboardModifier>(modifiers)) &
            (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier |
             Qt::KeypadModifier),
        k);
    return QKeySequence(combination).toString(QKeySequence::PortableText);
}

QString HotkeyController::displayText(const QString& sequence) {
    return QKeySequence::fromString(sequence, QKeySequence::PortableText)
        .toString(QKeySequence::NativeText);
}

QString HotkeyController::validate(const QString& sequence) const {
    if (!keys_.supported()) {
        return tr("Global hotkeys are not available on this system.");
    }
    const QKeySequence keys = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    if (keys.count() != 1 || isModifierKey(keys[0].key())) {
        return tr("A hotkey is one key, optionally with Ctrl, Alt, Shift, or the system key.");
    }
#if defined(Q_OS_MACOS)
    const bool mappable = macVirtualKey(keys[0]).has_value();
#else
    const bool mappable = windowsVirtualKey(keys[0]).has_value();
#endif
    if (!mappable) {
        return tr("%1 cannot be a global hotkey. Choose a letter, number, or function key.")
            .arg(displayText(sequence));
    }
    return {};
}

QString HotkeyController::ownerOf(const QString& sequence, const QString& except) const {
    for (const Action a : kActions) {
        if (actionKey(a) != except && sameKeys(settings_.hotkeys.value(actionKey(a)), sequence)) {
            return actionLabel(a);
        }
    }
    for (auto it = soundKeys_.cbegin(); it != soundKeys_.cend(); ++it) {
        const QString owner = QStringLiteral("sound:%1").arg(it.key());
        if (owner != except && sameKeys(it.value(), sequence)) {
            const QString name = soundName_ ? soundName_(it.key()) : QString{};
            return name.isEmpty() ? tr("a soundboard sound") : tr("the sound \"%1\"").arg(name);
        }
    }
    for (auto it = settings_.hotkeys.cbegin(); it != settings_.hotkeys.cend(); ++it) {
        if (it.key().startsWith(voicePrefix()) && it.key() != except &&
            sameKeys(it.value(), sequence)) {
            const QString id = it.key().mid(voicePrefix().size());
            const QString name = voiceName_ ? voiceName_(id) : QString{};
            return tr("the voice \"%1\"").arg(name.isEmpty() ? id : name);
        }
    }
    return {};
}

Status HotkeyController::registerAll() {
    std::vector<HotkeyBinding> bindings;
    for (const Action a : kActions) {
        const QString sequence = settings_.hotkeys.value(actionKey(a));
        if (!sequence.isEmpty()) {
            bindings.push_back({bindingId(a), combinationOf(sequence)});
        }
    }
    for (auto it = soundKeys_.cbegin(); it != soundKeys_.cend(); ++it) {
        if (!it.value().isEmpty()) {
            bindings.push_back(
                {kSoundIdBase + static_cast<int>(it.key()), combinationOf(it.value())});
        }
    }
    voiceOrder_.clear();
    for (auto it = settings_.hotkeys.cbegin(); it != settings_.hotkeys.cend(); ++it) {
        if (it.key().startsWith(voicePrefix()) && !it.value().isEmpty() &&
            voiceOrder_.size() < kSoundIdBase - kVoiceIdBase) {
            bindings.push_back(
                {kVoiceIdBase + static_cast<int>(voiceOrder_.size()), combinationOf(it.value())});
            voiceOrder_.append(it.key().mid(voicePrefix().size()));
        }
    }
    return keys_.setBindings(bindings);
}

QString HotkeyController::assignKey(const QString& owner, const QString& sequence,
                                    const QString& previous,
                                    const std::function<void(const QString&)>& store) {
    if (!sequence.isEmpty()) {
        if (QString problem = validate(sequence); !problem.isEmpty()) {
            return problem;
        }
        if (const QString other = ownerOf(sequence, owner); !other.isEmpty()) {
            return tr("%1 is already used for %2.").arg(displayText(sequence), other);
        }
    }
    store(sequence);
    if (auto registered = registerAll(); !registered) {
        store(previous);
        static_cast<void>(registerAll());
        return QString::fromStdString(registered.error().message);
    }
    emit bindingsChanged();
    return {};
}

QString HotkeyController::assignAction(const QString& action, const QString& sequence) {
    if (std::ranges::none_of(kActions, [&](Action a) { return actionKey(a) == action; })) {
        return tr("Unknown action \"%1\".").arg(action);
    }
    QString problem =
        assignKey(action, sequence, settings_.hotkeys.value(action),
                  [&](const QString& text) { settings_.hotkeys.insert(action, text); });
    if (problem.isEmpty()) {
        notifications_.dismiss(QStringLiteral("hotkeys"));
    }
    return problem;
}

QString HotkeyController::assignSound(quint32 soundId, const QString& sequence) {
    return assignKey(QStringLiteral("sound:%1").arg(soundId), sequence, soundKeys_.value(soundId),
                     [&](const QString& text) { soundKeys_.insert(soundId, text); });
}

QString HotkeyController::assignVoice(const QString& voiceId, const QString& sequence) {
    if (voiceId.isEmpty()) {
        return tr("Choose a voice first.");
    }
    const QString key = voicePrefix() + voiceId;
    return assignKey(key, sequence, settings_.hotkeys.value(key), [&](const QString& text) {
        if (text.isEmpty()) {
            settings_.hotkeys.remove(key);
        } else {
            settings_.hotkeys.insert(key, text);
        }
    });
}

QString HotkeyController::voiceHotkey(const QString& voiceId) const {
    return settings_.hotkeys.value(voicePrefix() + voiceId);
}

void HotkeyController::forgetSound(quint32 soundId) {
    if (soundKeys_.remove(soundId)) {
        static_cast<void>(registerAll());
        emit bindingsChanged();
    }
}

void HotkeyController::restoreSoundHotkeys(const QHash<quint32, QString>& hotkeys) {
    QStringList dropped;
    for (auto it = hotkeys.cbegin(); it != hotkeys.cend(); ++it) {
        const QString owner = QStringLiteral("sound:%1").arg(it.key());
        if (it.value().isEmpty()) {
            continue;
        }
        if (!validate(it.value()).isEmpty() || !ownerOf(it.value(), owner).isEmpty()) {
            dropped.append(displayText(it.value()));
            continue;
        }
        soundKeys_.insert(it.key(), it.value());
    }
    if (!dropped.isEmpty()) {
        notifications_.post(QStringLiteral("sound-hotkeys"), NotificationModel::Level::Warning,
                            tr("Some sound hotkeys were removed"),
                            tr("%1 conflicted with other hotkeys or cannot be used here.")
                                .arg(dropped.join(QStringLiteral(", "))));
    }
    initialize();
}

void HotkeyController::onActivated(int id, bool pressed) {
    if (id >= kSoundIdBase) {
        emit soundTriggered(static_cast<quint32>(id - kSoundIdBase), pressed);
        return;
    }
    if (id >= kVoiceIdBase) {
        const auto index = static_cast<qsizetype>(id - kVoiceIdBase);
        if (pressed && index < voiceOrder_.size()) {
            emit voiceTriggered(voiceOrder_.at(index));
        }
        return;
    }
    // Hold actions were handled on the key's thread; the rest act on press.
    const auto action = static_cast<Action>(id - 1);
    if (pressed && action != Action::Talk && action != Action::Censor) {
        emit actionTriggered(action);
    }
}

} // namespace vox::app
