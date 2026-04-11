#pragma once

/// \file temperaturechartbuilder.h
/// \brief Builds and manages Temperature chart tabs for ChartWidget.
///
/// Owns all temperature-related axis, series, and label pointers.
/// Delegates to ChartWidget for tab insertion, lock-checkbox creation,
/// and settings persistence.

#include <QList>
#include <QPointer>
#include <QLabel>
#include <QCheckBox>

#include "chartutils.h"
#include "fritzdevice.h"

class ChartWidget;

/// Builds and manages the Temperature chart tab (single-device and group).
/// Non-QObject value type; owned by ChartWidget as a plain member.
class TemperatureChartBuilder
{
public:
    explicit TemperatureChartBuilder(ChartWidget &owner);

    /// Build the single-device temperature chart tab.
    void buildTemperatureChart(const FritzDevice &dev);

    /// Build the group temperature chart tab (one line per temp-capable member).
    void buildGroupTemperatureChart(const FritzDeviceList &memberDevices);

    /// Update series data in-place from fresh device data (rolling poll).
    void updateRolling(const FritzDevice &device,
                       const FritzDeviceList &memberDevices);

    /// Re-scale Y axis to data visible in [minMs, maxMs].
    void rescaleYTemp(qint64 minMs, qint64 maxMs);
    void rescaleYGroupTemp(qint64 minMs, qint64 maxMs);

    /// Reset all pointers (called on device switch / teardown).
    void reset();

    /// Null out widget pointers owned by a tab widget being deleted.
    void nullifyWidgetPointers(QWidget *w);

    /// Reset lock state (called on device change).
    void resetLock();

    // -- Accessors for ChartWidget orchestration --------------------------
    bool hasTempAxisX()      const { return m_tempAxisX != nullptr; }
    bool hasGroupTempAxisX() const { return m_groupTempAxisX != nullptr; }

    // Save/load lock state via QSettings.
    void saveState() const;
    void loadState();

private:
    ChartWidget &m_owner;

    // Temperature chart (single device)
    QDateTimeAxis *m_tempAxisX  = nullptr;
    QValueAxis    *m_tempAxisY  = nullptr;
    QXYSeries     *m_tempSeries = nullptr;
    QPointer<QLabel> m_tempValueLabel = nullptr;

    // Group temperature chart
    QDateTimeAxis          *m_groupTempAxisX = nullptr;
    QValueAxis             *m_groupTempAxisY = nullptr;
    QList<QXYSeries *>      m_groupTempSeries;

    // Lock checkbox (shared between single and group — only one is built)
    QPointer<QCheckBox> m_tempLockCheckBox = nullptr;
    bool   m_tempScaleLocked = false;
    double m_lockedTempMin   = 0.0;
    double m_lockedTempMax   = 30.0;

    // Allow ChartWidget to access applyTimeWindow axes
    friend class ChartWidget;
};
