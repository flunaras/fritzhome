#pragma once

/// \file energyhistorybuilder.h
/// \brief Builds and manages the Energy History chart tab for ChartWidget.
///
/// Owns all energy-history-related state: resolution combo, chart view,
/// cached stats, error tracking, and the bar-chart tooltip text.
/// Handles three resolution views (15-min / daily / monthly) for both
/// single-device and stacked-group modes.  Delegates to ChartWidget for
/// tab insertion, event filter installation, and settings persistence.

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>

#include "chartutils.h"
#include "fritzdevice.h"

// Additional forward declares for types not in chartutils.h
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QAbstractSeries;
#else
namespace QtCharts {
class QAbstractSeries;
}
using QtCharts::QAbstractSeries;
#endif

QT_FORWARD_DECLARE_CLASS(QComboBox)

class ChartWidget;

/// Shared result struct from buildEnergyCategories().
struct EnergyCategories {
    QStringList categories;
    QStringList tooltipLabels;
    QVector<int> barIndices;     ///< newest-first index for each bar (left-to-right)
    QVector<int> sepCatIndices;
    QStringList sepHourLabels;
    QStringList monthBarLabels;
    QStringList yearBarLabels;
    int barsToShow = 0;
};

/// Per-member entry for the stacked energy history chart.
/// Carries the display name, statistics, and producer flag for each group member.
struct MemberHistoryEntry {
    QString          name;
    DeviceBasicStats stats;
    bool             isProducer = false;
};

/// Builds and manages the Energy History chart tab (single-device and group).
/// Non-QObject value type; owned by ChartWidget as a plain member.
class EnergyHistoryBuilder
{
public:
    explicit EnergyHistoryBuilder(ChartWidget &owner);

    /// Build the single-device energy history bar chart.
    void buildEnergyHistoryChart(const DeviceBasicStats &stats);

    /// Build the stacked energy history bar chart (group mode).
    void buildEnergyHistoryChartStacked(
        const QList<MemberHistoryEntry> &memberStats);

    /// Build a placeholder tab when no data is available for the selected view.
    void buildEnergyHistoryPlaceholder(const QStringList &viewLabels,
                                       int selectedIdx);

    /// Handle resolution combo change (tear down & rebuild the energy tab).
    void onEnergyResolutionChanged(int index);

    /// Show or update an error display in the Energy History tab.
    void showEnergyHistoryError(const QString &caption, const QStringList &errors);

    /// Reset all pointers (called on device switch / teardown).
    void reset();

    /// Null out widget pointers owned by a tab widget being deleted.
    void nullifyWidgetPointers(QWidget *w);

    // -- Accessors for ChartWidget orchestration --------------------------
    int activeEnergyGrid()      const { return m_activeEnergyGrid; }
    int energyHistoryTabIndex() const { return m_energyHistoryTabIndex; }
    const QString &energyBarTooltip() const { return m_energyBarTooltip; }

    /// Access the cached single-device stats.
    DeviceBasicStats       &lastEnergyStats()       { return m_lastEnergyStats; }
    const DeviceBasicStats &lastEnergyStats() const { return m_lastEnergyStats; }

    /// Access the cached group member stats.
    QList<MemberHistoryEntry>       &lastGroupMemberStats()       { return m_lastGroupMemberStats; }
    const QList<MemberHistoryEntry> &lastGroupMemberStats() const { return m_lastGroupMemberStats; }

    /// Access error tracking state.
    int  &lastAvailableGrids()    { return m_lastAvailableGrids; }
    bool &groupHistoryMode()      { return m_groupHistoryMode; }
    QStringList &groupEnergyErrors() { return m_groupEnergyErrors; }
    QString     &energyError()       { return m_energyError; }

    int  &energyResSelectedIdx()  { return m_energyResSelectedIdx; }

    // Save/load resolution selection state via QSettings.
    void saveState() const;
    void loadState();

private:
    ChartWidget &m_owner;

    /// Build category labels, tooltip labels, and bar indices for a given
    /// energy history grid/resolution.
    EnergyCategories buildEnergyCategories(int grid, int maxBars,
                                           qint64 fetchSecs,
                                           const QDateTime &fetchTime) const;

    /// Create and attach X/Y axes for an energy history bar chart.
    QValueAxis *setupEnergyHistoryAxes(
        QChart *chart,
        QAbstractSeries *barSeries,
        const EnergyCategories &cats,
        int grid, bool useKwh, double minY, double maxY);

    /// Build the container, install event filters, set member state, insert tab.
    void finalizeEnergyHistoryTab(QChartView *chartView,
                                  const QStringList &viewLabels,
                                  int selectedIdx,
                                  const QString &totalText,
                                  int grid);

    // -- Energy history chart state ----------------------------------------
    int         m_energyHistoryTabIndex  = -1;
    int         m_energyResSelectedIdx   = 0;  ///< persists user resolution choice
    int         m_activeEnergyGrid       = 0;  ///< grid of currently displayed view (0 = none)
    QComboBox  *m_energyResCombo         = nullptr;
    QChartView *m_energyChartView        = nullptr;
    DeviceBasicStats m_lastEnergyStats;        ///< latest stats (single-device mode)
    int         m_lastAvailableGrids     = 0;  ///< bitmask of available grids

    // Group energy history mode
    bool        m_groupHistoryMode       = false;
    QList<MemberHistoryEntry> m_lastGroupMemberStats;
    QStringList m_groupEnergyErrors;

    // Energy history error tracking
    QString     m_energyError;

    // Tooltip text for hover on energy bar chart
    QString     m_energyBarTooltip;

    friend class ChartWidget;
};
