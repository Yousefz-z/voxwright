// Test runner for the app layer: a QGuiApplication on the offscreen
// platform with the software renderer, so the UI renders without a display
// or GPU, and QStandardPaths in test mode so nothing touches real settings.

#include <catch2/catch_session.hpp>

#include <QGuiApplication>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(VoxwrightPlugin)

int main(int argc, char* argv[]) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QStandardPaths::setTestModeEnabled(true);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    const QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("VoxwrightTests"));
    return Catch::Session().run(argc, argv);
}
