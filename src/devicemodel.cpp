#include "devicemodel.h"
#include <QColor>
#include <QFont>
#include <QIcon>
#include <algorithm>
#include "i18n_shim.h"

// ---------------------------------------------------------------------------
// Helpers: primary type label and icon for a device (drives bucket assignment)
// ---------------------------------------------------------------------------

QString DeviceModel::primaryTypeLabel(const FritzDevice &dev) const
{
    switch (dev.primaryType()) {
    case FritzDevice::PrimaryType::Group:           return i18n("Groups");
    case FritzDevice::PrimaryType::ColorBulb:       return i18n("Color Bulbs");
    case FritzDevice::PrimaryType::Dimmer:          return i18n("Dimmers");
    case FritzDevice::PrimaryType::SmartPlug:       return i18n("Smart Plugs");
    case FritzDevice::PrimaryType::Switch:          return i18n("Switches");
    case FritzDevice::PrimaryType::Thermostat:      return i18n("Thermostats");
    case FritzDevice::PrimaryType::Blind:           return i18n("Blinds");
    case FritzDevice::PrimaryType::Alarm:           return i18n("Alarms");
    case FritzDevice::PrimaryType::HumiditySensor:  return i18n("Humidity Sensors");
    case FritzDevice::PrimaryType::Sensor:          return i18n("Sensors");
    }
    return i18n("Sensors");
}

QString DeviceModel::primaryIconName(const FritzDevice &dev) const
{
    return dev.iconPath();
}

// ---------------------------------------------------------------------------
// Group rebuild
// ---------------------------------------------------------------------------

void DeviceModel::rebuildGroups(const FritzDeviceList &devices)
{
    m_groups.clear();

    // Preserve insertion order of buckets (first device of each type seen).
    QList<QString> order;
    QMap<QString, Group> map;

    for (const FritzDevice &dev : devices) {
        QString label = primaryTypeLabel(dev);
        if (!map.contains(label)) {
            order.append(label);
            Group g;
            g.label    = label;
            g.iconName = primaryIconName(dev);
            map[label] = g;
        }
        map[label].devices.append(dev);
    }

    for (const QString &label : order) {
        Group g = map[label];
        std::sort(g.devices.begin(), g.devices.end(),
                  [](const FritzDevice &a, const FritzDevice &b) {
                      return a.name.toLower() < b.name.toLower();
                  });
        m_groups.append(g);
    }
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

DeviceModel::DeviceModel(QObject *parent)
    : QAbstractItemModel(parent)
{}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void DeviceModel::updateDevices(const FritzDeviceList &devices)
{
    beginResetModel();
    rebuildGroups(devices);
    endResetModel();
}

FritzDevice DeviceModel::deviceAt(const QModelIndex &index) const
{
    if (!index.isValid() || isGroupHeader(index))
        return FritzDevice{};
    int gi = static_cast<int>(index.internalId());
    if (gi < 0 || gi >= m_groups.size())
        return FritzDevice{};
    const Group &g = m_groups.at(gi);
    if (index.row() < 0 || index.row() >= g.devices.size())
        return FritzDevice{};
    return g.devices.at(index.row());
}

bool DeviceModel::isGroupHeader(const QModelIndex &index) const
{
    return index.isValid() && index.internalId() == kGroupSentinel;
}

FritzDevice DeviceModel::deviceByAin(const QString &ain) const
{
    for (const Group &g : m_groups) {
        for (const FritzDevice &dev : g.devices) {
            if (dev.ain == ain)
                return dev;
        }
    }
    return FritzDevice{};
}

FritzDevice DeviceModel::deviceById(const QString &id) const
{
    for (const Group &g : m_groups) {
        for (const FritzDevice &dev : g.devices) {
            if (dev.id == id)
                return dev;
        }
    }
    return FritzDevice{};
}

// ---------------------------------------------------------------------------
// QAbstractItemModel interface
// ---------------------------------------------------------------------------

QModelIndex DeviceModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    if (!parent.isValid()) {
        // Root level → group header row
        return createIndex(row, column, kGroupSentinel);
    }

    // Parent is a group header → device leaf
    int gi = parent.row();
    return createIndex(row, column, static_cast<quintptr>(gi));
}

QModelIndex DeviceModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return QModelIndex();

    if (child.internalId() == kGroupSentinel)
        return QModelIndex();   // group headers have no parent

    // Device leaf: parent is the group header
    int gi = static_cast<int>(child.internalId());
    return createIndex(gi, 0, kGroupSentinel);
}

int DeviceModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_groups.size();

    if (parent.internalId() == kGroupSentinel)
        return m_groups.at(parent.row()).devices.size();

    return 0; // device leaves have no children
}

int DeviceModel::columnCount(const QModelIndex & /*parent*/) const
{
    return ColumnCount;
}

