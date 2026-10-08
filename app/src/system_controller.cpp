#include "system_controller.hpp"

#include <utility>

namespace vox::app {

SystemController::SystemController(Autostart& autostart, AppSettings& settings, QString program,
                                   bool trayAvailable, QObject* parent)
    : QObject(parent)
    , autostart_(autostart)
    , settings_(settings)
    , program_(std::move(program))
    , trayAvailable_(trayAvailable) {}

QStringList SystemController::arguments() const {
    return settings_.startMinimized && trayAvailable_ ? QStringList{minimizedArgument()}
                                                      : QStringList{};
}

QString SystemController::setStartAtLogin(bool on) {
    if (auto set = autostart_.setEnabled(on, program_, arguments()); !set) {
        emit changed();
        return QString::fromStdString(set.error().message);
    }
    emit changed();
    return {};
}

void SystemController::setStartMinimized(bool on) {
    if (on == settings_.startMinimized) {
        return;
    }
    settings_.startMinimized = on;
    if (autostart_.isEnabled()) {
        // The sign-in entry carries the argument, so rewrite it.
        static_cast<void>(autostart_.setEnabled(true, program_, arguments()));
    }
    emit changed();
    emit settingsChanged();
}

void SystemController::setCloseToTray(bool on) {
    if (on == settings_.closeToTray) {
        return;
    }
    settings_.closeToTray = on;
    emit changed();
    emit settingsChanged();
}

void SystemController::finishFirstRun() {
    if (!settings_.firstRunDone) {
        settings_.firstRunDone = true;
        emit changed();
        emit settingsChanged();
    }
}

void SystemController::showFirstRunAgain() {
    if (settings_.firstRunDone) {
        settings_.firstRunDone = false;
        emit changed();
        emit settingsChanged();
    }
}

QString SystemController::platform() {
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

} // namespace vox::app
