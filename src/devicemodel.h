#pragma once

#include <QAbstractItemModel>
#include <QList>
#include "fritzdevice.h"
#include "localgroupmanager.h"

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

    /// Update the locally-defined groups; call after updateDevices() or
    /// whenever the LocalGroupManager emits groupsChanged().
    void setLocalGroups(const LocalGroupList &localGroups,
                        const FritzDeviceList &allFritzDevices);

    /// Return the device for a valid leaf index (returns default FritzDevice for group rows).
    FritzDevice deviceAt(const QModelIndex &index) const;

    /// Find a device by AIN across all type buckets. Returns a default FritzDevice if not found.
    FritzDevice deviceByAin(const QString &ain) const;

    /// Find a device by internal numeric ID across all type buckets. Returns a default FritzDevice if not found.
    FritzDevice deviceById(const QString &id) const;

    /// Update producer/consumer status for a device and signal model update.
    void updateDeviceProducerStatus(const QString &ain, bool isProducer);

    /// Update native net power status for a device and signal model update.
    void updateDeviceNativeNetPowerStatus(const QString &ain, bool nativeNetPower);

    /// True if index points to a group-header row (not a device).
    bool isGroupHeader(const QModelIndex &index) const;

    /// Get a device icon with battery overlay (if device has battery status).
    QIcon iconWithBatteryOverlay(const FritzDevice &dev) const;

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

    /// Compute the displayed (signed) power value for a device leaf.
    ///
    /// For ordinary devices this returns power negated when the device is
    /// flagged as a producer.  For native Fritz!Box (hardware) groups, the
    /// API-supplied energyStats.power is an unsigned sum that ignores the
    /// per-member producer flag, so this method recomputes the total by
    /// iterating the member AINs and applying the sign convention from
    /// QSettings (the authoritative producer-flag store, matching
    /// accumulateMemberEnergy()).  Local groups already carry signed power
    /// in their synthetic FritzDevice and are returned as-is.
    double signedPowerForDisplay(const FritzDevice &dev) const;

    QList<Group>    m_groups;
    FritzDeviceList m_lastFritzDevices; ///< last devices passed to updateDevices()
    LocalGroupList  m_localGroups;      ///< current local groups (for setLocalGroups())

    static constexpr quintptr kGroupSentinel = static_cast<quintptr>(-1);
};