Qt::ItemFlags DeviceModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    if (isGroupHeader(index))
        return Qt::ItemIsEnabled;   // not selectable
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    // ── Group header row ──────────────────────────────────────────────────
    if (isGroupHeader(index)) {
        const Group &g = m_groups.at(index.row());
        if (role == Qt::DisplayRole && index.column() == ColName)
            return QString("%1  (%2)").arg(g.label).arg(g.devices.size());
        if (role == Qt::UserRole)   // raw label, used for expand-state restore
            return g.label;
        if (role == Qt::DecorationRole && index.column() == ColName)
            return QIcon(g.iconName);
        if (role == Qt::FontRole) {
            QFont f;
            f.setBold(true);
            return f;
        }
        if (role == Qt::ForegroundRole)
            return QColor(Qt::darkGray);
        return QVariant();
    }

    // ── Device leaf row ───────────────────────────────────────────────────
    int gi = static_cast<int>(index.internalId());
    const FritzDevice &dev = m_groups.at(gi).devices.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName:   return dev.name;
        case ColType:   return primaryTypeLabel(dev).chopped(0); // reuse label (singular would be nicer but consistent)
        case ColStatus: return deviceStatusString(dev);
        case ColTemperature:
            if (dev.hasTemperature() && dev.temperature > -273.0)
                return QString("%1 °C").arg(dev.temperature, 0, 'f', 1);
            if (dev.hasThermostat()) {
                int raw = dev.thermostatStats.currentTemp;
                if (raw > 0 && raw < 255)
                    return QString("%1 °C").arg((raw / 2.0) + 8.0, 0, 'f', 1);
            }
            return QString("-");
        case ColPower:
            if (dev.hasEnergyMeter() && dev.energyStats.valid) {
                const double power = dev.isProducer ? -dev.energyStats.power : dev.energyStats.power;
                return QString("%1 W").arg(power, 0, 'f', 1);
            }
            return QString("-");
        case ColPresent: return dev.present ? i18n("Online") : i18n("Offline");
        }
    }

    if (role == Qt::ForegroundRole) {
        if (index.column() == ColPresent)
            return dev.present ? QColor(Qt::darkGreen) : QColor(Qt::red);
        if (index.column() == ColStatus && dev.hasAlarm() && dev.alarmStats.triggered)
            return QColor(Qt::red);
    }

    if (role == Qt::DecorationRole && index.column() == ColName)
        return QIcon(primaryIconName(dev));

    if (role == Qt::ToolTipRole) {
        QString tip = QString("<b>%1</b><br/>").arg(dev.name);
        tip += QString("AIN: %1<br/>").arg(dev.identifier);
        tip += QString("Product: %1 (%2)<br/>").arg(dev.productname, dev.manufacturer);
        tip += QString("Firmware: %1<br/>").arg(dev.fwversion);
        if (dev.hasSwitch())
            tip += QString("Switch: %1<br/>").arg(dev.switchStats.on ? i18n("On") : i18n("Off"));
        if (dev.hasEnergyMeter() && dev.energyStats.valid) {
            const double sign = dev.isProducer ? -1.0 : 1.0;
            tip += QString("Power: %1 W<br/>").arg(sign * dev.energyStats.power, 0, 'f', 1);
            tip += QString("Energy: %1 Wh<br/>").arg(sign * dev.energyStats.energy, 0, 'f', 0);
            tip += QString("Voltage: %1 V<br/>").arg(dev.energyStats.voltage, 0, 'f', 1);
        }
        if (dev.hasThermostat()) {
            auto t2c = [](int raw) -> QString {
                if (raw == 253) return i18n("Off");
                if (raw == 254) return i18n("Comfort");
                if (raw >= 16 && raw <= 56)
                    return QString("%1 °C").arg(8.0 + (raw - 16) * 0.5, 0, 'f', 1);
                return QString::number(raw);
            };
            tip += QString("Target: %1<br/>").arg(t2c(dev.thermostatStats.targetTemp));
            tip += QString("Battery: %1%<br/>").arg(dev.thermostatStats.battery);
        }
        if (dev.hasHumidity() && dev.humidityStats.valid)
            tip += QString("Humidity: %1%<br/>").arg(dev.humidityStats.humidity);
        return tip;
    }

    return QVariant();
}

QVariant DeviceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    switch (section) {
    case ColName:        return i18n("Name");
    case ColType:        return i18n("Type");
    case ColStatus:      return i18n("Status");
    case ColTemperature: return i18n("Temperature");
    case ColPower:       return i18n("Power");
    case ColPresent:     return i18n("Availability");
    }
    return QVariant();
}

// ---------------------------------------------------------------------------
// Producer/Consumer status management
// ---------------------------------------------------------------------------

void DeviceModel::updateDeviceProducerStatus(const QString &ain, bool isProducer)
{
    // Find the device and update its producer status
    for (int gi = 0; gi < m_groups.size(); ++gi) {
        for (int di = 0; di < m_groups[gi].devices.size(); ++di) {
            if (m_groups[gi].devices[di].ain == ain) {
                m_groups[gi].devices[di].isProducer = isProducer;
                // Emit dataChanged for this cell
                QModelIndex idx = index(di, 0, index(gi, 0));
                emit dataChanged(idx, idx);
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Status helper
// ---------------------------------------------------------------------------

QString DeviceModel::deviceStatusString(const FritzDevice &dev) const
{
    if (!dev.present) return i18n("Offline");
    if (dev.hasAlarm() && dev.alarmStats.triggered) return i18n("ALARM");
    if (dev.hasSwitch()) return dev.switchStats.on ? i18n("On") : i18n("Off");
    if (dev.hasThermostat()) {
        int raw = dev.thermostatStats.targetTemp;
        if (raw == 253) return i18n("Off");
        if (raw == 254) return i18n("On (Comfort)");
        if (raw >= 16 && raw <= 56)
            return QString("%1 °C").arg(8.0 + (raw - 16) * 0.5, 0, 'f', 1);
    }
    if (dev.hasDimmer()) return QString("%1%").arg(dev.dimmerStats.levelPercent);
    if (dev.hasBlind())  return dev.blindStats.mode;
    return i18n("Active");
}
