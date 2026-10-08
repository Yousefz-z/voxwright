#include "soundboard_store.hpp"

#include <vox/plugins/sound_pack.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace vox::app {
namespace {

constexpr int kFormatVersion = 1;

QJsonObject soundToJson(const SoundEntry& s) {
    return {{"slot", static_cast<int>(s.slot)},
            {"name", s.name},
            {"source", s.source},
            {"color", s.color},
            {"icon", s.icon},
            {"hotkey", s.hotkey},
            {"mode", playModeName(s.options.mode)},
            {"loop", s.options.loop},
            {"gainDb", static_cast<double>(s.options.gainDb)},
            {"muteOthers", s.options.muteOthers},
            {"stopOthers", s.options.stopOthers},
            {"muteVoice", s.options.muteVoice},
            {"muteForMe", s.options.muteForMe}};
}

std::optional<SoundEntry> soundFromJson(const QJsonObject& o) {
    SoundEntry s;
    const int slot = o.value("slot").toInt(0);
    if (slot <= 0 || slot >= 1024 || o.value("source").toString().isEmpty()) {
        return std::nullopt;
    }
    s.slot = static_cast<std::uint32_t>(slot);
    s.name = o.value("name").toString();
    s.source = o.value("source").toString();
    s.color = o.value("color").toString(QStringLiteral("#A6ACBF"));
    s.icon = o.value("icon").toString(QStringLiteral("sparkle"));
    s.hotkey = o.value("hotkey").toString();
    s.options.mode = playModeFromName(o.value("mode").toString());
    s.options.loop = o.value("loop").toBool(false);
    s.options.gainDb = static_cast<float>(o.value("gainDb").toDouble(0.0));
    s.options.muteOthers = o.value("muteOthers").toBool(false);
    s.options.stopOthers = o.value("stopOthers").toBool(false);
    s.options.muteVoice = o.value("muteVoice").toBool(false);
    s.options.muteForMe = o.value("muteForMe").toBool(false);
    return s;
}

} // namespace

QString playModeName(engine::PlayMode mode) {
    switch (mode) {
    case engine::PlayMode::Restart:
        return QStringLiteral("restart");
    case engine::PlayMode::Toggle:
        return QStringLiteral("toggle");
    case engine::PlayMode::Pause:
        return QStringLiteral("pause");
    case engine::PlayMode::Overlap:
        return QStringLiteral("overlap");
    case engine::PlayMode::Hold:
        return QStringLiteral("hold");
    }
    return QStringLiteral("restart");
}

engine::PlayMode playModeFromName(const QString& name) {
    for (const auto mode :
         {engine::PlayMode::Restart, engine::PlayMode::Toggle, engine::PlayMode::Pause,
          engine::PlayMode::Overlap, engine::PlayMode::Hold}) {
        if (playModeName(mode) == name) {
            return mode;
        }
    }
    return engine::PlayMode::Restart;
}

SoundboardStore::SoundboardStore(QString path)
    : path_(std::move(path)) {}

QString SoundboardStore::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/soundboards.json");
}

SoundboardData SoundboardStore::defaults() {
    SoundboardData data;
    std::uint32_t slot = 1;
    for (const auto& sound : plugins::builtinSounds()) {
        const QString category =
            QString::fromUtf8(sound.category.data(), static_cast<qsizetype>(sound.category.size()));
        auto board = std::find_if(data.boards.begin(), data.boards.end(),
                                  [&](const Board& b) { return b.name == category; });
        if (board == data.boards.end()) {
            data.boards.push_back({category, {}});
            board = data.boards.end() - 1;
        }
        SoundEntry e;
        e.slot = slot++;
        e.name = QString::fromUtf8(sound.name.data(), static_cast<qsizetype>(sound.name.size()));
        e.source = QStringLiteral("builtin:") +
                   QString::fromUtf8(sound.id.data(), static_cast<qsizetype>(sound.id.size()));
        e.color = QString::fromUtf8(sound.color.data(), static_cast<qsizetype>(sound.color.size()));
        e.icon = QString::fromUtf8(sound.icon.data(), static_cast<qsizetype>(sound.icon.size()));
        // A siren and crickets are more useful looping until stopped.
        e.options.mode = engine::PlayMode::Toggle;
        e.options.loop = sound.id == "alarm" || sound.id == "crickets";
        board->sounds.push_back(e);
    }
    return data;
}

SoundboardStore::Loaded SoundboardStore::load() const {
    Loaded result;
    QFile file(path_);
    if (!file.exists()) {
        result.firstRun = true;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.problem = makeError(ErrorCode::FileUnreadable,
                                   "Your soundboards could not be read. Check that you can open " +
                                       path_.toStdString() + ".",
                                   file.errorString().toStdString());
        return result;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        const QString aside = path_ + QStringLiteral(".damaged");
        QFile::remove(aside);
        const bool kept = QFile::rename(path_, aside);
        result.problem = makeError(
            ErrorCode::SettingsDamaged,
            "Your soundboard file was damaged, so the built-in sounds were restored." +
                (kept ? " The damaged file was kept as " + aside.toStdString() + "." : ""),
            parseError.errorString().toStdString());
        result.firstRun = true;
        return result;
    }
    for (const auto boardValue : doc.object().value("boards").toArray()) {
        const QJsonObject b = boardValue.toObject();
        Board board{b.value("name").toString(QStringLiteral("Sounds")), {}};
        for (const auto soundValue : b.value("sounds").toArray()) {
            if (auto entry = soundFromJson(soundValue.toObject())) {
                board.sounds.push_back(std::move(*entry));
            }
        }
        result.data.boards.push_back(std::move(board));
    }
    return result;
}

Status SoundboardStore::save(const SoundboardData& data) const {
    const QFileInfo info(path_);
    if (!QDir().mkpath(info.absolutePath())) {
        return makeError(ErrorCode::FileWriteFailed, "Soundboards could not be saved: the folder " +
                                                         info.absolutePath().toStdString() +
                                                         " could not be created.");
    }
    QJsonArray boards;
    for (const Board& board : data.boards) {
        QJsonArray sounds;
        for (const SoundEntry& s : board.sounds) {
            sounds.append(soundToJson(s));
        }
        boards.append(QJsonObject{{"name", board.name}, {"sounds", sounds}});
    }
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{"version", kFormatVersion}, {"boards", boards}})
            .toJson(QJsonDocument::Indented);
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return makeError(ErrorCode::FileWriteFailed,
                         "Soundboards could not be saved to " + path_.toStdString() +
                             ". Check that the disk is not full and the file is not read-only.",
                         file.errorString().toStdString());
    }
    return {};
}

} // namespace vox::app
