// Test runner for the app layer: a QApplication on the offscreen
// platform with the software renderer, so the UI renders without a display
// or GPU, and QStandardPaths in test mode so nothing touches real settings.

#include <catch2/catch_session.hpp>

#include <QApplication>
#include <QFont>
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
    const QApplication application(argc, argv); // the tray menu is a widget
    QApplication::setApplicationName(QStringLiteral("VoxwrightTests"));
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    // The offscreen platform's default font is "Sans Serif", which only
    // fontconfig resolves. On macOS Qt scans every family to find it and warns
    // about the cost, which the UI tests treat as a failure. Use the family
    // the UI itself asks for on this platform (Theme.qml).
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) {
        QFont font = QApplication::font();
#if defined(Q_OS_MACOS)
        font.setFamily(QStringLiteral("Helvetica Neue"));
#else
        font.setFamily(QStringLiteral("Segoe UI"));
#endif
        QApplication::setFont(font);
    }
#endif
    return Catch::Session().run(argc, argv);
}
