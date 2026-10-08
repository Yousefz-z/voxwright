#include "custom_voice_store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace vox::app {
namespace {

/// Larger files are not voice files (the biggest built-in voice is 3 kB).
constexpr qint64 kMaxVoiceFileBytes = qint64{256} * 1024;

Status writeText(const QString& path, const std::string& text) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return makeError(ErrorCode::FileWriteFailed,
                         "The voice could not be saved to " + path.toStdString() +
                             ". Check that the folder exists and you can write to it.",
                         file.errorString().toStdString());
    }
    file.write(text.data(), static_cast<qint64>(text.size()));
    if (!file.commit()) {
        return makeError(ErrorCode::FileWriteFailed,
                         "The voice could not be saved to " + path.toStdString() +
                             ". The disk may be full.",
                         file.errorString().toStdString());
    }
    return {};
}

Result<plugins::VoicePreset> readVoice(const QString& path,
                                       const plugins::EffectRegistry& registry) {
    QFile file(path);
    const QString name = QFileInfo(path).fileName();
    if (!file.exists()) {
        return makeError(ErrorCode::FileNotFound, "\"" + name.toStdString() + "\" was not found.");
    }
    if (file.size() > kMaxVoiceFileBytes) {
        return makeError(ErrorCode::InvalidPreset,
                         "\"" + name.toStdString() + "\" is too large to be a voice file.");
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return makeError(ErrorCode::FileUnreadable,
                         "\"" + name.toStdString() + "\" could not be opened.",
                         file.errorString().toStdString());
    }
    const QByteArray bytes = file.readAll();
    auto parsed = plugins::parseVoicePreset(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), registry);
    if (!parsed) {
        Error e = parsed.error();
        e.message = "\"" + name.toStdString() + "\": " + e.message;
        return e;
    }
    plugins::VoicePreset preset = std::move(parsed).value();
    preset.builtIn = false;
    return preset;
}

} // namespace

CustomVoiceStore::CustomVoiceStore(QString directory)
    : directory_(std::move(directory)) {}

QString CustomVoiceStore::defaultDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/voices");
}

bool CustomVoiceStore::isCustomId(const QString& id) {
    static const QRegularExpression kPattern(QStringLiteral("^custom-[a-z0-9]{1,32}$"));
    return kPattern.match(id).hasMatch();
}

QString CustomVoiceStore::pathOf(const QString& id) const {
    return directory_ + QLatin1Char('/') + id + QStringLiteral(".json");
}

CustomVoiceStore::Loaded CustomVoiceStore::load(const plugins::EffectRegistry& registry) const {
    Loaded result;
    const QDir dir(directory_);
    if (!dir.exists()) {
        return result;
    }
    const QStringList files =
        dir.entryList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QString& fileName : files) {
        const QString path = dir.filePath(fileName);
        auto voice = readVoice(path, registry);
        QString problem;
        if (!voice) {
            problem = QString::fromStdString(voice.error().message);
        } else if (const QString id = QString::fromStdString(voice.value().id);
                   !isCustomId(id) || pathOf(id) != path) {
            problem = QStringLiteral("\"%1\" does not belong in the voices folder.").arg(fileName);
        }
        if (problem.isEmpty()) {
            result.voices.push_back(std::move(voice).value());
            continue;
        }
        const QString aside = path + QStringLiteral(".damaged");
        QFile::remove(aside);
        if (QFile::rename(path, aside)) {
            problem += QStringLiteral(" It was renamed to %1.").arg(QFileInfo(aside).fileName());
        }
        result.problems.append(problem);
    }
    std::ranges::sort(result.voices, [](const auto& a, const auto& b) { return a.name < b.name; });
    return result;
}

Status CustomVoiceStore::save(const plugins::VoicePreset& preset) const {
    const QString id = QString::fromStdString(preset.id);
    if (!isCustomId(id)) {
        return makeError(ErrorCode::InvalidArgument, "Only your own voices can be saved.");
    }
    if (!QDir().mkpath(directory_)) {
        return makeError(ErrorCode::FileWriteFailed, "The voice could not be saved: the folder " +
                                                         directory_.toStdString() +
                                                         " could not be created.");
    }
    return writeText(pathOf(id), plugins::serializeVoicePreset(preset));
}

Status CustomVoiceStore::remove(const QString& id) const {
    if (!isCustomId(id)) {
        return makeError(ErrorCode::InvalidArgument, "Built-in voices cannot be deleted.");
    }
    QFile file(pathOf(id));
    if (file.exists() && !file.remove()) {
        return makeError(ErrorCode::FileWriteFailed,
                         "The voice file " + pathOf(id).toStdString() +
                             " could not be deleted. Check that it is not open in another app.",
                         file.errorString().toStdString());
    }
    return {};
}

Status CustomVoiceStore::exportTo(const plugins::VoicePreset& preset, const QString& path) {
    return writeText(path, plugins::serializeVoicePreset(preset));
}

Result<plugins::VoicePreset> CustomVoiceStore::importFrom(const QString& path,
                                                          const plugins::EffectRegistry& registry) {
    return readVoice(path, registry);
}

} // namespace vox::app
