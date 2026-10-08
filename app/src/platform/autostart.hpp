#pragma once

#include <vox/core/result.hpp>

#include <QString>
#include <QStringList>

#include <memory>

namespace vox::app {

/// Starting the app when the user signs in. Each implementation writes the
/// standard per-user entry for its system; none needs administrator rights.
class Autostart {
public:
    Autostart() = default;
    Autostart(const Autostart&) = delete;
    Autostart& operator=(const Autostart&) = delete;
    Autostart(Autostart&&) = delete;
    Autostart& operator=(Autostart&&) = delete;
    virtual ~Autostart() = default;

    [[nodiscard]] virtual bool isEnabled() const = 0;
    /// Starts `program` with `arguments` at sign-in, or stops doing so.
    [[nodiscard]] virtual Status setEnabled(bool enabled, const QString& program,
                                            const QStringList& arguments) = 0;
    /// Where the entry lives, for messages ("the Run key", a file path).
    [[nodiscard]] virtual QString location() const = 0;
};

/// Windows: a value in HKEY_CURRENT_USER\...\CurrentVersion\Run. `settings`
/// is the registry path, or an INI file for tests.
[[nodiscard]] std::unique_ptr<Autostart> makeRunKeyAutostart(const QString& settings,
                                                             bool registry);
/// macOS: a LaunchAgent property list in `directory` (~/Library/LaunchAgents).
[[nodiscard]] std::unique_ptr<Autostart> makeLaunchAgentAutostart(const QString& directory);
/// Linux and other XDG desktops: a .desktop file in `directory`
/// (~/.config/autostart).
[[nodiscard]] std::unique_ptr<Autostart> makeXdgAutostart(const QString& directory);

/// The right one for this system, in the user's real locations.
[[nodiscard]] std::unique_ptr<Autostart> makePlatformAutostart();

/// Quotes one argument the way CommandLineToArgvW splits it (Run key).
[[nodiscard]] QString windowsArgument(const QString& argument);
/// Quotes one argument for the Exec key of a .desktop file (XDG Desktop
/// Entry rules, including the escaping of the value itself).
[[nodiscard]] QString desktopEntryArgument(const QString& argument);

} // namespace vox::app
