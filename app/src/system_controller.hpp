#pragma once

#include "platform/autostart.hpp"
#include "settings.hpp"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace vox::app {

/// How Voxwright fits into the desktop: starting at sign-in, starting
/// hidden in the tray, closing to the tray, and the first-run guide.
class SystemController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(bool startAtLogin READ startAtLogin NOTIFY changed)
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY changed)
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY changed)
    Q_PROPERTY(bool firstRunDone READ firstRunDone NOTIFY changed)
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)

public:
    /// The argument that starts the app hidden in the tray.
    [[nodiscard]] static QString minimizedArgument() { return QStringLiteral("--minimized"); }

    SystemController(Autostart& autostart, AppSettings& settings, QString program,
                     bool trayAvailable, QObject* parent = nullptr);

    /// Returns "" or why it could not be changed.
    Q_INVOKABLE QString setStartAtLogin(bool on);
    Q_INVOKABLE void finishFirstRun();
    Q_INVOKABLE void showFirstRunAgain();

    [[nodiscard]] bool startAtLogin() const { return autostart_.isEnabled(); }
    [[nodiscard]] bool startMinimized() const { return settings_.startMinimized; }
    void setStartMinimized(bool on);
    /// Closing the window keeps the app running only where there is a tray
    /// to bring it back from.
    [[nodiscard]] bool closeToTray() const { return trayAvailable_ && settings_.closeToTray; }
    void setCloseToTray(bool on);
    [[nodiscard]] bool firstRunDone() const { return settings_.firstRunDone; }
    [[nodiscard]] bool trayAvailable() const { return trayAvailable_; }
    /// "windows", "macos", or "linux", for platform-specific wording.
    [[nodiscard]] static QString platform();

signals:
    void changed();
    void settingsChanged();

private:
    [[nodiscard]] QStringList arguments() const;

    Autostart& autostart_;
    AppSettings& settings_;
    QString program_;
    bool trayAvailable_;
};

} // namespace vox::app
