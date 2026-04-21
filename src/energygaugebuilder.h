#pragma once

/// \file energygaugebuilder.h
/// \brief Builds and manages the Energy gauge tab and group pie chart
///        for ChartWidget.
///
/// Owns gauge labels (kWh, W, V), group energy pie chart, and the
/// PieTooltipFilter.  Delegates to ChartWidget for tab insertion.

#include <QPointer>
#include <QLabel>

#include "chartutils.h"
#include "fritzdevice.h"

// Additional forward declares for types not in chartutils.h
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QPieSeries;
#else
namespace QtCharts {
class QPieSeries;
}
using QtCharts::QPieSeries;
#endif

class ChartWidget;
class PieTooltipFilter;

/// Builds and manages the Energy gauge tab (single-device and group pie).
/// Non-QObject value type; owned by ChartWidget as a plain member.
class EnergyGaugeBuilder
{
public:
    explicit EnergyGaugeBuilder(ChartWidget &owner);

    /// Build the energy gauge tab (total kWh, current W, optional V, optional pie).
    void buildEnergyGauge(const FritzDevice &dev,
                          const FritzDeviceList &memberDevices = FritzDeviceList());

    /// Update gauge labels in-place from fresh device data (rolling poll).
    /// For group devices, \a memberDevices is used to compute net signed values.
    void updateRolling(const FritzDevice &device,
                       const FritzDeviceList &memberDevices = FritzDeviceList());

    /// Update pie labels (after energy stats arrive for group members).
    void updateGroupPieLabels();

    /// Reset all pointers (called on device switch / teardown).
    void reset();

    /// Null out widget pointers owned by a tab widget being deleted.
    void nullifyWidgetPointers(QWidget *w);

    /// Remove the pie tooltip event filter and clear pie-related pointers.
    /// Called by EnergyHistoryBuilder and ChartWidget::teardownTabs() before
    /// destroying the tab that owns the pie view.
    void cleanupPieFilter();

    /// Returns true if this tab widget \a w is the pie chart ancestor.
    bool isPieOwner(QWidget *w) const;

    // Save/load (no-op for gauge — no persistent state).
    void saveState() const {}
    void loadState() {}

private:
    ChartWidget &m_owner;

    // Gauge labels
    QPointer<QLabel> m_gaugeKwhLabel     = nullptr;
    QPointer<QLabel> m_gaugePowerLabel   = nullptr;
    QPointer<QLabel> m_gaugeVoltageLabel = nullptr;

    // Optional pie chart showing group member energy distribution
    QPieSeries       *m_groupEnergyPie        = nullptr;
    QChartView       *m_groupEnergyPieView    = nullptr;
    PieTooltipFilter *m_groupPieTooltipFilter = nullptr;

    friend class ChartWidget;
};
