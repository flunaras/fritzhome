#pragma once

#include <QString>
#include <QList>
#include <QStringList>
#include <QDateTime>

// Capability bitmask bit positions (originally from AHA spec; now synthesized
// from REST API interface names in FritzApi::parseDeviceListJson)
namespace FunctionMask {
    constexpr int ALARM           = (1 << 4);   // 16
    constexpr int BLIND           = (1 << 6);   // 64 (jalousie/blind)
    constexpr int BUTTON          = (1 << 7);   // 128
    constexpr int THERMOSTAT      = (1 << 6);   // Radiator controller (HKR)
    constexpr int ENERGY_METER    = (1 << 7);   // Energy metering
    constexpr int TEMPERATURE     = (1 << 8);   // 256 temperature sensor
    constexpr int SWITCH          = (1 << 9);   // 512 switchable outlet
    constexpr int DECT_REPEATER   = (1 << 10);  // 1024
    constexpr int MICROPHONE      = (1 << 11);  // 2048
    constexpr int HKR             = (1 << 6);   // HKR (heating controller) – function bit
    constexpr int DIMMER          = (1 << 13);  // 8192
    constexpr int COLOR_BULB      = (1 << 14);  // 16384 (color/white bulb)
    constexpr int BLIND2          = (1 << 18);  // 262144 (Rollladen)
    constexpr int HUMIDITY        = (1 << 20);  // humidity sensor
}

struct EnergyStats {
    double power = 0.0;         // current power in W
    double energy = 0.0;        // total energy in Wh
    double voltage = 0.0;       // voltage in V
    bool valid = false;
};

/**
 * One time-series from the REST API per-unit statistics endpoint.
 * grid: seconds between samples
 * values: sample values (already scaled to physical units), newest first
 */
struct StatSeries {
    int grid = 0;              ///< seconds per sample
    QList<double> values;      ///< newest-first
    QString unit;
};

/**
 * All stats returned by the REST API per-unit statistics endpoint for a single device.
 * Each category (energy, power, temperature, voltage) may have multiple
 * StatSeries entries with different grid resolutions.
 */
struct DeviceBasicStats {
    QList<StatSeries> energy;      ///< Wh
    QList<StatSeries> power;       ///< W (raw unit "0.01W" → divide by 100)
    QList<StatSeries> temperature; ///< °C (raw unit "0.1°C" → divide by 10)
    QList<StatSeries> voltage;     ///< V  (raw unit "0.001V" → divide by 1000)
    QDateTime fetchTime;           ///< when the request was made (used to reconstruct timestamps)
    bool valid = false;

    /// Non-empty when the Fritz!Box returned a statistics object but energy
    /// data was unavailable.  Contains the Fritz!Box-reported
    /// ``statisticsState`` value (e.g. ``"unknown"``, ``"notConnected"``)
    /// so the UI can show a meaningful reason instead of a generic message.
    QString energyStatsState;
};

struct ThermostatStats {
    int targetTemp = -1;   // in 0.5°C steps (mapped: 16=8°C, 56=28°C, 253=off, 254=on)
    int currentTemp = -1;  // in 0.1°C steps, offset by 0
    int comfortTemp = -1;
    int ecotTemp = -1;
    bool windowOpen = false;
    bool summerActive = false;
    bool holidayActive = false;
    bool batteryLow = false;
    int battery = -1;       // battery percent
};

struct SwitchStats {
    bool on = false;
    bool locked = false;         // locked via Fritz!Box UI
    bool deviceLocked = false;   // locked on device itself
    bool mixedSwitchState = false; // group: controllable members have different on/off states → label shows PARTIAL
    bool hasLockedMembers = false; // group: at least one member is locked and at least one is controllable
                                   // → group toggle would produce uncontrollable mixed result → use InstantPopup
    bool allOn  = false;           // group: all controllable members are already on  → disable Turn On button
    bool allOff = false;           // group: all controllable members are already off → disable Turn Off button
    bool valid = false;
};

struct DimmerStats {
    int level = 0;         // 0-255
    int levelPercent = 0;  // 0-100
    bool on = false;
    bool valid = false;
};

struct ColorStats {
    int hue = 0;            // 0-359
    int saturation = 0;     // 0-255
    int unmappedHue = 0;
    int unmappedSaturation = 0;
    int colorTemperature = 0; // in Kelvin
    QString colorMode;      // "1"=hue/sat, "4"=color temp
    bool valid = false;
};

struct BlindStats {
    QString mode;   // "open"/"close"/"stop"
    int endPosition = 0; // 0=open, 100=closed (or reverse depending on config)
    bool valid = false;
};

struct HumidityStats {
    int humidity = -1; // relative humidity %
    bool valid = false;
};

struct AlarmStats {
    bool triggered = false;
    QDateTime lastAlert;
    bool valid = false;
};

struct FritzDevice {
    // Common
    QString ain;             // actor identification number (identifier attribute, spaces stripped)
    QString identifier;      // formatted AIN with spaces (original identifier attribute)
    QString id;              // internal numeric device ID (id attribute, e.g. "24")
    QString unitUID;         // REST API unit UID (e.g. "11630 0015376-1"), used for REST calls
    int functionBitmask = 0; // capabilities bitmask
    bool group = false;      // true if parsed from the groups[] JSON array
    QStringList memberAins;  // for groups: AINs of member devices (resolved from memberUnitUids)
    QString fwversion;
    QString manufacturer;
    QString productname;
    QString name;            // device name
    bool present = false;    // currently reachable

    // Temperature (if TEMPERATURE bit set)
    double temperature = -273.0;  // in °C
    int tempOffset = 0;

    // Sub-device capabilities
    SwitchStats switchStats;
    EnergyStats energyStats;
    ThermostatStats thermostatStats;
    DimmerStats dimmerStats;
    ColorStats colorStats;
    BlindStats blindStats;
    HumidityStats humidityStats;
    AlarmStats alarmStats;

    // Historical data for charts (accumulated from polling)
    QList<QPair<QDateTime, double>> temperatureHistory;
    QList<QPair<QDateTime, double>> powerHistory;
    QList<QPair<QDateTime, double>> humidityHistory;

    // Detailed historical stats from getbasicdevicestats
    DeviceBasicStats basicStats;

    // Helper methods
    bool hasSwitch()       const { return (functionBitmask & (1 << 9)) != 0; }
    bool hasThermostat()   const { return (functionBitmask & (1 << 6)) != 0; }
    bool hasEnergyMeter()  const { return (functionBitmask & (1 << 7)) != 0; }
    bool hasTemperature()  const { return (functionBitmask & (1 << 8)) != 0; }
    bool hasDimmer()       const { return (functionBitmask & (1 << 13)) != 0; }
    bool hasColorBulb()    const { return (functionBitmask & (1 << 14)) != 0; }
    bool hasBlind()        const {
        return (functionBitmask & (1 << 18)) != 0 ||
               (functionBitmask & (1 << 6)) != 0;
    }
    bool hasHumidity()     const { return (functionBitmask & (1 << 20)) != 0; }
    bool hasAlarm()        const { return (functionBitmask & (1 << 4)) != 0; }
    bool isGroup()         const { return group; }
};

using FritzDeviceList = QList<FritzDevice>;
