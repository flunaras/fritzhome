#pragma once

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QList>
#include <QPair>
#include <QString>
#include <QPointer>

#include <QtCharts/QChartView>
#include "fritzdevice.h"


// Forward-declare Qt Charts types to avoid pulling in the whole namespace here
QT_FORWARD_DECLARE_CLASS(QTabWidget)
QT_FORWARD_DECLARE_CLASS(QScrollBar)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QSettings)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QGraphicsTextItem)

// Qt5: charts live in QtCharts namespace; Qt6: global namespace (no wrapper needed)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QDateTimeAxis;
class QValueAxis;
class QChartView;
class QLineSeries;
class QPieSeries;
class QXYSeries;
#else
namespace QtCharts {
class QDateTimeAxis;
class QValueAxis;
class QChartView;
class QLineSeries;
class QPieSeries;
class QXYSeries;
}
#endif

// Forward-declare the PieTooltipFilter used by the chart implementation so the
// header can store a typed QPointer without pulling the filter's full
// definition into the public header.
class PieTooltipFilter;

/**
 * ChartWidget renders historical time-series charts for:
 * - Temperature over time  (with optional thermostat target line)
 * - Power consumption over time
 * - Humidity over time
 * - Energy history from getbasicdevicestats (bar chart per resolution bucket)
 * using Qt Charts (QtCharts module).
 *
 * A shared time-window combo box overlaid in the top-left of the Temperature
 * and Power chart tabs lets the user zoom into the last 5 min / 15 min / 30 min
 * / 1 h / 2 h / … of data without triggering a full chart rebuild (for the
 * rolling-poll charts).
 *
 * The Temperature and Power tabs each contain a "Lock Y scale" checkbox
 * overlaid inside the chart area at the bottom-left corner.  When checked,
 * the vertical axis range is frozen at the value it had when the box was
 * ticked; auto-rescaling is suppressed until the box is unchecked, at which
 * point the axis immediately re-fits to the currently visible data.  Switching
 * to a different device always resets both locks.  Each lock state (and its
 * captured range) is persisted across app restarts via QSettings.
 */
class ChartWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ChartWidget(QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device,
                      const FritzDeviceList &memberDevices = FritzDeviceList());

    /// Called on every poll tick for the same device — updates series data and
    /// overlay labels in-place without rebuilding any tabs.
    void updateRollingCharts(const FritzDevice &device,
                             const FritzDeviceList &memberDevices = FritzDeviceList());

    /// Called by MainWindow when getbasicdevicestats reply arrives for the
    /// currently selected device. Rebuilds the Energy History tab in-place.
    void updateEnergyStats(const DeviceBasicStats &stats);

    /// Called by MainWindow when per-member getbasicdevicestats replies have all
    /// arrived for the currently selected group device.  Rebuilds the Energy
    /// History tab with a stacked bar chart (one colour per member).
    void updateGroupEnergyStats(const QList<QPair<QString, DeviceBasicStats>> &memberStats);

    /// Called by MainWindow when an error occurs fetching energy stats for the
    /// currently selected device. Updates the placeholder with error message(s).
    void updateEnergyStatsError(const QString &error);

    /// Called by MainWindow when an error occurs fetching energy stats for a
    /// group member. Updates the placeholder with accumulated error message(s).
    void updateGroupEnergyStatsError(const QString &memberName, const QString &error);

    /// Returns the grid interval (in seconds) of the currently displayed energy
    /// history view: 900 (15-min / 24 h), 86400 (daily), 2678400 (monthly), or
    /// 0 if no energy history tab has been built yet.
    int activeEnergyGrid() const { return m_activeEnergyGrid; }

