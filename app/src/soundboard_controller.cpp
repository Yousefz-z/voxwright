#include "soundboard_controller.hpp"

#include <vox/devices/audio_file.hpp>
#include <vox/plugins/sound_pack.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace vox::app {
namespace {

constexpr int kSaveDelayMs = 500;
constexpr std::uint32_t kMaxSlot = 1023;
QString builtinPrefix() {
    return QStringLiteral("builtin:");
}

/// Colors given to imported sounds in turn.
constexpr std::array<const char*, 8> kPalette{"#FF7A59", "#5AA9F5", "#3DD68C", "#F5B841",
                                              "#C77DFF", "#E85D75", "#8CC8E8", "#7BD389"};

struct Loaded {
    std::vector<float> samples;
    std::optional<Error> error;
};

/// Built-in sounds are rendered once per process (they never change).
std::optional<std::vector<float>> cachedRender(const std::string& id) {
    static std::mutex mutex;
    static std::map<std::string, std::vector<float>> cache;
    {
        const std::scoped_lock lock(mutex);
        if (const auto it = cache.find(id); it != cache.end()) {
            return it->second;
        }
    }
    auto rendered = plugins::renderBuiltinSound(id, engine::kEngineRate);
    if (!rendered) {
        return std::nullopt;
    }
    const std::scoped_lock lock(mutex);
    return cache.emplace(id, std::move(rendered).value()).first->second;
}

Loaded loadSource(const QString& source) {
    Loaded out;
    if (source.startsWith(builtinPrefix())) {
        const std::string id = source.mid(builtinPrefix().size()).toStdString();
        if (auto samples = cachedRender(id)) {
            out.samples = std::move(*samples);
        } else {
            out.error = makeError(ErrorCode::FileNotFound,
                                  "The built-in sound \"" + id +
                                      "\" does not exist in this version of Voxwright.");
        }
        return out;
    }
    auto decoded = devices::decodeAudioFile(source.toStdString(), engine::kEngineRate);
    if (decoded) {
        out.samples = std::move(decoded).value().samples;
    } else {
        out.error = decoded.error();
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------- model

SoundListModel::SoundListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int SoundListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant SoundListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Row& r = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case SlotRole:
        return r.slot;
    case Qt::DisplayRole:
    case NameRole:
        return r.name;
    case ColorRole:
        return r.color;
    case IconRole:
        return r.icon;
    case HotkeyRole:
        return r.hotkey;
    case PlayingRole:
        return r.playing;
    case LoadStateRole:
        return static_cast<int>(r.state);
    case ModeRole:
        return r.mode;
    case LoopRole:
        return r.loop;
    default:
        return {};
    }
}

QHash<int, QByteArray> SoundListModel::roleNames() const {
    return {{SlotRole, "slot"},           {NameRole, "name"},     {ColorRole, "accentColor"},
            {IconRole, "iconName"},       {HotkeyRole, "hotkey"}, {PlayingRole, "playing"},
            {LoadStateRole, "loadState"}, {ModeRole, "mode"},     {LoopRole, "loop"}};
}

void SoundListModel::setRows(std::vector<Row> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
    emit countChanged();
}

void SoundListModel::setState(std::uint32_t slot, bool playing, LoadState state) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].slot == slot) {
            rows_[i].playing = playing;
            rows_[i].state = state;
            const QModelIndex changed = index(static_cast<int>(i));
            emit dataChanged(changed, changed, {PlayingRole, LoadStateRole});
            return;
        }
    }
}

// ---------------------------------------------------------------- controller

SoundboardController::SoundboardController(engine::AudioEngine& engine, HotkeyController& hotkeys,
                                           NotificationModel& notifications, Paths paths,
                                           QObject* parent)
    : QObject(parent)
    , engine_(engine)
    , hotkeys_(hotkeys)
    , notifications_(notifications)
    , store_(std::move(paths.store))
    , library_(std::move(paths.library))
    , saveTimer_(new QTimer(this)) {
    saveTimer_->setSingleShot(true);
    saveTimer_->setInterval(kSaveDelayMs);
    connect(saveTimer_, &QTimer::timeout, this, [this] {
        if (auto saved = saveNow(); !saved) {
            notifications_.post(QStringLiteral("soundboard-save"), NotificationModel::Level::Error,
                                tr("Soundboard not saved"),
                                QString::fromStdString(saved.error().message));
        }
    });
    connect(&hotkeys_, &HotkeyController::soundTriggered, this,
            [this](quint32 slot, bool pressed) { pressed ? press(slot) : release(slot); });
    hotkeys_.setSoundNamer([this](quint32 slot) { return soundName(slot); });
}

