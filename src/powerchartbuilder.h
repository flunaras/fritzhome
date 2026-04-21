#pragma once

/// \file powerchartbuilder.h
/// \brief Builds and manages Power and Humidity chart tabs for ChartWidget.
///
/// Owns all power/humidity-related axis, series, and label pointers.
/// Delegates to ChartWidget for tab insertion, lock-checkbox creation,
/// and settings persistence.

#include <QList>
#include <QPointer>
#include <QLabel>
#include <QCheckBox>

#include "chartutils.h"
#include "fritzdevice.h"

class ChartWidget;

/// Builds and manages the Power and Humidity chart tabs.
/// Non-QObject value type; owned by ChartWidget as a plain member.
class PowerChartBuilder
{
public:
    explicit PowerChartBuilder(ChartWidget &owner);

    /// Build the power chart tab (single-device area or stacked group).
    void buildPowerChart(const FritzDevice &dev,
                         const FritzDeviceList &memberDevices);

    /// Build the humidity chart tab.
    void buildHumidityChart(const FritzDevice &dev);

    /// Update series data in-place from fresh device data (rolling poll).
    void updateRolling(const FritzDevice &device,
                       const FritzDeviceList &memberDevices);

    /// Re-scale Y axis to data visible in [minMs, maxMs].
    void rescaleYPower(qint64 minMs, qint64 maxMs);

    /// Reset all pointers (called on device switch / teardown).
    void reset();

    /// Null out widget pointers owned by a tab widget being deleted.
    void nullifyWidgetPointers(QWidget *w);

    /// Reset lock state (called on device change).
    void resetLock();

    // -- Accessors for ChartWidget orchestration --------------------------
    bool hasPowerAxisX() const { return m_powerAxisX != nullptr; }

    // Save/load lock state via QSettings.
    void saveState() const;
    void loadState();

private:
    ChartWidget &m_owner;

    // Power chart (single-device or stacked-group share the axes)
    QDateTimeAxis *m_powerAxisX       = nullptr;
    QValueAxis    *m_powerAxisY       = nullptr;
    QXYSeries     *m_powerSeries      = nullptr;  ///< upper series (single-device mode)
    QXYSeries     *m_powerLowerSeries = nullptr;  ///< lower zero baseline (single-device mode)
    QPointer<QLabel> m_powerValueLabel = nullptr;

    // Stacked power chart (group mode)
    QList<QXYSeries *> m_powerStackedUpper;
    QList<QXYSeries *> m_powerStackedLower;
    QXYSeries         *m_powerNetSeries = nullptr;  ///< orange net/effective line (group mode only)

    // Lock checkbox
    QPointer<QCheckBox> m_powerLockCheckBox = nullptr;
    bool   m_powerScaleLocked = false;
    double m_lockedPowerMin   = 0.0;
    double m_lockedPowerMax   = 10.0;

    // Humidity chart
    QXYSeries *m_humiditySeries = nullptr;

    friend class ChartWidget;
};
