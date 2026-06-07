#include "devicemodel.h"
#include "localgroupmanager.h"
#include "chartutils.h"
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QSet>
#include <QSettings>
#include <QPainter>
#include <algorithm>
#include <functional>
#include "i18n_shim.h"

// QSettings key for the per-device producer flag (shared with mainwindow.cpp
// via this constant; both sites must use the same key to read/write correctly).
// Full path: "devices/<ain>/isProducer"
static const char *kSettingsKeyIsProducer = "isProducer";

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
// Battery overlay icon — composite icon with battery fill level visualization
// ---------------------------------------------------------------------------

QIcon DeviceModel::iconWithBatteryOverlay(const FritzDevice &dev) const
{
    if (!dev.hasBattery() || dev.batteryStats.level < 0) {
        // No battery data; return plain device icon
        return QIcon(primaryIconName(dev));
    }

    // Load base device icon
    QIcon baseIcon(primaryIconName(dev));
    QPixmap basePixmap = baseIcon.pixmap(QSize(32, 32));

    if (basePixmap.isNull())
        return baseIcon;  // fallback

    // Create composite pixmap (same size as base)
    QPixmap composite(basePixmap.size());
    composite.fill(Qt::transparent);

    // Draw base icon
    {
        QPainter p(&composite);
        p.drawPixmap(0, 0, basePixmap);
    }

    // Draw battery icon with fill level in bottom-right corner
    {
        // Determine battery color based on battery level (uses 5-state normalization)
        QColor fillColor = batteryColorForLevel(dev.batteryStats.level);

        // Battery icon dimensions: horizontal battery in the bottom-right corner.
        int batWidth = 18;
        int batHeight = 12;
        int batX = composite.width() - batWidth - 1;
        int batY = composite.height() - batHeight - 1;

        QPainter p(&composite);
        p.setRenderHint(QPainter::Antialiasing);

        // Draw battery body background (white)
        QRect bodyRect(batX, batY, batWidth, batHeight);
        p.fillRect(bodyRect, Qt::white);

        // Draw battery outline (black border)
        p.setPen(QPen(Qt::black, 1));
        p.drawRect(bodyRect);

        // Draw battery terminal on the left to make the overlay horizontal.
        QRect terminalRect(batX - 3,
                           batY + (batHeight / 2) - 3,
                           2,
                           6);
        p.fillRect(terminalRect, Qt::darkGray);

        // Draw fill level based on percentage.
        int fillWidth = (bodyRect.width() - 2) * dev.batteryStats.level / 100;
        if (fillWidth > 0) {
            QRect fillRect(bodyRect.x() + (bodyRect.width() - fillWidth - 1),  // right-align fill
                           bodyRect.y() + 1,
                           fillWidth,
                           bodyRect.height() - 2);
            p.fillRect(fillRect, fillColor);

            // Draw a thin border around the fill for definition.
            p.setPen(QPen(fillColor.darker(120), 0.5));
            p.drawRect(fillRect);
        }
    }

    return QIcon(composite);
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
    m_lastFritzDevices = devices;
    rebuildGroups(devices);
    endResetModel();
}

// ---------------------------------------------------------------------------
// Local groups: synthesise fake FritzDevice entries and add them to their own
// "Local Groups" bucket at the bottom of the tree.
// ---------------------------------------------------------------------------

/// Recursively collect member FritzDevices for a local group.
/// @p visited tracks local-group IDs already on the call stack to detect and
/// break cycles; devices are deduplicated by AIN across all recursive paths.
static FritzDeviceList collectLocalGroupMembers(
    const LocalGroup &lg,
    const FritzDeviceList &allFritz,
    const LocalGroupList &allLocal,
    QSet<QString> &visited,
    QSet<QString> &seenAins)
{
    // Cycle guard: if this group is already being expanded upstream, skip it.
    if (visited.contains(lg.id))
        return {};
    visited.insert(lg.id);

    FritzDeviceList result;
    for (const QString &ain : lg.memberAins) {
        if (ain.startsWith(QStringLiteral("local:"))) {
            const QString memberId = ain.mid(6);
            for (const LocalGroup &sub : allLocal) {
                if (sub.id == memberId) {
                    result += collectLocalGroupMembers(sub, allFritz, allLocal, visited, seenAins);
                    break;
                }
            }
        } else {
            for (const FritzDevice &dev : allFritz) {
                if (dev.ain != ain)
                    continue;
                if (!dev.isGroup()) {
                    // Deduplicate: skip if this device was already added via
                    // another path.
                    if (!seenAins.contains(dev.ain)) {
                        seenAins.insert(dev.ain);
                        result.append(dev);
                    }
                } else {
                    // Member is a native Fritz!Box group — include the group
                    // device itself so its aggregated capabilities (temperature,
                    // energy meter, etc.) and live stats are reflected in the
                    // local group's capability union and power display.
                    if (!seenAins.contains(dev.ain)) {
                        seenAins.insert(dev.ain);
                        result.append(dev);
                    }
                }
                break;
            }
        }
    }

    // Allow this group to be re-entered from sibling paths (only block true
    // cycles within the current expansion chain); remove from visited on exit.
    visited.remove(lg.id);
    return result;
}

