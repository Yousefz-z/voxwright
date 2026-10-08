// Loads the real QML UI on the fake backend, clicks through it, checks that
// QML reports no warnings, and saves screenshots (to VOX_SCREENSHOT_DIR if
// set). Rendering is software, offscreen.

#include "app_test_support.hpp"

#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTest>

using namespace vox::app;
using namespace vox::app::test;

namespace {

/// Collects Qt warnings while alive (QML errors, binding loops, etc.).
class WarningCollector {
public:
    WarningCollector()
        : previous_(install(this)) {}
    WarningCollector(const WarningCollector&) = delete;
    WarningCollector& operator=(const WarningCollector&) = delete;
    WarningCollector(WarningCollector&&) = delete;
    WarningCollector& operator=(WarningCollector&&) = delete;
    ~WarningCollector() {
        qInstallMessageHandler(previous_);
        active() = nullptr;
    }
    [[nodiscard]] const QStringList& warnings() const { return warnings_; }

private:
    static WarningCollector*& active() {
        static WarningCollector* current = nullptr;
        return current;
    }
    static QtMessageHandler install(WarningCollector* collector) {
        active() = collector;
        return qInstallMessageHandler(&WarningCollector::handler);
    }
    static void handler(QtMsgType type, const QMessageLogContext& /*context*/,
                        const QString& message) {
        if (active() != nullptr && type != QtDebugMsg && type != QtInfoMsg) {
            active()->warnings_.append(message);
        }
    }
    QtMessageHandler previous_;
    QStringList warnings_;
};

/// Distinct colors in a downscaled copy: a blank or broken render has few.
int distinctColors(const QImage& image) {
    const QImage small = image.scaled(200, 130).convertToFormat(QImage::Format_RGB32);
    QSet<QRgb> colors;
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            colors.insert(small.pixel(x, y));
        }
    }
    return static_cast<int>(colors.size());
}

QImage grab(QQuickWindow* window, const QString& name) {
    QTest::qWait(250); // let meter and toggle animations settle
    QImage image = window->grabWindow();
    const QString dir = qEnvironmentVariable("VOX_SCREENSHOT_DIR");
    if (!dir.isEmpty()) {
        QDir().mkpath(dir);
        image.save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }
    return image;
}

/// Finds an item by objectName in the visual tree. View delegates are not
/// QObject children of the window, so QObject::findChild misses them.
QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (root == nullptr) {
        return nullptr;
    }
    if (root->objectName() == name) {
        return root;
    }
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

QQuickItem* findItem(QQuickWindow* window, const QString& name) {
    return findItem(window->contentItem(), name);
}

void click(QQuickWindow* window, QQuickItem* item) {
    const QPointF center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, {}, center.toPoint());
    // Views create delegates on the next polish and render pass.
    QTest::qWait(60);
}

} // namespace

