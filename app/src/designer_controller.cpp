#include "designer_controller.hpp"

#include "voice_controller.hpp"

#include <vox/engine/audio_engine.hpp>
#include <vox/plugins/voice_chain.hpp>
#include <vox/plugins/voice_edit.hpp>

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <utility>

namespace vox::app {
namespace {

QString toQString(const std::string& s) {
    return QString::fromStdString(s);
}

QString kindName(plugins::ParamKind kind) {
    switch (kind) {
    case plugins::ParamKind::Toggle:
        return QStringLiteral("toggle");
    case plugins::ParamKind::Choice:
        return QStringLiteral("choice");
    case plugins::ParamKind::Continuous:
        break;
    }
    return QStringLiteral("continuous");
}

QString message(const Error& e) {
    return QString::fromStdString(e.message);
}

} // namespace

DesignerController::DesignerController(engine::AudioEngine& engine,
                                       const plugins::EffectRegistry& registry,
                                       VoiceController& voices, CustomVoiceStore& store,
                                       NotificationModel& notifications, QObject* parent)
    : QObject(parent)
    , engine_(engine)
    , registry_(registry)
    , voices_(voices)
    , store_(store)
    , notifications_(notifications) {
    connect(&voices_, &VoiceController::currentVoiceChanged, this,
            &DesignerController::onVoiceSelected);
    connect(&voices_, &VoiceController::voicesChanged, this,
            &DesignerController::customVoicesChanged);
}

QString DesignerController::newId() {
    QString id;
    do {
        id = QStringLiteral("custom-%1").arg(random_.generate(), 8, 16, QLatin1Char('0'));
    } while (voices_.preset(id) != nullptr);
    return id;
}

QString DesignerController::uniqueName(const QString& base) const {
    const auto taken = [this](const QString& name) {
        return std::ranges::any_of(voices_.presets(), [&name](const plugins::VoicePreset& p) {
            return toQString(p.name).compare(name, Qt::CaseInsensitive) == 0;
        });
    };
    QString name = base;
    for (int n = 2; taken(name); ++n) {
        name = QStringLiteral("%1 %2").arg(base).arg(n);
    }
    return name;
}

void DesignerController::begin(plugins::VoicePreset draft, bool isNew) {
    draft_ = std::move(draft);
    editing_ = true;
    isNew_ = isNew;
    dirty_ = isNew;
    emit editingChanged();
    emit dirtyChanged();
    emit detailsChanged();
    updateLatency();
    emit blocksChanged();
    emit macrosChanged();
    applyPreview();
}

void DesignerController::newVoice() {
    plugins::VoicePreset voice;
    voice.id = newId().toStdString();
    voice.name = uniqueName(tr("My voice")).toStdString();
    voice.category = "Character";
    voice.icon = "character";
    voice.color = colors().front().toStdString();
    static_cast<void>(plugins::insertBlock(voice, 0, "pitch", registry_));
    begin(std::move(voice), true);
}

bool DesignerController::editVoice(const QString& id) {
    const plugins::VoicePreset* source = voices_.preset(id);
    if (source == nullptr) {
        return false;
    }
    if (!source->builtIn) {
        begin(*source, false);
        return true;
    }
    plugins::VoicePreset copy = plugins::duplicateVoice(
        *source, newId().toStdString(),
        uniqueName(tr("%1 (mine)").arg(toQString(source->name))).toStdString());
    // The copy starts where the user has the quick sliders now.
    const std::vector<float> positions = voices_.macroPositions(id);
    for (std::size_t m = 0; m < copy.macros.size() && m < positions.size(); ++m) {
        copy.macros[m].defaultPosition = positions[m];
    }
    begin(std::move(copy), true);
    return true;
}

void DesignerController::setDirty() {
    if (!dirty_) {
        dirty_ = true;
        emit dirtyChanged();
    }
}

void DesignerController::setPreviewing(bool previewing) {
    if (previewing_ != previewing) {
        previewing_ = previewing;
        emit previewingChanged();
    }
}

void DesignerController::applyPreview() {
    if (!editing_) {
        return;
    }
    plugins::VoiceSettings settings;
    settings.backgroundEnabled = voices_.backgroundEnabled();
    if (auto set = engine_.setVoice(draft_, settings, registry_); !set) {
        notifications_.post(QStringLiteral("designer"), NotificationModel::Level::Error,
                            tr("The voice cannot play"), message(set.error()));
        setPreviewing(false);
        return;
    }
    notifications_.dismiss(QStringLiteral("designer"));
    setPreviewing(true);
}

void DesignerController::updateLatency() {
    auto chain = plugins::VoiceChain::build(draft_, {}, registry_, engine_.voiceContext());
    latencyMs_ = chain ? static_cast<double>(chain.value()->latencySamples()) * 1000.0 /
                             engine_.voiceContext().sampleRate
                       : 0.0;
}

void DesignerController::structureChanged() {
    setDirty();
    updateLatency();
    emit blocksChanged();
    emit macrosChanged();
    if (previewing_) {
        applyPreview();
    }
}

void DesignerController::onVoiceSelected() {
    // Choosing another voice (a hotkey, the voice grid) takes over the
    // engine; edits stay in the draft until the user listens again.
    if (editing_ && !selecting_) {
        setPreviewing(false);
    }
}

const plugins::ParamSpec* DesignerController::spec(int block, int param) const {
    if (block < 0 || param < 0 || static_cast<std::size_t>(block) >= draft_.blocks.size()) {
        return nullptr;
    }
    const plugins::EffectDescriptor* d =
        registry_.find(draft_.blocks[static_cast<std::size_t>(block)].effect);
    if (d == nullptr || static_cast<std::size_t>(param) >= d->params.size()) {
        return nullptr;
    }
    return &d->params[static_cast<std::size_t>(param)];
}

QString DesignerController::addBlock(const QString& effectId) {
    if (!editing_) {
        return tr("Start or open a voice first.");
    }
    if (auto added =
            plugins::insertBlock(draft_, draft_.blocks.size(), effectId.toStdString(), registry_);
        !added) {
        return message(added.error());
    }
    structureChanged();
    return {};
}

QString DesignerController::removeBlock(int block) {
    if (block < 0) {
        return tr("That effect is no longer part of this voice.");
    }
    if (auto removed = plugins::removeBlock(draft_, static_cast<std::size_t>(block)); !removed) {
        return message(removed.error());
    }
    structureChanged();
    return {};
}

QString DesignerController::moveBlock(int from, int to) {
    if (from < 0 || to < 0) {
        return tr("That effect is no longer part of this voice.");
    }
    if (auto moved = plugins::moveBlock(draft_, static_cast<std::size_t>(from),
                                        static_cast<std::size_t>(to));
        !moved) {
        return message(moved.error());
    }
    structureChanged();
    return {};
}

void DesignerController::setBypassed(int block, bool bypassed) {
    if (block < 0 || static_cast<std::size_t>(block) >= draft_.blocks.size()) {
        return;
    }
    draft_.blocks[static_cast<std::size_t>(block)].bypassed = bypassed;
    setDirty();
    updateLatency();
    if (previewing_) {
        static_cast<void>(engine_.setBlockBypassed(static_cast<std::size_t>(block), bypassed));
    }
}

double DesignerController::setParameter(int block, int param, double value) {
    const plugins::ParamSpec* s = spec(block, param);
    if (s == nullptr) {
        return 0.0;
    }
    const auto b = static_cast<std::size_t>(block);
    const auto p = static_cast<std::size_t>(param);
    auto stored = plugins::setBlockParameter(draft_, b, p, static_cast<float>(value), registry_);
    if (!stored) {
        return 0.0;
    }
    setDirty();
    // A setting under a quick slider is driven by the slider instead.
    if (previewing_ && !plugins::macroControlling(draft_, b, s->id)) {
        if (auto sent = engine_.setVoiceParameter(b, p, stored.value()); !sent) {
            notifications_.post(QStringLiteral("engine-busy"), NotificationModel::Level::Warning,
                                tr("A change was not applied"), message(sent.error()));
        }
    }
    return static_cast<double>(stored.value());
}

double DesignerController::valueAt(int block, int param, double position) const {
    const plugins::ParamSpec* s = spec(block, param);
    return s != nullptr ? static_cast<double>(s->fromNormalized(static_cast<float>(position)))
                        : 0.0;
}

double DesignerController::positionOf(int block, int param, double value) const {
    const plugins::ParamSpec* s = spec(block, param);
    return s != nullptr ? static_cast<double>(s->toNormalized(static_cast<float>(value))) : 0.0;
}

QString DesignerController::formatValue(int block, int param, double value) const {
    const plugins::ParamSpec* s = spec(block, param);
    if (s == nullptr) {
        return {};
    }
    if (s->kind == plugins::ParamKind::Choice) {
        const auto i = static_cast<std::size_t>(s->clamp(static_cast<float>(value)));
        return i < s->choices.size() ? toQString(s->choices[i]) : QString{};
    }
    if (s->kind == plugins::ParamKind::Toggle) {
        return value >= 0.5 ? tr("On") : tr("Off");
    }
    const QString unit = toQString(s->unit);
    if (unit.isEmpty() && s->min >= -1.0F && s->max <= 1.0F) {
        return QStringLiteral("%1 %").arg(std::round(value * 100.0), 0, 'f', 0); // mix, depth
    }
    if (unit.isEmpty() && s->min >= 1.0F) {
        return QString::number(std::round(value), 'f', 0); // counts: bands, bits, voices
    }
    const double magnitude = std::abs(value);
    int decimals = 2;
    if (magnitude >= 100.0) {
        decimals = 0;
    } else if (magnitude >= 10.0) {
        decimals = 1;
    }
    const QString number = QString::number(value, 'f', decimals);
    return unit.isEmpty() ? number : number + QLatin1Char(' ') + unit;
}

QString DesignerController::exposeParameter(int block, int param) {
    if (block < 0 || param < 0) {
        return tr("That setting is no longer part of this voice.");
    }
    auto exposed = plugins::exposeParameter(draft_, static_cast<std::size_t>(block),
                                            static_cast<std::size_t>(param), registry_);
    if (!exposed) {
        return message(exposed.error());
    }
    setDirty();
    emit blocksChanged(); // the setting now shows it is under a quick slider
    emit macrosChanged();
    return {};
}

void DesignerController::removeMacro(int index) {
    if (index < 0) {
        return;
    }
    if (plugins::removeMacro(draft_, static_cast<std::size_t>(index), registry_)) {
        structureChanged();
    }
}

void DesignerController::renameMacro(int index, const QString& name) {
    const QString trimmed = name.trimmed();
    if (index < 0 || static_cast<std::size_t>(index) >= draft_.macros.size() || trimmed.isEmpty()) {
        return;
    }
    draft_.macros[static_cast<std::size_t>(index)].name = trimmed.toStdString();
    setDirty();
    emit macrosChanged();
}

void DesignerController::setMacroPosition(int index, double position) {
    if (index < 0 || static_cast<std::size_t>(index) >= draft_.macros.size()) {
        return;
    }
    const auto i = static_cast<std::size_t>(index);
    const auto pos = static_cast<float>(std::clamp(position, 0.0, 1.0));
    draft_.macros[i].defaultPosition = pos;
    setDirty();
    if (!previewing_) {
        return;
    }
    for (const auto& change : plugins::evaluateMacro(draft_, i, pos, registry_)) {
        if (auto sent = engine_.setVoiceParameter(change.block, change.param, change.value);
            !sent) {
            notifications_.post(QStringLiteral("engine-busy"), NotificationModel::Level::Warning,
                                tr("A change was not applied"), message(sent.error()));
            break;
        }
    }
}

void DesignerController::preview() {
    applyPreview();
}

QString DesignerController::save() {
    if (!editing_) {
        return tr("Start or open a voice first.");
    }
    if (QString::fromStdString(draft_.name).trimmed().isEmpty()) {
        return tr("Give the voice a name.");
    }
    if (draft_.blocks.empty()) {
        return tr("Add at least one effect.");
    }
    if (auto valid = plugins::validateVoice(draft_, registry_); !valid) {
        return message(valid.error());
    }
    if (auto saved = store_.save(draft_); !saved) {
        return message(saved.error());
    }
    voices_.upsertVoice(draft_);
    editing_ = false;
    dirty_ = false;
    setPreviewing(false);
    emit editingChanged();
    emit dirtyChanged();
    selecting_ = true;
    static_cast<void>(voices_.selectVoice(QString::fromStdString(draft_.id)));
    selecting_ = false;
    return {};
}

void DesignerController::discard() {
    if (!editing_) {
        return;
    }
    const bool wasPreviewing = previewing_;
    editing_ = false;
    dirty_ = false;
    setPreviewing(false);
    emit editingChanged();
    emit dirtyChanged();
    if (wasPreviewing) {
        selecting_ = true;
        static_cast<void>(voices_.selectVoice(voices_.currentVoiceId()));
        selecting_ = false;
    }
}

QString DesignerController::deleteVoice(const QString& id) {
    const plugins::VoicePreset* voice = voices_.preset(id);
    if (voice == nullptr) {
        return tr("That voice no longer exists.");
    }
    if (voice->builtIn) {
        return tr("Built-in voices cannot be deleted.");
    }
    if (auto removed = store_.remove(id); !removed) {
        return message(removed.error());
    }
    if (editing_ && draft_.id == id.toStdString()) {
        discard();
    }
    voices_.removeVoice(id);
    return {};
}

QString DesignerController::exportVoice(const QString& id, const QUrl& file) {
    const plugins::VoicePreset* voice = voices_.preset(id);
    if (voice == nullptr) {
        return tr("That voice no longer exists.");
    }
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const QString extension = QLatin1Char('.') + CustomVoiceStore::fileExtension();
    if (!path.endsWith(extension, Qt::CaseInsensitive)) {
        path += extension;
    }
    plugins::VoicePreset copy = *voice;
    copy.expect = {};
    if (auto written = CustomVoiceStore::exportTo(copy, path); !written) {
        return message(written.error());
    }
    return {};
}

int DesignerController::importVoices(const QList<QUrl>& files) {
    QStringList problems;
    int imported = 0;
    for (const QUrl& url : files) {
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        auto voice = CustomVoiceStore::importFrom(path, registry_);
        if (!voice) {
            problems.append(message(voice.error()));
            continue;
        }
        plugins::VoicePreset preset = std::move(voice).value();
        preset.id = newId().toStdString();
        preset.name = uniqueName(toQString(preset.name)).toStdString();
        if (auto saved = store_.save(preset); !saved) {
            problems.append(message(saved.error()));
            continue;
        }
        voices_.upsertVoice(preset);
        ++imported;
    }
    if (problems.isEmpty()) {
        notifications_.dismiss(QStringLiteral("voice-import"));
    } else {
        notifications_.post(QStringLiteral("voice-import"), NotificationModel::Level::Warning,
                            problems.size() == 1 ? tr("A voice was not imported")
                                                 : tr("%n voices were not imported", nullptr,
                                                      static_cast<int>(problems.size())),
                            problems.join(QLatin1Char('\n')));
    }
    return imported;
}

void DesignerController::setName(const QString& name) {
    if (name.toStdString() == draft_.name) {
        return;
    }
    draft_.name = name.toStdString();
    setDirty();
    emit detailsChanged();
}

void DesignerController::setCategory(const QString& category) {
    if (!categories().contains(category) || category.toStdString() == draft_.category) {
        return;
    }
    draft_.category = category.toStdString();
    setDirty();
    emit detailsChanged();
}

void DesignerController::setDescription(const QString& description) {
    if (description.toStdString() == draft_.description) {
        return;
    }
    draft_.description = description.toStdString();
    setDirty();
    emit detailsChanged();
}

void DesignerController::setIcon(const QString& icon) {
    if (!icons().contains(icon) || icon.toStdString() == draft_.icon) {
        return;
    }
    draft_.icon = icon.toStdString();
    setDirty();
    emit detailsChanged();
}

void DesignerController::setColor(const QString& color) {
    if (!colors().contains(color, Qt::CaseInsensitive) || color.toStdString() == draft_.color) {
        return;
    }
    draft_.color = color.toUpper().toStdString();
    setDirty();
    emit detailsChanged();
}

QVariantList DesignerController::blocks() const {
    QVariantList out;
    for (std::size_t b = 0; b < draft_.blocks.size(); ++b) {
        const plugins::BlockSpec& block = draft_.blocks[b];
        const plugins::EffectDescriptor* d = registry_.find(block.effect);
        if (d == nullptr) {
            continue;
        }
        QVariantList params;
        for (std::size_t p = 0; p < d->params.size(); ++p) {
            const plugins::ParamSpec& s = d->params[p];
            const float value = plugins::blockParameter(draft_, b, p, registry_);
            const auto macro = plugins::macroControlling(draft_, b, s.id);
            QStringList choices;
            for (const auto& c : s.choices) {
                choices.append(toQString(c));
            }
            params.append(QVariantMap{
                {QStringLiteral("index"), static_cast<int>(p)},
                {QStringLiteral("id"), toQString(s.id)},
                {QStringLiteral("name"), toQString(s.name)},
                {QStringLiteral("unit"), toQString(s.unit)},
                {QStringLiteral("kind"), kindName(s.kind)},
                {QStringLiteral("choices"), choices},
                {QStringLiteral("value"), static_cast<double>(value)},
                {QStringLiteral("position"), static_cast<double>(s.toNormalized(value))},
                {QStringLiteral("macro"),
                 macro ? toQString(draft_.macros[*macro].name) : QString{}}});
        }
        out.append(QVariantMap{{QStringLiteral("index"), static_cast<int>(b)},
                               {QStringLiteral("effect"), toQString(block.effect)},
                               {QStringLiteral("name"), toQString(d->name)},
                               {QStringLiteral("description"), toQString(d->description)},
                               {QStringLiteral("bypassed"), block.bypassed},
                               {QStringLiteral("params"), params}});
    }
    return out;
}

QVariantList DesignerController::macros() const {
    QVariantList out;
    for (std::size_t m = 0; m < draft_.macros.size(); ++m) {
        const plugins::MacroSpec& macro = draft_.macros[m];
        QStringList targets;
        for (const auto& t : macro.targets) {
            const plugins::EffectDescriptor* d = registry_.find(draft_.blocks[t.block].effect);
            const std::size_t p = d != nullptr ? d->indexOf(t.param) : 0;
            if (d != nullptr && p < d->params.size()) {
                targets.append(
                    QStringLiteral("%1: %2").arg(toQString(d->name), toQString(d->params[p].name)));
            }
        }
        out.append(
            QVariantMap{{QStringLiteral("index"), static_cast<int>(m)},
                        {QStringLiteral("name"), toQString(macro.name)},
                        {QStringLiteral("position"), static_cast<double>(macro.defaultPosition)},
                        {QStringLiteral("targets"), targets.join(QStringLiteral(", "))}});
    }
    return out;
}

bool DesignerController::canAddBlock() const {
    return editing_ && draft_.blocks.size() < plugins::kMaxVoiceBlocks;
}

bool DesignerController::canAddMacro() const {
    return editing_ && draft_.macros.size() < plugins::kMaxVoiceMacros;
}

QVariantList DesignerController::palette() const {
    QVariantList out;
    for (const plugins::EffectDescriptor& d : registry_.descriptors()) {
        out.append(QVariantMap{{QStringLiteral("id"), toQString(d.id)},
                               {QStringLiteral("name"), toQString(d.name)},
                               {QStringLiteral("category"), toQString(d.category)},
                               {QStringLiteral("description"), toQString(d.description)}});
    }
    std::ranges::stable_sort(out, [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("category")).toString() <
               b.toMap().value(QStringLiteral("category")).toString();
    });
    return out;
}