void SoundboardController::initialize() {
    auto loaded = store_.load();
    if (loaded.problem) {
        notifications_.post(QStringLiteral("soundboard"), NotificationModel::Level::Warning,
                            tr("Soundboard reset"),
                            QString::fromStdString(loaded.problem->message));
    }
    data_ = loaded.firstRun ? SoundboardStore::defaults() : std::move(loaded.data);
    if (data_.boards.empty()) {
        data_.boards.push_back({tr("Sounds"), {}});
    }
    QHash<quint32, QString> keys;
    for (const Board& board : data_.boards) {
        for (const SoundEntry& s : board.sounds) {
            keys.insert(s.slot, s.hotkey);
            load(s);
        }
    }
    hotkeys_.restoreSoundHotkeys(keys);
    refreshModel();
    emit boardsChanged();
    if (loaded.firstRun) {
        scheduleSave();
    }
}

QStringList SoundboardController::fileFilters() {
    QStringList patterns;
    for (const auto& ext : devices::supportedAudioExtensions()) {
        patterns.append(QStringLiteral("*") + QString::fromStdString(ext));
    }
    return {tr("Audio files (%1)").arg(patterns.join(QLatin1Char(' ')))};
}

QStringList SoundboardController::boardNames() const {
    QStringList names;
    for (const Board& b : data_.boards) {
        names.append(b.name);
    }
    return names;
}

void SoundboardController::setCurrentBoard(int index) {
    if (index < 0 || index >= static_cast<int>(data_.boards.size()) || index == current_) {
        return;
    }
    current_ = index;
    refreshModel();
    emit currentBoardChanged();
}

SoundboardController::Located SoundboardController::find(std::uint32_t slot) {
    for (Board& b : data_.boards) {
        for (SoundEntry& s : b.sounds) {
            if (s.slot == slot) {
                return {&b, &s};
            }
        }
    }
    return {};
}

const SoundEntry* SoundboardController::findEntry(std::uint32_t slot) const {
    for (const Board& b : data_.boards) {
        for (const SoundEntry& s : b.sounds) {
            if (s.slot == slot) {
                return &s;
            }
        }
    }
    return nullptr;
}

QString SoundboardController::soundName(quint32 slot) const {
    const SoundEntry* s = findEntry(slot);
    return s != nullptr ? s->name : QString{};
}

std::uint32_t SoundboardController::freeSlot() const {
    for (std::uint32_t slot = 1; slot <= kMaxSlot; ++slot) {
        if (findEntry(slot) == nullptr) {
            return slot;
        }
    }
    return 0;
}

void SoundboardController::refreshModel() {
    std::vector<SoundListModel::Row> rows;
    if (current_ < static_cast<int>(data_.boards.size())) {
        for (const SoundEntry& s : data_.boards[static_cast<std::size_t>(current_)].sounds) {
            rows.push_back({s.slot, s.name, s.color, s.icon,
                            HotkeyController::displayText(s.hotkey), playing_.contains(s.slot),
                            states_.value(s.slot, SoundListModel::LoadState::Loading),
                            playModeName(s.options.mode), s.options.loop});
        }
    }
    model_.setRows(std::move(rows));
}

void SoundboardController::load(const SoundEntry& entry) {
    const std::uint32_t slot = entry.slot;
    const quint64 generation = nextGeneration_++;
    generation_.insert(slot, generation);
    states_.insert(slot, SoundListModel::LoadState::Loading);
    ++loading_;
    emit loadingChanged();
    // The continuation runs on this object's thread, and not at all if this
    // object is gone by then.
    static_cast<void>(
        QtConcurrent::run(&loadSource, entry.source)
            .then(this, [this, slot, generation](Loaded result) {
                --loading_;
                emit loadingChanged();
                if (generation_.value(slot) != generation) {
                    return; // removed or replaced while loading
                }
                const Located where = find(slot);
                if (where.sound == nullptr) {
                    return;
                }
                const Status installed = result.error
                                             ? Status(*result.error)
                                             : engine_.loadSound(slot, std::move(result.samples));
                if (!installed) {
                    states_.insert(slot, SoundListModel::LoadState::Failed);
                    notifications_.post(QStringLiteral("sound-%1").arg(slot),
                                        NotificationModel::Level::Warning,
                                        tr("\"%1\" could not be loaded").arg(where.sound->name),
                                        QString::fromStdString(installed.error().message));
                } else {
                    states_.insert(slot, SoundListModel::LoadState::Ready);
                }
                model_.setState(slot, playing_.contains(slot), states_.value(slot));
            }));
}