private slots:
    void onWindowComboChanged(int index);
    void onScrollBarChanged(int value);
    void onEnergyResolutionChanged(int index);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // -- builders (called once per updateDevice) ------------------------------
    void buildTemperatureChart(const FritzDevice &dev);
    void buildGroupTemperatureChart(const FritzDeviceList &memberDevices);
    void buildPowerChart(const FritzDevice &dev,
                         const FritzDeviceList &memberDevices);
    void buildHumidityChart(const FritzDevice &dev);
    void buildEnergyGauge(const FritzDevice &dev,
                          const FritzDeviceList &memberDevices = FritzDeviceList());
    void buildEnergyHistoryChart(const DeviceBasicStats &stats);
    void buildEnergyHistoryChartStacked(const QList<QPair<QString, DeviceBasicStats>> &memberStats);
    void buildEnergyHistoryPlaceholder(const QStringList &viewLabels, int selectedIdx);

    // -- time-window helpers (for rolling-poll charts) -----------------------
    /// Returns the selected window duration in milliseconds.
    qint64 windowMs() const;
    /// Re-applies the current slider window to all live chart axes.
    void applyTimeWindow();
    /// Update the horizontal scroll bar range/page to match the full data span.
    void updateScrollBar();
    /// Auto-scale the Y axis to the data visible in [minMs, maxMs].
    void rescaleYTemp(qint64 minMs, qint64 maxMs);
    void rescaleYGroupTemp(qint64 minMs, qint64 maxMs);
    void rescaleYPower(qint64 minMs, qint64 maxMs);

    // -- slider visibility ----------------------------------------------------
    /// Show/hide the time-window combo based on the currently active tab.
    /// (No-op: combo is now embedded as an overlay; kept for compatibility.)
    void updateSliderVisibility();

    // -- settings persistence -------------------------------------------------
    void saveChartState() const;
    void loadChartState();

    // -- widgets --------------------------------------------------------------
    QTabWidget  *m_tabs            = nullptr;
    int m_windowComboIndex = 2;             ///< selected time-window index (persisted); per-tab combos are created on build
    QScrollBar  *m_scrollBar       = nullptr;  ///< horizontal scroll through history (Temperature tab)
    QScrollBar  *m_powerScrollBar  = nullptr;  ///< horizontal scroll through history (Power tab)
    QPointer<QCheckBox>   m_powerLockCheckBox = nullptr; ///< "Lock Y scale" checkbox overlaid inside the Power chart area (bottom-left)
    QPointer<QCheckBox>   m_tempLockCheckBox  = nullptr; ///< "Lock Y scale" checkbox overlaid inside the Temperature chart area (bottom-left)

    // -- scroll state ---------------------------------------------------------
    /// When true the view follows the live end of the data (scroll bar pinned to max).
    bool m_scrollAtEnd = true;

    // -- temperature Y-axis lock ----------------------------------------------
    /// When true, rescaleYTemp() and rescaleYGroupTemp() are no-ops.
    bool   m_tempScaleLocked = false;
    double m_lockedTempMin   = 0.0;   ///< axis minimum captured at lock time
    double m_lockedTempMax   = 30.0;  ///< axis maximum captured at lock time

    // -- power Y-axis lock ----------------------------------------------------
    /// When true, rescaleYPower() is a no-op; the axis stays at the range that
    /// was current when the checkbox was checked.
    bool   m_powerScaleLocked = false;
    double m_lockedPowerMin   = 0.0;  ///< axis minimum captured at lock time
    double m_lockedPowerMax   = 10.0; ///< axis maximum captured at lock time

    // -- cached chart state (rebuilt each updateDevice call) ------------------
    FritzDevice     m_device;          ///< last device passed to updateDevice()
    FritzDeviceList m_memberDevices;   ///< for groups: member devices (empty for non-groups)

