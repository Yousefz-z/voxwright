#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace vox::app {

/// Banners shown at the top of the window: device problems, errors with
/// what to do about them, and short confirmations. A notification with the
/// same key replaces the previous one instead of stacking.
class NotificationModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum class Level { Info, Warning, Error };
    Q_ENUM(Level)

    enum Role {
        IdRole = Qt::UserRole + 1,
        LevelRole,
        TitleRole,
        MessageRole,
        ActionLabelRole,
        ActionRole,
    };

    explicit NotificationModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(items_.size()); }

    /// Shows a notification; `key` identifies it for replacement and
    /// dismissal (for example "device-input"). `action` is an identifier
    /// the UI handles, shown as a button labelled `actionLabel`.
    void post(const QString& key, Level level, const QString& title, const QString& message,
              const QString& actionLabel = {}, const QString& action = {});
    Q_INVOKABLE void dismiss(const QString& key);
    [[nodiscard]] bool contains(const QString& key) const;
    [[nodiscard]] QString messageFor(const QString& key) const;

signals:
    void countChanged();

private:
    struct Item {
        QString key;
        Level level = Level::Info;
        QString title;
        QString message;
        QString actionLabel;
        QString action;
    };
    std::vector<Item> items_;
};

} // namespace vox::app