void SoundboardController::importFiles(const QList<QUrl>& urls) {
    if (data_.boards.empty()) {
        return;
    }
    Board& board = data_.boards[static_cast<std::size_t>(current_)];
    QDir().mkpath(library_);
    const auto& supported = devices::supportedAudioExtensions();
    for (const QUrl& url : urls) {
        const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : url.toString());
        const QString key = QStringLiteral("import-%1").arg(info.fileName());
        const std::string ext = QStringLiteral(".%1").arg(info.suffix().toLower()).toStdString();
        if (std::find(supported.begin(), supported.end(), ext) == supported.end()) {
            notifications_.post(key, NotificationModel::Level::Warning,
                                tr("\"%1\" was not added").arg(info.fileName()),
                                tr("Only WAV, MP3, FLAC, and OGG files can be added. Convert it "
                                   "to one of these and try again."));
            continue;
        }
        if (!info.exists()) {
            notifications_.post(key, NotificationModel::Level::Warning,
                                tr("\"%1\" was not added").arg(info.fileName()),
                                tr("The file was not found. It may have been moved or deleted."));
            continue;
        }
        const std::uint32_t slot = freeSlot();
        if (slot == 0) {
            notifications_.post(
                QStringLiteral("soundboard-full"), NotificationModel::Level::Error,
                tr("The soundboard is full"),
                tr("Voxwright holds up to %1 sounds. Remove some to add more.").arg(kMaxSlot));
            return;
        }
        const QString copy =
            QDir(library_).filePath(QStringLiteral("%1-%2").arg(slot).arg(info.fileName()));
        QFile::remove(copy);
        if (!QFile::copy(info.absoluteFilePath(), copy)) {
            notifications_.post(key, NotificationModel::Level::Error,
                                tr("\"%1\" was not added").arg(info.fileName()),
                                tr("It could not be copied into the sound library (%1). Check "
                                   "that the disk is not full.")
                                    .arg(library_));
            continue;
        }
        SoundEntry entry;
        entry.slot = slot;
        entry.name = info.completeBaseName();
        entry.source = copy;
        entry.color = QString::fromLatin1(kPalette[slot % kPalette.size()]);
        entry.icon = QStringLiteral("musical");
        board.sounds.push_back(entry);
        load(entry);
    }
    refreshModel();
    scheduleSave();
}

void SoundboardController::forget(const SoundEntry& entry) {
    generation_.remove(entry.slot);
    states_.remove(entry.slot);
    playing_.remove(entry.slot);
    hotkeys_.forgetSound(entry.slot);
    static_cast<void>(engine_.unloadSound(entry.slot));
    if (!entry.source.startsWith(builtinPrefix()) && entry.source.startsWith(library_)) {
        QFile::remove(entry.source);
    }
}

void SoundboardController::removeSound(quint32 slot) {
    const Located where = find(slot);
    if (where.sound == nullptr) {
        return;
    }
    const SoundEntry entry = *where.sound;
    std::erase_if(where.board->sounds, [slot](const SoundEntry& s) { return s.slot == slot; });
    forget(entry);
    refreshModel();
    emit playingChanged();
    scheduleSave();
}

void SoundboardController::updateSound(quint32 slot, const QVariantMap& changes) {
    const Located where = find(slot);
    if (where.sound == nullptr) {
        return;
    }
    SoundEntry& s = *where.sound;
    if (changes.contains(QStringLiteral("name"))) {
        const QString name = changes.value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty()) {
            s.name = name;
        }
    }
    if (changes.contains(QStringLiteral("mode"))) {
        s.options.mode = playModeFromName(changes.value(QStringLiteral("mode")).toString());
    }
    if (changes.contains(QStringLiteral("loop"))) {
        s.options.loop = changes.value(QStringLiteral("loop")).toBool();
    }
    if (changes.contains(QStringLiteral("gainDb"))) {
        s.options.gainDb =
            std::clamp(changes.value(QStringLiteral("gainDb")).toFloat(), -40.0F, 12.0F);
    }
    for (const auto& [name, flag] :
         {std::pair{QStringLiteral("muteOthers"), &s.options.muteOthers},
          std::pair{QStringLiteral("stopOthers"), &s.options.stopOthers},
          std::pair{QStringLiteral("muteVoice"), &s.options.muteVoice},
          std::pair{QStringLiteral("muteForMe"), &s.options.muteForMe}}) {
        if (changes.contains(name)) {
            *flag = changes.value(name).toBool();
        }
    }
    refreshModel();
    scheduleSave();
}

