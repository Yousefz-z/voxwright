#include "settings.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <utility>

namespace vox::app {
namespace {

constexpr int kFormatVersion = 1;

QJsonObject voiceToJson(const VoiceUserSettings& v) {
    QJsonArray macros;
    for (const float m : v.macroPositions) {
        macros.append(static_cast<double>(m));
    }
    return {{"macros", macros},
            {"bassDb", static_cast<double>(v.bassDb)},
            {"trebleDb", static_cast<double>(v.trebleDb)}};
}

VoiceUserSettings voiceFromJson(const QJsonObject& o) {
    VoiceUserSettings v;
    for (const auto m : o.value("macros").toArray()) {
        v.macroPositions.push_back(static_cast<float>(m.toDouble(0.5)));
    }
    v.bassDb = static_cast<float>(o.value("bassDb").toDouble(0.0));
    v.trebleDb = static_cast<float>(o.value("trebleDb").toDouble(0.0));
    return v;
}

float number(const QJsonObject& o, const char* key, float fallback) {
    return static_cast<float>(o.value(QLatin1String(key)).toDouble(static_cast<double>(fallback)));
}

QJsonObject toJson(const AppSettings& s) {
    QJsonObject voices;
    for (auto it = s.voices.cbegin(); it != s.voices.cend(); ++it) {
        voices.insert(it.key(), voiceToJson(it.value()));
    }
    const QJsonObject devices{{"inputId", s.inputId},
                              {"inputName", s.inputName},
                              {"virtualMicId", s.virtualMicId},
                              {"virtualMicName", s.virtualMicName},
                              {"monitorId", s.monitorId},
                              {"monitorName", s.monitorName},
                              {"useVirtualMic", s.useVirtualMic},
                              {"virtualMicChosen", s.virtualMicChosen},
                              {"periodFrames", s.periodFrames},
                              {"exclusive", s.exclusive}};
    const QJsonObject input{
        {"hearMyself", s.hearMyself},
        {"voiceEnabled", s.voiceEnabled},
        {"backgroundEnabled", s.backgroundEnabled},
        {"noiseReduction", s.noiseReduction},
        {"noiseReductionStrength", static_cast<double>(s.noiseReductionStrength)},
        {"gate", s.gate},
        {"gateThresholdDb", static_cast<double>(s.gateThresholdDb)},
        {"inputGainDb", static_cast<double>(s.inputGainDb)}};
    const QJsonObject mix{{"voiceDb", static_cast<double>(s.mix.voiceDb)},
                          {"soundsDb", static_cast<double>(s.mix.soundsDb)},
                          {"speechDb", static_cast<double>(s.mix.speechDb)},
                          {"monitorDb", static_cast<double>(s.mix.monitorDb)}};
    return {{"version", kFormatVersion},
            {"devices", devices},
            {"input", input},
            {"mix", mix},

            {"currentVoice", s.currentVoiceId},
            {"favorites", QJsonArray::fromStringList(s.favorites)},
            {"voices", voices}};
}

AppSettings fromJson(const QJsonObject& root) {
    AppSettings s;
    const AppSettings d;
    const QJsonObject devices = root.value("devices").toObject();
    s.inputId = devices.value("inputId").toString();
    s.inputName = devices.value("inputName").toString();
    s.virtualMicId = devices.value("virtualMicId").toString();
    s.virtualMicName = devices.value("virtualMicName").toString();
    s.monitorId = devices.value("monitorId").toString();
    s.monitorName = devices.value("monitorName").toString();
    s.useVirtualMic = devices.value("useVirtualMic").toBool(d.useVirtualMic);
    s.virtualMicChosen = devices.value("virtualMicChosen").toBool(false);
    s.periodFrames = devices.value("periodFrames").toInt(d.periodFrames);
    s.exclusive = devices.value("exclusive").toBool(d.exclusive);

    const QJsonObject input = root.value("input").toObject();
    s.hearMyself = input.value("hearMyself").toBool(d.hearMyself);
    s.voiceEnabled = input.value("voiceEnabled").toBool(d.voiceEnabled);
    s.backgroundEnabled = input.value("backgroundEnabled").toBool(d.backgroundEnabled);
    s.noiseReduction = input.value("noiseReduction").toBool(d.noiseReduction);
    s.noiseReductionStrength = number(input, "noiseReductionStrength", d.noiseReductionStrength);
    s.gate = input.value("gate").toBool(d.gate);
    s.gateThresholdDb = number(input, "gateThresholdDb", d.gateThresholdDb);
    s.inputGainDb = number(input, "inputGainDb", d.inputGainDb);

    const QJsonObject mix = root.value("mix").toObject();
    s.mix.voiceDb = number(mix, "voiceDb", d.mix.voiceDb);
    s.mix.soundsDb = number(mix, "soundsDb", d.mix.soundsDb);
    s.mix.speechDb = number(mix, "speechDb", d.mix.speechDb);
    s.mix.monitorDb = number(mix, "monitorDb", d.mix.monitorDb);

    s.currentVoiceId = root.value("currentVoice").toString(d.currentVoiceId);
    for (const auto f : root.value("favorites").toArray()) {
        s.favorites.append(f.toString());
    }
    const QJsonObject voices = root.value("voices").toObject();
    for (auto it = voices.begin(); it != voices.end(); ++it) {
        s.voices.insert(it.key(), voiceFromJson(it.value().toObject()));
    }
    return s;
}

} // namespace

SettingsStore::SettingsStore(QString path)
    : path_(std::move(path)) {}

QString SettingsStore::defaultPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           QStringLiteral("/settings.json");
}

SettingsStore::Loaded SettingsStore::load() const {
    Loaded result;
    QFile file(path_);
    if (!file.exists()) {
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        result.problem = makeError(ErrorCode::FileUnreadable,
                                   "Your settings could not be read, so the defaults are in use. "
                                   "Check that you can open " +
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
            "Your settings file was damaged, so Voxwright started with the defaults." +
                (kept ? " The damaged file was kept as " + aside.toStdString() + "." : ""),
            parseError.errorString().toStdString());
        return result;
    }
    const QJsonObject root = doc.object();
    if (root.value("version").toInt(0) > kFormatVersion) {
        result.problem = makeError(ErrorCode::SettingsFromNewerVersion,
                                   "Your settings were saved by a newer version of Voxwright. "
                                   "Settings this version does not know are ignored.");
    }
    result.settings = fromJson(root);
    return result;
}

Status SettingsStore::save(const AppSettings& settings) const {
    const QFileInfo info(path_);
    if (!QDir().mkpath(info.absolutePath())) {
        return makeError(ErrorCode::FileWriteFailed,
                         "Settings could not be saved: the folder " +
                             info.absolutePath().toStdString() +
                             " could not be created. Check the disk and its permissions.");
    }
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly)) {
        return makeError(ErrorCode::FileWriteFailed,
                         "Settings could not be saved to " + path_.toStdString() +
                             ". Check that the disk is not full and the file is not read-only.",
                         file.errorString().toStdString());
    }
    const QByteArray data = QJsonDocument(toJson(settings)).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size() || !file.commit()) {
        return makeError(ErrorCode::FileWriteFailed,
                         "Settings could not be saved to " + path_.toStdString() +
                             ". Check that the disk is not full.",
                         file.errorString().toStdString());
    }
    return {};
}

} // namespace vox::app
