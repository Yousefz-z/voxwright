#pragma once

#include <vox/core/result.hpp>
#include <vox/engine/types.hpp>

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace vox::app {

/// One sound on a board. `slot` is its slot in the engine (1 to 1023).
struct SoundEntry {
    std::uint32_t slot = 0;
    QString name;
    /// "builtin:<id>" for a sound that ships with Voxwright, otherwise the
    /// path of the copy in the sound library folder.
    QString source;
    QString color;
    QString icon;
    QString hotkey; ///< Portable key text, "" for none.
    engine::SoundOptions options;
};

struct Board {
    QString name;
    std::vector<SoundEntry> sounds;
};

struct SoundboardData {
    std::vector<Board> boards;
};

/// Reads and writes the soundboards as JSON (atomic writes, like settings).
class SoundboardStore {
public:
    explicit SoundboardStore(QString path);

    struct Loaded {
        SoundboardData data;
        std::optional<Error> problem;
        bool firstRun = false; ///< No file yet: the caller fills in the defaults.
    };
    [[nodiscard]] Loaded load() const;
    [[nodiscard]] Status save(const SoundboardData& data) const;

    /// The built-in sounds, one board per category.
    [[nodiscard]] static SoundboardData defaults();
    [[nodiscard]] static QString defaultPath();

private:
    QString path_;
};

[[nodiscard]] QString playModeName(engine::PlayMode mode);
[[nodiscard]] engine::PlayMode playModeFromName(const QString& name);

} // namespace vox::app