QString SoundboardController::setSoundHotkey(quint32 slot, const QString& sequence) {
    const Located where = find(slot);
    if (where.sound == nullptr) {
        return tr("This sound no longer exists.");
    }
    QString problem = hotkeys_.assignSound(slot, sequence);
    if (problem.isEmpty()) {
        where.sound->hotkey = sequence;
        refreshModel();
        scheduleSave();
    }
    return problem;
}

QVariantMap SoundboardController::soundDetails(quint32 slot) const {
    const SoundEntry* s = findEntry(slot);
    if (s == nullptr) {
        return {};
    }
    return {{QStringLiteral("slot"), s->slot},
            {QStringLiteral("name"), s->name},
            {QStringLiteral("mode"), playModeName(s->options.mode)},
            {QStringLiteral("loop"), s->options.loop},
            {QStringLiteral("gainDb"), static_cast<double>(s->options.gainDb)},
            {QStringLiteral("muteOthers"), s->options.muteOthers},
            {QStringLiteral("stopOthers"), s->options.stopOthers},
            {QStringLiteral("muteVoice"), s->options.muteVoice},
            {QStringLiteral("muteForMe"), s->options.muteForMe},
            {QStringLiteral("hotkey"), s->hotkey},
            {QStringLiteral("hotkeyText"), HotkeyController::displayText(s->hotkey)},
            {QStringLiteral("builtIn"), s->source.startsWith(builtinPrefix())}};
}

void SoundboardController::addBoard(const QString& name) {
    const QString trimmed = name.trimmed();
    data_.boards.push_back({trimmed.isEmpty() ? tr("New board") : trimmed, {}});
    emit boardsChanged();
    setCurrentBoard(static_cast<int>(data_.boards.size()) - 1);
    scheduleSave();
}

void SoundboardController::renameBoard(int index, const QString& name) {
    if (index < 0 || index >= static_cast<int>(data_.boards.size()) || name.trimmed().isEmpty()) {
        return;
    }
    data_.boards[static_cast<std::size_t>(index)].name = name.trimmed();
    emit boardsChanged();
    scheduleSave();
}

void SoundboardController::removeBoard(int index) {
    if (index < 0 || index >= static_cast<int>(data_.boards.size()) || data_.boards.size() == 1) {
        return; // there is always at least one board
    }
    for (const SoundEntry& s : data_.boards[static_cast<std::size_t>(index)].sounds) {
        forget(s);
    }
    data_.boards.erase(data_.boards.begin() + index);
    current_ = std::min(current_, static_cast<int>(data_.boards.size()) - 1);
    refreshModel();
    emit boardsChanged();
    emit currentBoardChanged();
    emit playingChanged();
    scheduleSave();
}

// ---------------------------------------------------------------- playing

void SoundboardController::setPlaying(std::uint32_t slot, bool playing) {
    const bool changed = playing ? !playing_.contains(slot) : playing_.remove(slot);
    if (playing) {
        playing_.insert(slot);
    }
    model_.setState(slot, playing, states_.value(slot, SoundListModel::LoadState::Loading));
    if (changed) {
        emit playingChanged();
    }
}

void SoundboardController::press(quint32 slot) {
    const SoundEntry* s = findEntry(slot);
    if (s == nullptr || states_.value(slot) != SoundListModel::LoadState::Ready) {
        return;
    }
    if (auto sent = engine_.triggerSound(slot, s->options, true); !sent) {
        notifications_.post(QStringLiteral("engine-busy"), NotificationModel::Level::Warning,
                            tr("A sound did not play"),
                            QString::fromStdString(sent.error().message));
        return;
    }
    // Toggle and pause presses on a playing sound stop or pause it; the
    // engine's "finished" event clears the state when it really ends.
    setPlaying(slot, true);
}

void SoundboardController::release(quint32 slot) {
    const SoundEntry* s = findEntry(slot);
    if (s == nullptr || s->options.mode != engine::PlayMode::Hold) {
        return;
    }
    static_cast<void>(engine_.triggerSound(slot, s->options, false));
}

void SoundboardController::stopAll() {
    static_cast<void>(engine_.stopAllSounds());
}

void SoundboardController::onSoundFinished(quint32 slot) {
    setPlaying(slot, false);
}

// ---------------------------------------------------------------- saving

void SoundboardController::scheduleSave() {
    saveTimer_->start();
}

Status SoundboardController::saveNow() {
    saveTimer_->stop();
    return store_.save(data_);
}

} // namespace vox::app
