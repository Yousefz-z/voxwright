#include "app_test_support.hpp"
#include "soundboard_store.hpp"

#include <vox/plugins/sound_pack.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>
#include <vox/testing/wav.hpp>

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

using namespace vox::app;
using namespace vox::app::test;

namespace {

/// Slot of the first sound on the current board called `name`.
quint32 slotNamed(SoundboardController& board, const QString& name) {
    auto* model = board.sounds();
    for (int i = 0; i < model->count(); ++i) {
        const QModelIndex idx = model->index(i, 0);
        if (idx.data(SoundListModel::NameRole).toString() == name) {
            return idx.data(SoundListModel::SlotRole).toUInt();
        }
    }
    return 0;
}

int stateOf(SoundboardController& board, quint32 slot) {
    auto* model = board.sounds();
    for (int i = 0; i < model->count(); ++i) {
        const QModelIndex idx = model->index(i, 0);
        if (idx.data(SoundListModel::SlotRole).toUInt() == slot) {
            return idx.data(SoundListModel::LoadStateRole).toInt();
        }
    }
    return -1;
}

int boardIndex(SoundboardController& board, const QString& name) {
    return static_cast<int>(board.boardNames().indexOf(name));
}

} // namespace

TEST_CASE("First start fills the soundboard with the built-in sounds", "[app][soundboard]") {
    TestApp t;
    auto* board = t.context().soundboard();
    REQUIRE(t.waitForSounds());
    std::size_t total = 0;
    for (const auto& b : board->data().boards) {
        total += b.sounds.size();
    }
    CHECK(total == vox::plugins::builtinSounds().size());
    CHECK(board->boardNames().contains(QStringLiteral("Alerts")));
    for (int i = 0; i < board->sounds()->count(); ++i) {
        CHECK(board->sounds()->index(i, 0).data(SoundListModel::LoadStateRole).toInt() ==
              static_cast<int>(SoundListModel::LoadState::Ready));
    }
    REQUIRE(board->saveNow());
    CHECK(QFile::exists(t.dataDir() + QStringLiteral("/soundboards.json")));
}

TEST_CASE("A pressed sound plays into the virtual mic and reports when done", "[app][soundboard]") {
    TestApp t;
    auto* board = t.context().soundboard();
    REQUIRE(t.waitForSounds());
    board->setCurrentBoard(boardIndex(*board, QStringLiteral("Alerts")));
    const quint32 bleep = slotNamed(*board, QStringLiteral("Censor Bleep"));
    REQUIRE(bleep != 0);
    board->press(bleep);
    CHECK(board->playingCount() == 1);
    t.pump(0.3);
    CHECK(t.context().engine().stats().outputPeak > 0.05F);
    t.pump(1.0); // the bleep is 0.6 s long
    CHECK(board->playingCount() == 0);
}

TEST_CASE("Imported files are copied, decoded, and playable", "[app][soundboard]") {
    TestApp t;
    auto* board = t.context().soundboard();
    REQUIRE(t.waitForSounds());
    const QTemporaryDir source;
    const QString wav = source.filePath(QStringLiteral("My Clip.wav"));
    REQUIRE(vox::testing::writeWav(wav.toStdString(), vox::testing::sine(440.0, 0.5, 44100.0, 0.5F),
                                   44100));
    board->addBoard(QStringLiteral("Mine"));
    board->importFiles({QUrl::fromLocalFile(wav)});
    REQUIRE(t.waitForSounds());
    const quint32 slot = slotNamed(*board, QStringLiteral("My Clip"));
    REQUIRE(slot != 0);
    CHECK(stateOf(*board, slot) == static_cast<int>(SoundListModel::LoadState::Ready));
    const QStringList copies = QDir(t.dataDir() + QStringLiteral("/sounds")).entryList(QDir::Files);
    REQUIRE(copies.size() == 1);
    CHECK(copies.front().endsWith(QStringLiteral("My Clip.wav")));
    board->press(slot);
    t.pump(0.2);
    CHECK(t.context().engine().stats().outputPeak > 0.1F);

    // Removing it deletes the copy and frees the slot.
    board->removeSound(slot);
    CHECK(slotNamed(*board, QStringLiteral("My Clip")) == 0);
    CHECK(QDir(t.dataDir() + QStringLiteral("/sounds")).entryList(QDir::Files).isEmpty());
}

TEST_CASE("Import problems are reported per file", "[app][soundboard][errors]") {
    TestApp t;
    auto* board = t.context().soundboard();
    REQUIRE(t.waitForSounds());
    const QTemporaryDir source;
    const QString notes = source.filePath(QStringLiteral("notes.txt"));
    const QString broken = source.filePath(QStringLiteral("broken.wav"));
    for (const QString& path : {notes, broken}) {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("not audio");
    }
    board->addBoard(QStringLiteral("Imports"));
    board->importFiles({QUrl::fromLocalFile(notes), QUrl::fromLocalFile(broken),
                        QUrl::fromLocalFile(source.filePath(QStringLiteral("missing.mp3")))});
    REQUIRE(t.waitForSounds());
    auto* n = t.context().notifications();
    CHECK(n->messageFor(QStringLiteral("import-notes.txt")).contains(QStringLiteral("WAV, MP3")));
    CHECK(
        n->messageFor(QStringLiteral("import-missing.mp3")).contains(QStringLiteral("not found")));
    // The damaged WAV was added but cannot load; its tile says so.
    const quint32 slot = slotNamed(*board, QStringLiteral("broken"));
    REQUIRE(slot != 0);
    CHECK(stateOf(*board, slot) == static_cast<int>(SoundListModel::LoadState::Failed));
    CHECK(
        n->messageFor(QStringLiteral("sound-%1").arg(slot)).contains(QStringLiteral("broken.wav")));
}

