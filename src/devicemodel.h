#pragma once

#include <QAbstractItemModel>
#include <QList>
#include "fritzdevice.h"

/**
 * DeviceModel is a two-level tree model:
 *
 *   Level 0 (root children)  — type-group header rows  (not selectable)
 *   Level 1 (group children) — individual FritzDevice leaf rows
 *
 * Columns are the same as the old flat table:
 *   0 Name | 1 Type | 2 Status | 3 Temperature | 4 Power | 5 Availability
 *
 * Internal pointer encoding:
 *   - Group-header index:  internalId() == 0xFFFFFFFF  (sentinel)
 *                          row()        == group index
 *   - Device leaf index:   internalId() == group index
 *                          row()        == device index within group
 */
class DeviceModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column {
        ColName = 0,
        ColType,
        ColStatus,
        ColTemperature,
        ColPower,
        ColPresent,
        ColumnCount
    };

    explicit DeviceModel(QObject *parent = nullptr);

    /// Replace the full device list and rebuild groups.
    void updateDevices(const FritzDeviceList &devices);

    /// Return the device for a valid leaf index (returns default FritzDevice for group rows).
    FritzDevice deviceAt(const QModelIndex &index) const;

    /// Find a device by AIN across all type buckets. Returns a default FritzDevice if not found.
    FritzDevice deviceByAin(const QString &ain) const;

    /// Find a device by internal numeric ID across all type buckets. Returns a default FritzDevice if not found.
    FritzDevice deviceById(const QString &id) const;

    /// Update producer/consumer status for a device and signal model update.
    void updateDeviceProducerStatus(const QString &ain, bool isProducer);

    /// True if index points to a group-header row (not a device).
    bool isGroupHeader(const QModelIndex &index) const;

    // QAbstractItemModel interface
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
    struct Group {
        QString      label;   ///< display name of the type bucket
        QString      iconName; ///< theme icon name for the bucket
        FritzDeviceList devices;
    };

    void rebuildGroups(const FritzDeviceList &devices);
    QString primaryTypeLabel(const FritzDevice &dev) const;
    QString primaryIconName(const FritzDevice &dev) const;
    QString deviceStatusString(const FritzDevice &dev) const;

    QList<Group> m_groups;

    static constexpr quintptr kGroupSentinel = static_cast<quintptr>(-1);
};
