#pragma once

#include <vox/devices/audio_backend.hpp>

#include <QAbstractListModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace vox::app {

/// Devices of one kind for a picker. Optional leading entries stand for
/// "System default" (id "") and "None" (id "none").
class DeviceListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    /// Id of the "None" entry.
    [[nodiscard]] static QString noneId() { return QStringLiteral("none"); }

    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        IsDefaultRole,
        IsVirtualCableRole,
    };

    struct Options {
        bool systemDefaultEntry = true;
        bool noneEntry = false;
        bool virtualCablesFirst = false;
    };

    explicit DeviceListModel(Options options, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(rows_.size()); }

    void setDevices(const std::vector<devices::DeviceInfo>& devices);
    /// Row of the device with `id`, or -1.
    Q_INVOKABLE int indexOf(const QString& id) const;
    Q_INVOKABLE QString idAt(int row) const;
    [[nodiscard]] QString nameOf(const QString& id) const;
    [[nodiscard]] bool hasDevice(const QString& id) const { return indexOf(id) >= 0; }
    /// The first device recognised as a virtual cable, if any.
    [[nodiscard]] const devices::DeviceInfo* firstVirtualCable() const;
    /// The devices as last set, without the extra entries.
    [[nodiscard]] const std::vector<devices::DeviceInfo>& devices() const { return devices_; }

signals:
    void countChanged();

private:
    struct Row {
        QString id;
        QString name;
        bool isDefault = false;
        bool isVirtualCable = false;
    };
    Options options_;
    std::vector<Row> rows_;
    std::vector<devices::DeviceInfo> devices_;
};

} // namespace vox::app
