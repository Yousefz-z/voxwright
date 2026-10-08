#include "notification_model.hpp"

#include <algorithm>

namespace vox::app {

NotificationModel::NotificationModel(QObject* parent)
    : QAbstractListModel(parent) {}

int NotificationModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant NotificationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Item& item = items_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case IdRole:
        return item.key;
    case LevelRole:
        return static_cast<int>(item.level);
    case TitleRole:
        return item.title;
    case MessageRole:
        return item.message;
    case ActionLabelRole:
        return item.actionLabel;
    case ActionRole:
        return item.action;
    default:
        return {};
    }
}

QHash<int, QByteArray> NotificationModel::roleNames() const {
    return {{IdRole, "key"},
            {LevelRole, "level"},
            {TitleRole, "title"},
            {MessageRole, "message"},
            {ActionLabelRole, "actionLabel"},
            {ActionRole, "action"}};
}

void NotificationModel::post(const QString& key, Level level, const QString& title,
                             const QString& message, const QString& actionLabel,
                             const QString& action) {
    Item item{key, level, title, message, actionLabel, action};
    const auto it =
        std::find_if(items_.begin(), items_.end(), [&key](const Item& i) { return i.key == key; });
    if (it != items_.end()) {
        *it = std::move(item);
        const QModelIndex changed = index(static_cast<int>(it - items_.begin()));
        emit dataChanged(changed, changed);
        return;
    }
    beginInsertRows({}, 0, 0); // newest first
    items_.insert(items_.begin(), std::move(item));
    endInsertRows();
    emit countChanged();
}

void NotificationModel::dismiss(const QString& key) {
    const auto it =
        std::find_if(items_.begin(), items_.end(), [&key](const Item& i) { return i.key == key; });
    if (it == items_.end()) {
        return;
    }
    const int row = static_cast<int>(it - items_.begin());
    beginRemoveRows({}, row, row);
    items_.erase(it);
    endRemoveRows();
    emit countChanged();
}

bool NotificationModel::contains(const QString& key) const {
    return std::any_of(items_.begin(), items_.end(),
                       [&key](const Item& i) { return i.key == key; });
}

QString NotificationModel::messageFor(const QString& key) const {
    const auto it =
        std::find_if(items_.begin(), items_.end(), [&key](const Item& i) { return i.key == key; });
    return it == items_.end() ? QString{} : it->message;
}

} // namespace vox::app