/// Accumulate a single member's energy contribution into @p target.
/// @p s must already be opened to the "devices" QSettings group.
static void accumulateMemberEnergy(FritzDevice &target,
                                   const FritzDevice &member,
                                   QSettings &s)
{
    if (!member.hasEnergyMeter() || !member.energyStats.valid)
        return;
    // Read producer flag from QSettings — model's isProducer is a runtime-only
    // field reset on every rebuild, so it cannot be trusted here.
    // QSettings is the authoritative source.
    const bool producer = s.value(
        member.ain + QLatin1Char('/') + QString::fromLatin1(kSettingsKeyIsProducer),
        false).toBool();
    target.energyStats.valid   = true;
    target.energyStats.power  += producer ? -member.energyStats.power : member.energyStats.power;
    target.energyStats.energy += member.energyStats.energy;
}

/// Build a synthetic FritzDevice that represents a local group.
/// @p s must already be opened to the "devices" QSettings group.
static FritzDevice synthesizeLocalGroupDevice(
    const LocalGroup &lg,
    const FritzDeviceList &allFritz,
    const LocalGroupList &allLocal,
    QSettings &s)
{
    FritzDevice gdev;
    gdev.ain        = QStringLiteral("local:") + lg.id;
    gdev.identifier = gdev.ain;
    gdev.id         = lg.id;
    gdev.unitUID    = gdev.ain;
    gdev.name       = lg.name;
    gdev.group      = true;
    gdev.localGroup = true;
    gdev.present    = true;
    gdev.memberAins = lg.memberAins;

    // Capability union of all members
    QSet<QString> visited, seenAins;
    const FritzDeviceList members = collectLocalGroupMembers(lg, allFritz, allLocal, visited, seenAins);
    int onlineCount  = 0;
    int offlineCount = 0;
    for (const FritzDevice &m : members) {
        m.present ? ++onlineCount : ++offlineCount;
        gdev.functionBitmask |= m.functionBitmask;
        if (m.hasSwitch() && m.switchStats.valid) {
            gdev.switchStats.valid = true;
            if (m.switchStats.on)
                gdev.switchStats.on = true;
        }
        accumulateMemberEnergy(gdev, m, s);
    }
    // Derive presence from member counts:
    //   all online  → present=true,  partiallyPresent=false
    //   mixed       → present=true,  partiallyPresent=true   ("Partial")
    //   all offline → present=false, partiallyPresent=false
    if (onlineCount == 0) {
        gdev.present          = false;
        gdev.partiallyPresent = false;
    } else if (offlineCount == 0) {
        gdev.present          = true;
        gdev.partiallyPresent = false;
    } else {
        gdev.present          = true;
        gdev.partiallyPresent = true;
    }
    return gdev;
}

