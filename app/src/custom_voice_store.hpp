#pragma once

#include <vox/core/result.hpp>
#include <vox/plugins/registry.hpp>
#include <vox/plugins/voice_preset.hpp>

#include <QString>
#include <QStringList>

#include <vector>

namespace vox::app {

/// The user's own voices: one JSON file per voice in a folder, in the same
/// format as the built-in voices. Exported voices use the same format with
/// the .voxvoice extension.
class CustomVoiceStore {
public:
    explicit CustomVoiceStore(QString directory);

    struct Loaded {
        std::vector<plugins::VoicePreset> voices;
        /// One message per file that could not be loaded (each is set aside
        /// as <name>.damaged so it is not lost and not reported again).
        QStringList problems;
    };

    /// Reads every voice file, sorted by name.
    [[nodiscard]] Loaded load(const plugins::EffectRegistry& registry) const;
    [[nodiscard]] Status save(const plugins::VoicePreset& preset) const;
    [[nodiscard]] Status remove(const QString& id) const;

    [[nodiscard]] static Status exportTo(const plugins::VoicePreset& preset, const QString& path);
    /// Reads a voice file from anywhere. The voice keeps its contents; the
    /// caller gives it a new id.
    [[nodiscard]] static Result<plugins::VoicePreset>
    importFrom(const QString& path, const plugins::EffectRegistry& registry);

    [[nodiscard]] static QString defaultDirectory();
    /// Custom voice ids look like "custom-1a2b3c4d": safe as file names.
    [[nodiscard]] static bool isCustomId(const QString& id);
    [[nodiscard]] static QString fileExtension() { return QStringLiteral("voxvoice"); }

    [[nodiscard]] const QString& directory() const { return directory_; }

private:
    [[nodiscard]] QString pathOf(const QString& id) const;

    QString directory_;
};

} // namespace vox::app
