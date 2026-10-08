#pragma once

#include <vox/plugins/voice_preset.hpp>

#include <QAbstractListModel>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace vox::app {

/// Every voice, with its favorite and active state.
class VoiceListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        CategoryRole,
        DescriptionRole,
        IconRole,
        ColorRole,
        TagsRole,
        FavoriteRole,
        ActiveRole,
    };

    explicit VoiceListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setVoices(const std::vector<plugins::VoicePreset>* voices);
    void setFavorites(const QSet<QString>& favorites);
    void setActive(const QString& id);

private:
    const std::vector<plugins::VoicePreset>* voices_ = nullptr;
    QSet<QString> favorites_;
    QString active_;
};

/// The voice grid's view of the list: text search and category filter.
class VoiceFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filterChanged)
    /// A category name, "" for all, or favoritesCategory().
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    /// The category value that shows favorites only.
    [[nodiscard]] static QString favoritesCategory() { return QStringLiteral("favorites"); }

    explicit VoiceFilterModel(QObject* parent = nullptr);

    [[nodiscard]] QString searchText() const { return search_; }
    void setSearchText(const QString& text);
    [[nodiscard]] QString category() const { return category_; }
    void setCategory(const QString& category);
    [[nodiscard]] int count() const { return rowCount(); }
    Q_INVOKABLE QString idAt(int row) const;
    /// Row of the voice with `id` in the filtered list, or -1.
    Q_INVOKABLE int rowOf(const QString& id) const;

signals:
    void filterChanged();
    void countChanged();

protected:
    [[nodiscard]] bool filterAcceptsRow(int sourceRow,
                                        const QModelIndex& sourceParent) const override;

private:
    QString search_;
    QString category_;
};

} // namespace vox::app
