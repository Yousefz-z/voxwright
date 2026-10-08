#include "app_test_support.hpp"
#include "platform/autostart.hpp"
#include "system_controller.hpp"
#include "tray_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QXmlStreamReader>

using namespace vox::app;
using namespace vox::app::test;

namespace {

QString readAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{};
}

QAction* actionNamed(QMenu* menu, const QString& text) {
    for (QAction* a : menu->actions()) {
        if (a->text() == text) {
            return a;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Arguments are quoted the way Windows and desktop files split them", "[app][system]") {
    CHECK(windowsArgument(QStringLiteral("--minimized")) == QStringLiteral("--minimized"));
    CHECK(windowsArgument(QStringLiteral("C:\\Program Files\\Voxwright\\bin\\Voxwright.exe")) ==
          QStringLiteral("\"C:\\Program Files\\Voxwright\\bin\\Voxwright.exe\""));
    // Backslashes double only before a quote, including the closing one.
    CHECK(windowsArgument(QStringLiteral("C:\\dir with space\\")) ==
          QStringLiteral("\"C:\\dir with space\\\\\""));
    CHECK(windowsArgument(QStringLiteral("say \"hi\"")) == QStringLiteral("\"say \\\"hi\\\"\""));
    CHECK(windowsArgument({}) == QStringLiteral("\"\""));

    CHECK(desktopEntryArgument(QStringLiteral("/usr/bin/voxwright")) ==
          QStringLiteral("/usr/bin/voxwright"));
    CHECK(desktopEntryArgument(QStringLiteral("/opt/Voxwright App/voxwright")) ==
          QStringLiteral("\"/opt/Voxwright App/voxwright\""));
    CHECK(desktopEntryArgument(QStringLiteral("100%")) == QStringLiteral("100%%"));
    // Inside quotes a backslash is escaped, then escaped again as a string.
    CHECK(desktopEntryArgument(QStringLiteral("a\\b c")) == QStringLiteral("\"a\\\\\\\\b c\""));
    CHECK(desktopEntryArgument(QStringLiteral("$HOME")) == QStringLiteral("\"\\\\$HOME\""));
}

TEST_CASE("Start at sign-in writes and removes each system's entry", "[app][system]") {
    const QTemporaryDir dir;
    const QString program = QStringLiteral("/opt/Voxwright App/voxwright");
    const QStringList args{QStringLiteral("--minimized")};

    SECTION("XDG autostart file") {
        auto autostart = makeXdgAutostart(dir.filePath(QStringLiteral("autostart")));
        CHECK_FALSE(autostart->isEnabled());
        REQUIRE(autostart->setEnabled(true, program, args));
        CHECK(autostart->isEnabled());
        const QString text = readAll(autostart->location());
        CHECK(text.startsWith(QStringLiteral("[Desktop Entry]\n")));
        CHECK(text.contains(QStringLiteral("Exec=\"/opt/Voxwright App/voxwright\" --minimized\n")));
        REQUIRE(autostart->setEnabled(false, program, args));
        CHECK_FALSE(autostart->isEnabled());
        CHECK_FALSE(QFile::exists(autostart->location()));
    }
    SECTION("macOS LaunchAgent") {
        auto autostart = makeLaunchAgentAutostart(dir.filePath(QStringLiteral("LaunchAgents")));
        REQUIRE(autostart->setEnabled(true, program, args));
        CHECK(
            autostart->location().endsWith(QStringLiteral("io.github.yousefz-z.voxwright.plist")));
        // The arguments are separate strings, so no quoting is involved.
        QFile file(autostart->location());
        REQUIRE(file.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&file);
        QStringList strings;
        bool runAtLoad = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("string")) {
                strings.append(xml.readElementText());
            } else if (xml.isStartElement() && xml.name() == QStringLiteral("true")) {
                runAtLoad = true;
            }
        }
        CHECK_FALSE(xml.hasError());
        CHECK(strings.contains(program));
        CHECK(strings.contains(QStringLiteral("--minimized")));
        CHECK(runAtLoad);
        file.close(); // Windows cannot delete a file that is still open
        REQUIRE(autostart->setEnabled(false, program, args));
        CHECK_FALSE(autostart->isEnabled());
    }
    SECTION("Windows Run key (as an INI file here)") {
        const QString ini = dir.filePath(QStringLiteral("run.ini"));
        auto autostart = makeRunKeyAutostart(ini, false);
        REQUIRE(autostart->setEnabled(true, program, args));
        CHECK(autostart->isEnabled());
        const QSettings stored(ini, QSettings::IniFormat);
        // The program path is stored with the platform's separators.
        CHECK(stored.value(QStringLiteral("Voxwright")).toString() ==
              QLatin1Char('"') + QDir::toNativeSeparators(program) +
                  QStringLiteral("\" --minimized"));
        REQUIRE(autostart->setEnabled(false, program, args));
        CHECK_FALSE(autostart->isEnabled());
    }
    SECTION("A folder that cannot be created is reported") {
        QFile blocker(dir.filePath(QStringLiteral("file")));
        REQUIRE(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        auto autostart = makeXdgAutostart(dir.filePath(QStringLiteral("file/autostart")));
        const auto failed = autostart->setEnabled(true, program, args);
        REQUIRE_FALSE(failed);
        CHECK(failed.error().code == vox::ErrorCode::AutostartFailed);
        CHECK(failed.error().message.find("could not be created") != std::string::npos);
    }
}

TEST_CASE("Startup options follow the settings and rewrite the sign-in entry", "[app][system]") {
    const QTemporaryDir dir;
    auto autostart = makeXdgAutostart(dir.path());
    AppSettings settings;
    SystemController withTray(*autostart, settings, QStringLiteral("/usr/bin/voxwright"), true);
    REQUIRE(withTray.setStartAtLogin(true).isEmpty());
    CHECK_FALSE(readAll(autostart->location()).contains(QStringLiteral("--minimized")));
    withTray.setStartMinimized(true);
    CHECK(readAll(autostart->location()).contains(QStringLiteral("--minimized")));
    CHECK(withTray.closeToTray());
    withTray.setCloseToTray(false);
    CHECK_FALSE(settings.closeToTray);

    CHECK_FALSE(withTray.firstRunDone());
    const QSignalSpy saved(&withTray, &SystemController::settingsChanged);
    withTray.finishFirstRun();
    CHECK(settings.firstRunDone);
    CHECK(saved.count() == 1);
    withTray.showFirstRunAgain();
    CHECK_FALSE(settings.firstRunDone);

    // Without a tray there is nowhere to start hidden or close to.
    AppSettings plain;
    plain.startMinimized = true;
    SystemController noTray(*autostart, plain, QStringLiteral("/usr/bin/voxwright"), false);
    CHECK_FALSE(noTray.closeToTray());
    REQUIRE(noTray.setStartAtLogin(true).isEmpty());
    CHECK_FALSE(readAll(autostart->location()).contains(QStringLiteral("--minimized")));
}

TEST_CASE("Startup settings are saved and come back", "[app][system]") {
    AppSettings s;
    s.firstRunDone = true;
    s.closeToTray = false;
    s.startMinimized = true;
    s.speechVoice = QStringLiteral("kal");
    s.speechThroughVoice = true;
    const QTemporaryDir dir;
    const SettingsStore store(dir.filePath(QStringLiteral("settings.json")));
    REQUIRE(store.save(s));
    const auto loaded = store.load();
    REQUIRE_FALSE(loaded.problem);
    CHECK(loaded.settings.firstRunDone);
    CHECK_FALSE(loaded.settings.closeToTray);
    CHECK(loaded.settings.startMinimized);
    CHECK(loaded.settings.speechVoice == QStringLiteral("kal"));
    CHECK(loaded.settings.speechThroughVoice);
}

TEST_CASE("The tray menu mirrors the switches and lists favorite voices", "[app][system]") {
    TestApp t;
    auto& ctx = t.context();
    QMenu* menu = ctx.tray()->menu();
    REQUIRE(menu != nullptr);

    QAction* hear = actionNamed(menu, QStringLiteral("Hear myself"));
    REQUIRE(hear != nullptr);
    CHECK(hear->isChecked() == ctx.audio()->hearMyself());
    hear->trigger();
    CHECK(ctx.audio()->hearMyself() == hear->isChecked());
    ctx.audio()->setMuted(true);
    QAction* mute = actionNamed(menu, QStringLiteral("Mute microphone"));
    REQUIRE(mute != nullptr);
    CHECK(mute->isChecked());

    QAction* favorites = actionNamed(menu, QStringLiteral("Favorite voices"));
    REQUIRE(favorites != nullptr);
    REQUIRE(favorites->menu() != nullptr);
    CHECK_FALSE(favorites->menu()->actions().front()->isEnabled()); // the hint
    ctx.voices()->toggleFavorite(QStringLiteral("cathedral"));
    QAction* cathedral = actionNamed(favorites->menu(), QStringLiteral("Cathedral"));
    REQUIRE(cathedral != nullptr);
    cathedral->trigger();
    CHECK(ctx.voices()->currentVoiceId() == QStringLiteral("cathedral"));

    const QSignalSpy quit(ctx.tray(), &TrayController::quitRequested);
    QAction* quitAction = actionNamed(menu, QStringLiteral("Quit Voxwright"));
    REQUIRE(quitAction != nullptr);
    quitAction->trigger();
    CHECK(quit.count() == 1);
}

TEST_CASE("Resetting puts every setting back and offers a restart", "[app][system]") {
    TestApp t;
    auto& ctx = t.context();
    ctx.voices()->toggleFavorite(QStringLiteral("cathedral"));
    ctx.audio()->setHearMyself(true);
    REQUIRE(ctx.hotkeys()->assignAction(QStringLiteral("mute"), QStringLiteral("F3")).isEmpty());
    REQUIRE(ctx.saveNow());

    ctx.runAction(QStringLiteral("reset-settings"));
    CHECK(ctx.settings().favorites.isEmpty());
    CHECK_FALSE(ctx.settings().hearMyself);
    CHECK(ctx.settings().firstRunDone); // the guide does not pop up
    CHECK(t.hotkeys().bindings().empty());
    REQUIRE(ctx.notifications()->contains(QStringLiteral("settings-reset")));
    const QString saved = readAll(t.settingsPath());
    CHECK_FALSE(saved.contains(QStringLiteral("cathedral")));
}
