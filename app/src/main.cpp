#include "app_context.hpp"
#include "platform/single_instance.hpp"
#include "system_controller.hpp"

#include <vox/devices/audio_backend.hpp>
#include <vox/devices/fake_backend.hpp>

#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(VoxwrightPlugin)

int main(int argc, char* argv[]) {
    const QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Voxwright"));
    QApplication::setOrganizationName(QStringLiteral("Voxwright"));
    QApplication::setApplicationVersion(vox::app::AppContext::version());
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Voxwright/icons/app.svg")));
    // Closing the window may only hide it (tray); quitting is explicit.
    QApplication::setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // One Voxwright per user: starting it again shows the running window
    // instead of opening the devices a second time. A start at sign-in while
    // it already runs just ends.
    vox::app::SingleInstance instance(vox::app::SingleInstance::defaultKey());
    if (!instance.isPrimary()) {
        if (!QApplication::arguments().contains(vox::app::SystemController::minimizedArgument())) {
            static_cast<void>(instance.activatePrimary());
        }
        return 0;
    }

    vox::app::AppContext::Options options;
    if (auto backend = vox::devices::createSystemBackend()) {
        options.backend = std::move(backend).value();
    } else {
        // Open the window anyway, without devices, so the user sees why.
        options.backend = std::make_unique<vox::devices::FakeBackend>();
        options.backendError = backend.error();
    }
    vox::app::AppContext context(std::move(options));
    QObject::connect(&instance, &vox::app::SingleInstance::activationRequested, context.tray(),
                     &vox::app::TrayController::showWindowRequested);
    context.tray()->show();
    const bool startHidden =
        QApplication::arguments().contains(vox::app::SystemController::minimizedArgument()) &&
        context.tray()->available();

    QQmlApplicationEngine qml;
    qml.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&context)},
                              {QStringLiteral("startHidden"), startHidden}});
    qml.loadFromModule(QStringLiteral("Voxwright"), QStringLiteral("Main"));
    if (qml.rootObjects().isEmpty()) {
        return 1;
    }
    // Without a tray, closing the window is the only way out.
    if (!context.tray()->available()) {
        QApplication::setQuitOnLastWindowClosed(true);
    }
    return QApplication::exec();
}