void DeviceModel::setLocalGroups(const LocalGroupList &localGroups,
                                  const FritzDeviceList &allFritzDevices)
{
    beginResetModel();

    // Remove any existing "Local Groups" bucket and rebuild from scratch
    m_localGroups = localGroups;
    rebuildGroups(m_lastFritzDevices);

    if (!localGroups.isEmpty()) {
        // Open QSettings once for all synthesize calls — avoids repeated
        // open/close for every group when iterating the bucket.
        QSettings s;
        s.beginGroup(QStringLiteral("devices"));
        Group localBucket;
        localBucket.label    = i18n("Local Groups");
        localBucket.iconName = QStringLiteral(":/icons/device-local-group.svg");
        for (const LocalGroup &lg : localGroups) {
            localBucket.devices.append(
                synthesizeLocalGroupDevice(lg, allFritzDevices, localGroups, s));
        }
        s.endGroup();
        std::sort(localBucket.devices.begin(), localBucket.devices.end(),
                  [](const FritzDevice &a, const FritzDevice &b) {
                      return a.name.toLower() < b.name.toLower();
                  });
        m_groups.append(localBucket);
    }

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

double DeviceModel::signedPowerForDisplay(const FritzDevice &dev) const
{
    if (!dev.hasEnergyMeter() || !dev.energyStats.valid)
        return 0.0;

    // Local groups synthesize their power as a pre-signed sum in
    // accumulateMemberEnergy(), so the cached value is already correct.
    if (dev.localGroup)
        return dev.energyStats.power;

    // Native Fritz!Box (hardware) group: the API-supplied power sums all
    // member powers without producer sign convention.  Recompute by walking
    // the immediate members (recursing into nested hardware groups) and
    // applying the QSettings producer flag.  Cycle guard via QSet.
    if (dev.isGroup()) {
        QSettings s;
        s.beginGroup(QStringLiteral("devices"));

        QSet<QString> visited;
        std::function<double(const FritzDevice &)> sumSigned =
            [&](const FritzDevice &g) -> double {
                if (visited.contains(g.ain))
                    return 0.0;
                visited.insert(g.ain);
                double total = 0.0;
                for (const QString &memberAin : g.memberAins) {
                    // Local-group prefix shouldn't appear in a hardware group's
                    // memberAins, but guard for robustness.
                    FritzDevice m = deviceByAin(memberAin);
                    if (m.ain.isEmpty())
                        m = deviceById(memberAin);
                    if (m.ain.isEmpty())
                        continue;
                    if (m.isGroup() && !m.localGroup) {
                        total += sumSigned(m);
                        continue;
                    }
                    if (!m.hasEnergyMeter() || !m.energyStats.valid)
                        continue;
                    const bool producer = s.value(
                        m.ain + QLatin1Char('/')
                            + QString::fromLatin1(kSettingsKeyIsProducer),
                        false).toBool();
                    total += producer ? -m.energyStats.power
                                      :  m.energyStats.power;
                }
                return total;
            };
        return sumSigned(dev);
    }

    // Plain device leaf — runtime isProducer flag is authoritative.
    return dev.isProducer ? -dev.energyStats.power : dev.energyStats.power;
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
                const double power = signedPowerForDisplay(dev);
                return QString("%1 W").arg(power, 0, 'f', 1);
            }
            return QString("-");
        case ColPresent:
            if (dev.partiallyPresent) return i18n("Partial");
            return dev.present ? i18n("Online") : i18n("Offline");
        }
    }

    if (role == Qt::ForegroundRole) {
        if (index.column() == ColPresent) {
            if (dev.partiallyPresent) return QColor(Qt::darkYellow);
            return dev.present ? QColor(Qt::darkGreen) : QColor(Qt::red);
        }
        if (index.column() == ColStatus && dev.hasAlarm() && dev.alarmStats.triggered)
            return QColor(Qt::red);
    }

    if (role == Qt::DecorationRole && index.column() == ColName)
        return iconWithBatteryOverlay(dev);

    if (role == Qt::ToolTipRole) {
        QString tip = QString("<b>%1</b><br/>").arg(dev.name);
        tip += QString("AIN: %1<br/>").arg(dev.identifier);
        tip += i18n("Product: %1 (%2)<br/>", dev.productname, dev.manufacturer);
        tip += i18n("Firmware: %1<br/>", dev.fwversion);
        if (dev.hasSwitch())
            tip += i18n("Switch: %1<br/>", dev.switchStats.on ? i18n("On") : i18n("Off"));
        if (dev.hasEnergyMeter() && dev.energyStats.valid) {
            const double sign = dev.isProducer ? -1.0 : 1.0;
            // Power: use the signed-aggregation helper so hardware groups
            // honour each member's producer flag.  Energy still uses the raw
            // sign-from-isProducer value (an aggregate-energy helper is a
            // separate concern; not part of this fix).
            tip += i18n("Power: %1 W<br/>", QString::number(signedPowerForDisplay(dev), 'f', 1));
            tip += i18n("Energy: %1 Wh<br/>", QString::number(sign * dev.energyStats.energy, 'f', 0));
            tip += i18n("Voltage: %1 V<br/>", QString::number(dev.energyStats.voltage, 'f', 1));
        }
        if (dev.hasThermostat()) {
            auto t2c = [](int raw) -> QString {
                if (raw == 253) return i18n("Off");
                if (raw == 254) return i18n("Comfort");
                if (raw >= 16 && raw <= 56)
                    return QString("%1 °C").arg(8.0 + (raw - 16) * 0.5, 0, 'f', 1);
                return QString::number(raw);
            };
            tip += i18n("Target: %1<br/>", t2c(dev.thermostatStats.targetTemp));
            // Only add battery line if not already covered by generic battery status below
            if (!dev.hasBattery() || dev.batteryStats.level < 0)
                tip += i18n("Battery: %1%<br/>", dev.thermostatStats.battery);
        }
        if (dev.hasBattery() && dev.batteryStats.level >= 0) {
            // Generate status text based on battery level (uses 5-state normalization)
            QString status = batteryStatusTextForLevel(dev.batteryStats.level, dev.batteryStats.low);
            tip += i18n("Battery: %1% — %2<br/>",
                        QString::number(dev.batteryStats.level),
                        status);
        }
        if (dev.hasHumidity() && dev.humidityStats.valid)
            tip += i18n("Humidity: %1%<br/>", dev.humidityStats.humidity);
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

void DeviceModel::updateDeviceNativeNetPowerStatus(const QString &ain, bool nativeNetPower)
{
    for (int gi = 0; gi < m_groups.size(); ++gi) {
        for (int di = 0; di < m_groups[gi].devices.size(); ++di) {
            if (m_groups[gi].devices[di].ain == ain) {
                m_groups[gi].devices[di].nativeNetPower = nativeNetPower;
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
    if (dev.partiallyPresent) return i18n("Partial");
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
