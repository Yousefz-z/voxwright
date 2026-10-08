#include "voice_models.hpp"

#include <QStringList>

#include <algorithm>
#include <array>

namespace vox::app {

VoiceListModel::VoiceListModel(QObject* parent)
    : QAbstractListModel(parent) {}

int VoiceListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() || voices_ == nullptr ? 0 : static_cast<int>(voices_->size());
}

QVariant VoiceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
        return {};
    }
    const auto& v = (*voices_)[static_cast<std::size_t>(index.row())];
    const QString id = QString::fromStdString(v.id);
    switch (role) {
    case IdRole:
        return id;
    case Qt::DisplayRole:
    case NameRole:
        return QString::fromStdString(v.name);
    case CategoryRole:
        return QString::fromStdString(v.category);
    case DescriptionRole:
        return QString::fromStdString(v.description);
    case IconRole:
        return QString::fromStdString(v.icon);
    case ColorRole:
        return QString::fromStdString(v.color);
    case TagsRole: {
        QStringList tags;
        for (const auto& t : v.tags) {
            tags.append(QString::fromStdString(t));
        }
        return tags.join(QLatin1Char(' '));
    }
    case FavoriteRole:
        return favorites_.contains(id);
    case ActiveRole:
        return id == active_;
    case CustomRole:
        return !v.builtIn;
    default:
        return {};
    }
}

QHash<int, QByteArray> VoiceListModel::roleNames() const {
    // "iconName" and "accentColor" avoid clashing with properties of the
    // Qt Quick Controls button the tile is built on.
    return {{IdRole, "voiceId"},        {NameRole, "name"},
            {CategoryRole, "category"}, {DescriptionRole, "description"},
            {IconRole, "iconName"},     {ColorRole, "accentColor"},
            {TagsRole, "tags"},         {FavoriteRole, "favorite"},
            {ActiveRole, "active"},     {CustomRole, "custom"}};
}

void VoiceListModel::setVoices(const std::vector<plugins::VoicePreset>* voices) {
    beginResetModel();
    voices_ = voices;
    endResetModel();
}

void VoiceListModel::setFavorites(const QSet<QString>& favorites) {
    favorites_ = favorites;
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {FavoriteRole});
    }
}

void VoiceListModel::setActive(const QString& id) {
    active_ = id;
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), {ActiveRole});
    }
}

VoiceFilterModel::VoiceFilterModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    connect(this, &QAbstractItemModel::rowsInserted, this, &VoiceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &VoiceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &VoiceFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &VoiceFilterModel::countChanged);
}

void VoiceFilterModel::setSearchText(const QString& text) {
    if (text == search_) {
        return;
    }
    search_ = text;
    invalidateFilter();
    emit filterChanged();
}

void VoiceFilterModel::setCategory(const QString& category) {
    if (category == category_) {
        return;
    }
    category_ = category;
    invalidateFilter();
    emit filterChanged();
}

QString VoiceFilterModel::idAt(int row) const {
    return data(index(row, 0), VoiceListModel::IdRole).toString();
}

int VoiceFilterModel::rowOf(const QString& id) const {
    if (rowCount() == 0) {
        return -1;
    }
    const QModelIndexList hits =
        match(index(0, 0), VoiceListModel::IdRole, id, 1, Qt::MatchExactly);
    return hits.isEmpty() ? -1 : hits.front().row();
}

bool VoiceFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QModelIndex i = sourceModel()->index(sourceRow, 0, sourceParent);
    if (category_ == favoritesCategory()) {
        if (!i.data(VoiceListModel::FavoriteRole).toBool()) {
            return false;
        }
    } else if (category_ == mineCategory()) {
        if (!i.data(VoiceListModel::CustomRole).toBool()) {
            return false;
        }
    } else if (!category_.isEmpty() &&
               i.data(VoiceListModel::CategoryRole).toString() != category_) {
        return false;
    }
    const QString needle = search_.trimmed();
    if (needle.isEmpty()) {
        return true;
    }
    constexpr std::array kSearched{VoiceListModel::NameRole, VoiceListModel::DescriptionRole,
                                   VoiceListModel::TagsRole, VoiceListModel::CategoryRole};
    return std::ranges::any_of(kSearched, [&](int role) {
        return i.data(role).toString().contains(needle, Qt::CaseInsensitive);
    });
}

} // namespace vox::app
