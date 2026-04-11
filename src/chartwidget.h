#pragma once

/// \file chartwidget.h
/// \brief Top-level chart widget that orchestrates four builder classes
///        (TemperatureChartBuilder, PowerChartBuilder, EnergyGaugeBuilder,
///        EnergyHistoryBuilder) to render device statistics.
///
/// ChartWidget owns the tab widget, scroll bars, and shared time-window
/// state.  Each builder manages its own chart tabs, axes, series, and
/// labels, accessing ChartWidget internals via friend-class grants.

#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QList>
#include <QPair>
#include <QString>
#include <QPointer>
#include <functional>

#include "fritzdevice.h"
#include "temperaturechartbuilder.h"
#include "powerchartbuilder.h"
#include "energygaugebuilder.h"
#include "energyhistorybuilder.h"

// Forward-declare Qt types used by ChartWidget
QT_FORWARD_DECLARE_CLASS(QTabWidget)
QT_FORWARD_DECLARE_CLASS(QScrollBar)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QSettings)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QCheckBox)

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
    int activeEnergyGrid() const { return m_historyBuilder.activeEnergyGrid(); }

private slots:
    void onWindowComboChanged(int index);
    void onScrollBarChanged(int value);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // -- orchestration (called once per updateDevice) -------------------------
    /// Reset axis/series/label pointers and unlock scales on device switch.
    void resetChartState(bool deviceChanged);
    /// Reparent persistent controls and delete all tab widgets safely.
    void teardownTabs();
    /// Create chart tabs (Temperature, Power, Energy, Humidity) for the device.
    void buildChartsForDevice(const FritzDevice &device,
                              const FritzDeviceList &memberDevices,
                              bool deviceChanged);
    /// Restore previously active tab, apply scroll/time-window, persist state.
    void restoreTabAndApplyWindow(const QString &activeTabText);

    // -- lock checkbox helper --------------------------------------------------
    /// Create and wire a "Lock Y scale" checkbox, assigning it to \a checkBoxOut.
    void createLockCheckBox(QPointer<QCheckBox> &checkBoxOut,
                            bool &scaleLocked,
                            double &lockedMin,
                            double &lockedMax,
                            const std::function<QValueAxis *()> &activeAxis);

    // -- energy history tab replacement ----------------------------------------
    /// Replace the Energy History tab in-place: tear down the old tab, invoke
    /// \a buildFn to insert the new one, and restore the previously active tab.
    void replaceEnergyHistoryTab(const std::function<void()> &buildFn);

    // -- time-window helpers (for rolling-poll charts) -----------------------
    /// Returns the selected window duration in milliseconds.
    qint64 windowMs() const;
    /// Re-applies the current slider window to all live chart axes.
    void applyTimeWindow();
    /// Update the horizontal scroll bar range/page to match the full data span.
    void updateScrollBar();

    // -- slider visibility ----------------------------------------------------
    void updateSliderVisibility();

    // -- settings persistence -------------------------------------------------
    void saveChartState() const;
    void loadChartState();

    // -- widgets --------------------------------------------------------------
    QTabWidget  *m_tabs            = nullptr;
    int m_windowComboIndex = 2;             ///< selected time-window index (persisted)
    QScrollBar  *m_scrollBar       = nullptr;  ///< horizontal scroll (Temperature tab)
    QScrollBar  *m_powerScrollBar  = nullptr;  ///< horizontal scroll (Power tab)

    // -- scroll state ---------------------------------------------------------
    bool m_scrollAtEnd = true;

    // -- cached device state --------------------------------------------------
    FritzDevice     m_device;          ///< last device passed to updateDevice()
    FritzDeviceList m_memberDevices;   ///< for groups: member devices

    // -- builder objects (manage chart-specific state) -------------------------
    TemperatureChartBuilder m_tempBuilder;
    PowerChartBuilder       m_powerBuilder;
    EnergyGaugeBuilder      m_gaugeBuilder;
    EnergyHistoryBuilder    m_historyBuilder;

    // Grant builders access to shared state (m_tabs, m_device, etc.)
    friend class TemperatureChartBuilder;
    friend class PowerChartBuilder;
    friend class EnergyGaugeBuilder;
    friend class EnergyHistoryBuilder;
};
