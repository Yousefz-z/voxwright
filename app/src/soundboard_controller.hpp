#pragma once

#include "hotkey_controller.hpp"
#include "notification_model.hpp"
#include "soundboard_store.hpp"

#include <vox/engine/audio_engine.hpp>

#include <QAbstractListModel>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <vector>

namespace vox::app {

/// The sounds of the board on screen.
class SoundListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum class LoadState { Loading, Ready, Failed };
    Q_ENUM(LoadState)

    enum Role {
        SlotRole = Qt::UserRole + 1,
        NameRole,
        ColorRole,
        IconRole,
        HotkeyRole,
        PlayingRole,
        LoadStateRole,
        ModeRole,
        LoopRole,
    };

    struct Row {
        std::uint32_t slot = 0;
        QString name;
        QString color;
        QString icon;
        QString hotkey; ///< Display text.
        bool playing = false;
        LoadState state = LoadState::Loading;
        QString mode;
        bool loop = false;
    };

    explicit SoundListModel(QObject* parent = nullptr);
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(rows_.size()); }

    void setRows(std::vector<Row> rows);
    /// Updates one sound's playing and load state in place.
    void setState(std::uint32_t slot, bool playing, LoadState state);

signals:
    void countChanged();

private:
    std::vector<Row> rows_;
};

/// Soundboards: boards of sounds, loading them into the engine, playing
/// them from the UI or hotkeys, importing files, and editing sounds.
class SoundboardController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(QStringList boards READ boardNames NOTIFY boardsChanged)
    Q_PROPERTY(int currentBoard READ currentBoard WRITE setCurrentBoard NOTIFY currentBoardChanged)
    Q_PROPERTY(vox::app::SoundListModel* sounds READ sounds CONSTANT)
    Q_PROPERTY(int playingCount READ playingCount NOTIFY playingChanged)
    Q_PROPERTY(int loadingCount READ loadingCount NOTIFY loadingChanged)
    Q_PROPERTY(QStringList fileFilters READ fileFilters CONSTANT)

public:
    struct Paths {
        QString store;   ///< soundboards.json
        QString library; ///< Folder that keeps copies of imported files.
    };

    SoundboardController(engine::AudioEngine& engine, HotkeyController& hotkeys,
                         NotificationModel& notifications, Paths paths, QObject* parent = nullptr);

    /// Loads the boards (the built-in sounds on first run) and starts
    /// loading every sound into the engine in the background.
    void initialize();

    Q_INVOKABLE void press(quint32 slot);
    Q_INVOKABLE void release(quint32 slot);
    Q_INVOKABLE void stopAll();
    /// Copies the files into the sound library and adds them to the current board.
    Q_INVOKABLE void importFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void removeSound(quint32 slot);
    /// Keys: name, mode, loop, gainDb, muteOthers, stopOthers, muteVoice, muteForMe.
    Q_INVOKABLE void updateSound(quint32 slot, const QVariantMap& changes);
    /// "" on success, otherwise why the hotkey cannot be used.
    Q_INVOKABLE QString setSoundHotkey(quint32 slot, const QString& sequence);
    Q_INVOKABLE QVariantMap soundDetails(quint32 slot) const;
    Q_INVOKABLE void addBoard(const QString& name);
    Q_INVOKABLE void renameBoard(int index, const QString& name);
    Q_INVOKABLE void removeBoard(int index);

    [[nodiscard]] QStringList boardNames() const;
    [[nodiscard]] int currentBoard() const { return current_; }
    void setCurrentBoard(int index);
    [[nodiscard]] SoundListModel* sounds() { return &model_; }
    [[nodiscard]] int playingCount() const { return static_cast<int>(playing_.size()); }
    [[nodiscard]] int loadingCount() const { return loading_; }
    [[nodiscard]] static QStringList fileFilters();
    [[nodiscard]] const SoundboardData& data() const { return data_; }
    [[nodiscard]] QString soundName(quint32 slot) const;

    /// The engine finished playing `slot` (forwarded from AudioController).
    void onSoundFinished(quint32 slot);
    [[nodiscard]] Status saveNow();

signals:
    void boardsChanged();
    void currentBoardChanged();
    void playingChanged();
    void loadingChanged();

private:
    struct Located {
        Board* board = nullptr;
        SoundEntry* sound = nullptr;
    };
    [[nodiscard]] Located find(std::uint32_t slot);
    [[nodiscard]] const SoundEntry* findEntry(std::uint32_t slot) const;
    [[nodiscard]] std::uint32_t freeSlot() const;
    void load(const SoundEntry& entry);
    void refreshModel();
    void setPlaying(std::uint32_t slot, bool playing);
    void scheduleSave();
    void forget(const SoundEntry& entry);

    engine::AudioEngine& engine_;
    HotkeyController& hotkeys_;
    NotificationModel& notifications_;
    SoundboardStore store_;
    QString library_;
    SoundboardData data_;
    int current_ = 0;
    SoundListModel model_;
    QHash<std::uint32_t, SoundListModel::LoadState> states_;
    QHash<std::uint32_t, quint64> generation_; ///< Ignores loads for removed sounds.
    QSet<std::uint32_t> playing_;
    int loading_ = 0;
    quint64 nextGeneration_ = 1;
    QTimer* saveTimer_ = nullptr;
};

} // namespace vox::app