QStringList DesignerController::categories() {
    QStringList out;
    for (const auto& c : plugins::voiceCategories()) {
        out.append(toQString(c));
    }
    return out;
}

QStringList DesignerController::icons() {
    // The category glyphs that voice tiles use.
    QStringList out;
    for (const auto& c : plugins::voiceCategories()) {
        out.append(toQString(c).toLower());
    }
    return out;
}

QStringList DesignerController::colors() {
    return {QStringLiteral("#FF7A59"), QStringLiteral("#F5B841"), QStringLiteral("#3DD68C"),
            QStringLiteral("#4FB3BF"), QStringLiteral("#5AA9F5"), QStringLiteral("#9B8CFF"),
            QStringLiteral("#C77DFF"), QStringLiteral("#E85D75")};
}

QVariantList DesignerController::customVoices() const {
    QVariantList out;
    for (const plugins::VoicePreset& p : voices_.presets()) {
        if (!p.builtIn) {
            out.append(QVariantMap{{QStringLiteral("voiceId"), toQString(p.id)},
                                   {QStringLiteral("name"), toQString(p.name)},
                                   {QStringLiteral("category"), toQString(p.category)},
                                   {QStringLiteral("iconName"), toQString(p.icon)},
                                   {QStringLiteral("accentColor"), toQString(p.color)},
                                   {QStringLiteral("effects"), static_cast<int>(p.blocks.size())}});
        }
    }
    return out;
}

} // namespace vox::app