TEST_CASE("The UI loads, switches voices and pages, and renders", "[app][ui]") {
    const WarningCollector collector;
    TestApp t;
    t.pump(0.3, vox::testing::sine(220.0, 0.5, 48000.0, 0.3F));
    QQmlApplicationEngine qml;
    qml.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&t.context())}});
    qml.loadFromModule(QStringLiteral("Voxwright"), QStringLiteral("Main"));
    REQUIRE(qml.rootObjects().size() == 1);
    auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().front());
    REQUIRE(window != nullptr);
    window->resize(1280, 820);
    window->show();
    REQUIRE(QTest::qWaitForWindowExposed(window));

    // Voices page.
    const QImage voices = grab(window, QStringLiteral("voices"));
    CHECK(voices.width() == 1280);
    CHECK(distinctColors(voices) > 40);

    // Narrow the grid with the search box, then click a tile.
    auto* search = findItem(window, QStringLiteral("voiceSearch"));
    REQUIRE(search != nullptr);
    search->setProperty("text", QStringLiteral("baritone"));
    QCoreApplication::processEvents();
    CHECK(t.context().voices()->voices()->count() >= 1);
    auto* tile = findItem(window, QStringLiteral("voiceTile_deep-baritone"));
    REQUIRE(tile != nullptr);
    click(window, tile);
    CHECK(t.context().voices()->currentVoiceId() == QStringLiteral("deep-baritone"));
    auto* name = findItem(window, QStringLiteral("currentVoiceName"));
    REQUIRE(name != nullptr);
    CHECK(name->property("text").toString() == QStringLiteral("Deep Baritone"));
    search->setProperty("text", QString{});
    t.pump(0.3, vox::testing::sine(220.0, 0.5, 48000.0, 0.3F));
    static_cast<void>(grab(window, QStringLiteral("voices-selected")));

    // Bottom bar toggle reaches the controller.
    auto* hear = findItem(window, QStringLiteral("hearMyselfToggle"));
    REQUIRE(hear != nullptr);
    click(window, hear);
    CHECK(t.context().audio()->hearMyself());

    // Audio page.
    auto* nav = findItem(window, QStringLiteral("navAudio"));
    REQUIRE(nav != nullptr);
    click(window, nav);
    t.pump(0.3, vox::testing::sine(220.0, 0.5, 48000.0, 0.3F));
    auto* latency = findItem(window, QStringLiteral("latencyValue"));
    REQUIRE(latency != nullptr);
    CHECK(latency->isVisible());
    CHECK(latency->property("text").toString().endsWith(QStringLiteral(" ms")));
    const QImage audio = grab(window, QStringLiteral("audio"));
    CHECK(distinctColors(audio) > 40);

    // Soundboard: click the first tile; it plays.
    REQUIRE(t.waitForSounds());
    click(window, findItem(window, QStringLiteral("navSoundboard")));
    auto* soundboard = t.context().soundboard();
    const quint32 first = soundboard->sounds()->index(0, 0).data(SoundListModel::SlotRole).toUInt();
    auto* soundTile = findItem(window, QStringLiteral("soundTile_%1").arg(first));
    REQUIRE(soundTile != nullptr);
    click(window, soundTile);
    CHECK(soundboard->playingCount() == 1);
    t.pump(0.2);
    const QImage sounds = grab(window, QStringLiteral("soundboard"));
    CHECK(distinctColors(sounds) > 40);
    soundboard->stopAll();
    t.pump(0.1);

    // Hotkeys page in push-to-talk mode shows the talk key field.
    click(window, findItem(window, QStringLiteral("navHotkeys")));
    click(window, findItem(window, QStringLiteral("transmitMode1")));
    CHECK(t.context().audio()->transmitMode() == 1);
    REQUIRE(t.context()
                .hotkeys()
                ->assignAction(QStringLiteral("talk"), QStringLiteral("F13"))
                .isEmpty());
    REQUIRE(t.context()
                .hotkeys()
                ->assignAction(QStringLiteral("stopSounds"), QStringLiteral("Ctrl+Alt+S"))
                .isEmpty());
    auto* talk = findItem(window, QStringLiteral("talkKey"));
    REQUIRE(talk != nullptr);
    CHECK(talk->isVisible());
    static_cast<void>(grab(window, QStringLiteral("hotkeys")));

    // The bottom bar fits at every width down to the minimum, sounds playing,
    // and keeps the chip labels while there is room for them.
    soundboard->press(first);
    auto* meter = findItem(window, QStringLiteral("inputMeter"));
    auto* bar = findItem(window, QStringLiteral("bottomBar"));
    REQUIRE(meter != nullptr);
    REQUIRE(bar != nullptr);
    for (const int width : {1280, 1180, 1100, 1040, 960}) {
        window->resize(width, 720);
        QTest::qWait(60);
        const QPointF meterEnd = meter->mapToScene(QPointF(meter->width(), 0));
        INFO("width " << width << ": meter ends at " << meterEnd.x());
        CHECK(meterEnd.x() <= window->width());
        if (width >= 1280) {
            CHECK_FALSE(bar->property("compact").toBool());
        }
    }
    CHECK(bar->property("compact").toBool());
    static_cast<void>(grab(window, QStringLiteral("compact")));
    soundboard->stopAll();

    INFO(collector.warnings().join(QLatin1Char('\n')).toStdString());
    CHECK(collector.warnings().isEmpty());
}

TEST_CASE("Notifications appear as banners and their actions run", "[app][ui]") {
    const WarningCollector collector;
    TestApp t({.cable = false});
    QQmlApplicationEngine qml;
    qml.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&t.context())}});
    qml.loadFromModule(QStringLiteral("Voxwright"), QStringLiteral("Main"));
    REQUIRE(qml.rootObjects().size() == 1);
    auto* window = qobject_cast<QQuickWindow*>(qml.rootObjects().front());
    REQUIRE(window != nullptr);
    window->resize(1280, 820);
    window->show();
    REQUIRE(QTest::qWaitForWindowExposed(window));
    REQUIRE(t.context().notifications()->contains(QStringLiteral("no-virtual-cable")));
    auto* status = findItem(window, QStringLiteral("statusLine"));
    REQUIRE(status != nullptr);
    CHECK(status->property("text").toString() == QStringLiteral("Not sent to other apps"));
    static_cast<void>(grab(window, QStringLiteral("no-virtual-cable")));

    // "open-audio" is a UI action: the window switches to the Audio page.
    t.context().runAction(QStringLiteral("open-audio"));
    QCoreApplication::processEvents();
    CHECK(window->property("page").toInt() == window->property("audioPage").toInt());

    INFO(collector.warnings().join(QLatin1Char('\n')).toStdString());
    CHECK(collector.warnings().isEmpty());
}