TEST_CASE("Sound settings change and hold mode stops on release", "[app][soundboard]") {
    TestApp t;
    auto* board = t.context().soundboard();
    REQUIRE(t.waitForSounds());
    board->setCurrentBoard(boardIndex(*board, QStringLiteral("Ambience")));
    const quint32 crickets = slotNamed(*board, QStringLiteral("Crickets"));
    REQUIRE(crickets != 0);
    board->updateSound(crickets, {{QStringLiteral("mode"), QStringLiteral("hold")},
                                  {QStringLiteral("gainDb"), -6.0},
                                  {QStringLiteral("name"), QStringLiteral("Night")}});
    const auto details = board->soundDetails(crickets);
    CHECK(details.value(QStringLiteral("mode")).toString() == QStringLiteral("hold"));
    CHECK(details.value(QStringLiteral("gainDb")).toDouble() == -6.0);
    CHECK(details.value(QStringLiteral("name")).toString() == QStringLiteral("Night"));
    board->press(crickets);
    t.pump(1.0);
    CHECK(board->playingCount() == 1);
    board->release(crickets);
    t.pump(0.2);
    CHECK(board->playingCount() == 0);
}

TEST_CASE("Sound hotkeys play sounds and cannot clash", "[app][soundboard][hotkeys]") {
    TestApp t;
    auto& ctx = t.context();
    auto* board = ctx.soundboard();
    REQUIRE(t.waitForSounds());
    board->setCurrentBoard(boardIndex(*board, QStringLiteral("Alerts")));
    const quint32 bleep = slotNamed(*board, QStringLiteral("Censor Bleep"));
    const quint32 alarm = slotNamed(*board, QStringLiteral("Alarm"));
    REQUIRE(board->setSoundHotkey(bleep, QStringLiteral("Ctrl+1")).isEmpty());
    CHECK(board->setSoundHotkey(alarm, QStringLiteral("Ctrl+1"))
              .contains(QStringLiteral("Censor Bleep")));
    CHECK(ctx.hotkeys()
              ->assignAction(QStringLiteral("mute"), QStringLiteral("Ctrl+1"))
              .contains(QStringLiteral("Censor Bleep")));
    t.hotkeys().press(QKeyCombination(Qt::ControlModifier, Qt::Key_1), true);
    QCoreApplication::processEvents();
    CHECK(board->playingCount() == 1);
    t.pump(0.3);
    CHECK(ctx.engine().stats().outputPeak > 0.05F); // on the virtual-mic bus
    // Removing the sound frees its key.
    board->removeSound(bleep);
    CHECK(ctx.hotkeys()->assignAction(QStringLiteral("mute"), QStringLiteral("Ctrl+1")).isEmpty());
}

TEST_CASE("Boards can be added, renamed, and removed, but never all", "[app][soundboard]") {
    TestApp t;
    auto* board = t.context().soundboard();
    REQUIRE(t.waitForSounds());
    const auto before = board->boardNames().size();
    board->addBoard(QStringLiteral("Stream"));
    CHECK(board->boardNames().size() == before + 1);
    CHECK(board->currentBoard() == before);
    board->renameBoard(board->currentBoard(), QStringLiteral("Stream sounds"));
    CHECK(board->boardNames().back() == QStringLiteral("Stream sounds"));
    while (board->boardNames().size() > 1) {
        board->removeBoard(0);
    }
    board->removeBoard(0);
    CHECK(board->boardNames().size() == 1);
}

TEST_CASE("The soundboard store round-trips and recovers from damage", "[app][soundboard]") {
    const QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("soundboards.json"));
    const SoundboardStore store(path);
    SoundboardData data = SoundboardStore::defaults();
    data.boards.front().sounds.front().hotkey = QStringLiteral("Ctrl+Shift+F9");
    data.boards.front().sounds.front().options.mode = vox::engine::PlayMode::Overlap;
    data.boards.front().sounds.front().options.muteForMe = true;
    REQUIRE(store.save(data));
    const auto loaded = store.load();
    REQUIRE_FALSE(loaded.problem);
    REQUIRE(loaded.data.boards.size() == data.boards.size());
    const auto& s = loaded.data.boards.front().sounds.front();
    CHECK(s.hotkey == QStringLiteral("Ctrl+Shift+F9"));
    CHECK(s.options.mode == vox::engine::PlayMode::Overlap);
    CHECK(s.options.muteForMe);
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("{ broken");
    }
    const auto damaged = store.load();
    REQUIRE(damaged.problem);
    CHECK(damaged.problem.value_or(vox::Error{}).code == vox::ErrorCode::SettingsDamaged);
    CHECK(damaged.firstRun);
    CHECK(QFile::exists(path + QStringLiteral(".damaged")));
}
