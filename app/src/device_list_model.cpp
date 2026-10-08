#include "device_list_model.hpp"

#include <algorithm>

namespace vox::app {

DeviceListModel::DeviceListModel(Options options, QObject* parent)
    : QAbstractListModel(parent)
    , options_(options) {}

int DeviceListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant DeviceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Row& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case IdRole:
        return row.id;
    case Qt::DisplayRole:
    case NameRole:
        return row.name;
    case IsDefaultRole:
        return row.isDefault;
    case IsVirtualCableRole:
        return row.isVirtualCable;
    default:
        return {};
    }
}

QHash<int, QByteArray> DeviceListModel::roleNames() const {
    return {{IdRole, "deviceId"},
            {NameRole, "name"},
            {IsDefaultRole, "isDefault"},
            {IsVirtualCableRole, "isVirtualCable"}};
}

void DeviceListModel::setDevices(const std::vector<devices::DeviceInfo>& devices) {
    beginResetModel();
    devices_ = devices;
    rows_.clear();
    if (options_.noneEntry) {
        rows_.push_back({noneId(), tr("None"), false, false});
    }
    if (options_.systemDefaultEntry) {
        const auto def = std::find_if(devices.begin(), devices.end(),
                                      [](const devices::DeviceInfo& d) { return d.isDefault; });
        const QString name = def == devices.end()
                                 ? tr("System default")
                                 : tr("System default (%1)").arg(QString::fromStdString(def->name));
        rows_.push_back({QString{}, name, false, false});
    }
    std::vector<devices::DeviceInfo> sorted = devices;
    if (options_.virtualCablesFirst) {
        std::stable_partition(sorted.begin(), sorted.end(),
                              [](const devices::DeviceInfo& d) { return d.isVirtualCable; });
    }
    for (const auto& d : sorted) {
        rows_.push_back({QString::fromStdString(d.id), QString::fromStdString(d.name), d.isDefault,
                         d.isVirtualCable});
    }
    endResetModel();
    emit countChanged();
}

int DeviceListModel::indexOf(const QString& id) const {
    const auto it =
        std::find_if(rows_.begin(), rows_.end(), [&id](const Row& r) { return r.id == id; });
    return it == rows_.end() ? -1 : static_cast<int>(it - rows_.begin());
}

QString DeviceListModel::idAt(int row) const {
    return row >= 0 && row < count() ? rows_[static_cast<std::size_t>(row)].id : QString{};
}

QString DeviceListModel::nameOf(const QString& id) const {
    const int row = indexOf(id);
    return row < 0 ? QString{} : rows_[static_cast<std::size_t>(row)].name;
}

const devices::DeviceInfo* DeviceListModel::firstVirtualCable() const {
    const auto it = std::find_if(devices_.begin(), devices_.end(),
                                 [](const devices::DeviceInfo& d) { return d.isVirtualCable; });
    return it == devices_.end() ? nullptr : &*it;
}

} // namespace vox::app