// In Qt6, chart types are in the global namespace; in Qt5 they are in QtCharts::
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#  define QTCHARTS_NS
#else
#  define QTCHARTS_NS QtCharts::
#endif

    // Temperature chart
    QTCHARTS_NS QDateTimeAxis *m_tempAxisX  = nullptr;
    QTCHARTS_NS QValueAxis    *m_tempAxisY  = nullptr;
    QTCHARTS_NS QXYSeries     *m_tempSeries = nullptr;  ///< main temperature line
    QPointer<QLabel>           m_tempValueLabel = nullptr;

    // Group temperature chart — one line series per temperature-capable member.
    // Axes are shared; series are owned by the QChart.
    QTCHARTS_NS QDateTimeAxis          *m_groupTempAxisX = nullptr;
    QTCHARTS_NS QValueAxis             *m_groupTempAxisY = nullptr;
    QList<QTCHARTS_NS QXYSeries *>      m_groupTempSeries;   ///< one per temp-capable member

    // Power chart
    QTCHARTS_NS QDateTimeAxis *m_powerAxisX      = nullptr;
    QTCHARTS_NS QValueAxis    *m_powerAxisY      = nullptr;
    QTCHARTS_NS QXYSeries     *m_powerSeries     = nullptr; ///< upper (spline) series of the area (single-device mode)
    QTCHARTS_NS QXYSeries     *m_powerLowerSeries = nullptr; ///< lower (zero baseline) series of the area (single-device mode)
    QPointer<QLabel>           m_powerValueLabel = nullptr;

    // Stacked power chart (group mode) — parallel upper/lower series per member device.
    // m_powerStackedUpper[i] is the upper boundary of layer i (= cumulative sum up to member i).
    // m_powerStackedLower[i] is the lower boundary of layer i (= upper boundary of layer i-1, or zero for i==0).
    // These are stored as raw pointers; ownership is with the QChart.
    QList<QTCHARTS_NS QXYSeries *> m_powerStackedUpper;  ///< one per member device
    QList<QTCHARTS_NS QXYSeries *> m_powerStackedLower;  ///< one per member device (shared with upper[i-1] conceptually)

    // Humidity chart
    QTCHARTS_NS QXYSeries     *m_humiditySeries = nullptr;

    // Energy gauge tab
    QPointer<QLabel> m_gaugeKwhLabel    = nullptr;  ///< "X.XXX kWh" value label
    QPointer<QLabel> m_gaugePowerLabel  = nullptr;  ///< "X.X W" value label
    QPointer<QLabel> m_gaugeVoltageLabel = nullptr; ///< "X.X V" value label (may be nullptr)

    // Energy history chart (getbasicdevicestats)
    // The tab index is remembered so we can rebuild it without touching others.
    int         m_energyHistoryTabIndex  = -1;
    int         m_energyResSelectedIdx   = 0;  ///< persists user resolution choice across rebuilds
    int         m_activeEnergyGrid       = 0;  ///< grid of currently displayed energy history view (0 = none)
    QComboBox *m_energyResCombo = nullptr;  ///< resolution selector inside the tab
    QTCHARTS_NS QChartView *m_energyChartView = nullptr;  ///< chart view for mouse-event forwarding (tooltip fix)
    DeviceBasicStats m_lastEnergyStats;            ///< latest stats for rebuild (single-device mode)
    int         m_lastAvailableGrids     = 0;  ///< bitmask of available grids in last build (0=900, 1=86400, 2=2678400)

    // Group energy history mode
    bool        m_groupHistoryMode       = false;  ///< true when Energy History shows a stacked group chart
    QList<QPair<QString, DeviceBasicStats>> m_lastGroupMemberStats;  ///< cached member stats for rebuild
    QStringList m_groupEnergyErrors;              ///< accumulated error messages for group members

    // Energy history error tracking
    QString     m_energyError;                    ///< current error message for single-device energy stats

    // Optional pie chart showing group member energy distribution
    QTCHARTS_NS QPieSeries *m_groupEnergyPie = nullptr;
    QTCHARTS_NS QChartView *m_groupEnergyPieView = nullptr;
    PieTooltipFilter *m_groupPieTooltipFilter = nullptr; ///< event filter for pie hover tooltips

    void updateGroupPieLabels();

    // Hover interaction: on mouse hover explode the slice for emphasis.
    // No persistent graphics items are kept for labels in this approach.

    // Tooltip text for the energy history bar chart — set/cleared by QBarSet::hovered,
    // shown via eventFilter on the chart view viewport (QHelpEvent mechanism).
    QString     m_energyBarTooltip;
};
