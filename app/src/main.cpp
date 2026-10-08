#include "app_context.hpp"

#include <vox/devices/audio_backend.hpp>
#include <vox/devices/fake_backend.hpp>

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(VoxwrightPlugin)

int main(int argc, char* argv[]) {
    const QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Voxwright"));
    QGuiApplication::setOrganizationName(QStringLiteral("Voxwright"));
    QGuiApplication::setApplicationVersion(vox::app::AppContext::version());
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Voxwright/icons/app.svg")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    vox::app::AppContext::Options options;
    if (auto backend = vox::devices::createSystemBackend()) {
        options.backend = std::move(backend).value();
    } else {
        // Open the window anyway, without devices, so the user sees why.
        options.backend = std::make_unique<vox::devices::FakeBackend>();
        options.backendError = backend.error();
    }
    vox::app::AppContext context(std::move(options));

    QQmlApplicationEngine qml;
    qml.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&context)}});
    qml.loadFromModule(QStringLiteral("Voxwright"), QStringLiteral("Main"));
    if (qml.rootObjects().isEmpty()) {
        return 1;
    }
    return QGuiApplication::exec();
}
