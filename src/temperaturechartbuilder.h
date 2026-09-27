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

QT_FORWARD_DECLARE_CLASS(QGraphicsLineItem)
QT_FORWARD_DECLARE_CLASS(QGraphicsRectItem)
QT_FORWARD_DECLARE_CLASS(QGraphicsSimpleTextItem)
QT_FORWARD_DECLARE_CLASS(QEvent)
// QAbstractSeries is already made visible (via QT_CHARTS_USE_NAMESPACE) by
// the QtCharts headers pulled in through chartutils.h above — do NOT forward
// declare it again here: under Qt5 it lives in the QtCharts:: namespace, and
// a bare QT_FORWARD_DECLARE_CLASS(QAbstractSeries) would create a clashing
// *global*-namespace declaration (see powerchartbuilder.h for the same issue).

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

    // -- Hover crosshair / persistent tooltip ------------------------------
    // Called once per temperature-tab (re)build (single-device or group);
    // installs a mouse-tracking event filter on the view's viewport
    // (forwarded via ChartWidget::eventFilter, since QGraphicsView::viewport()
    // is a child widget of ChartWidget's tab hierarchy) and creates the
    // crosshair line + tooltip graphics items.
    void installHoverGraphics(QChartView *view);

    /// Returns true when \a watched is the viewport of the temperature
    /// chart's QChartView — used by ChartWidget::eventFilter() to decide
    /// whether to forward the event to handleHoverEvent().
    bool ownsViewport(QObject *watched) const;

    /// Handles a QEvent::MouseMove / QEvent::Leave on the temperature chart
    /// viewport: draws a vertical dotted crosshair at the hovered timestamp
    /// and shows a persistent tooltip box with the data at that timestamp.
    /// Unlike QToolTip, the tooltip does not auto-hide on a timer — it is
    /// only hidden when the mouse leaves the chart view.
    void handleHoverEvent(QEvent *event);

private:
    ChartWidget &m_owner;

    // Temperature chart (single device)
    QChart        *m_tempChart  = nullptr;
    QDateTimeAxis *m_tempAxisX  = nullptr;
    QValueAxis    *m_tempAxisY  = nullptr;
    QXYSeries     *m_tempSeries = nullptr;
    QPointer<QLabel> m_tempValueLabel = nullptr;

    // Exact-time X-axis tick overlays (see chartutils.h updateTimeAxisOverlay).
    // Rebuilt by ChartWidget::applyTimeWindow() right after the respective
    // axis's range is set, and on chart view resize.
    TimeAxisOverlay m_tempAxisOverlay;
    TimeAxisOverlay m_groupTempAxisOverlay;

    // Group temperature chart
    QChart                 *m_groupTempChart  = nullptr;
    QDateTimeAxis          *m_groupTempAxisX = nullptr;
    QValueAxis             *m_groupTempAxisY = nullptr;
    QList<QXYSeries *>      m_groupTempSeries;
    // Temp-capable members backing m_groupTempSeries, in the same order —
    // kept independent of m_owner.m_memberDevices (which PowerChartBuilder
    // may have already overwritten to its own energy-only member subset by
    // the time a hover event fires) so formatTooltip()/findNearestTimestamp()
    // always look up the correct raw history.
    FritzDeviceList         m_groupTempMembers;

    // Lock checkbox (shared between single and group — only one is built)
    QPointer<QCheckBox> m_tempLockCheckBox = nullptr;
    bool   m_tempScaleLocked = false;
    double m_lockedTempMin   = 0.0;
    double m_lockedTempMax   = 30.0;

    // Hover crosshair / persistent tooltip (single-device or group — only one
    // tab is ever built per device, so both modes share the same graphics).
    QPointer<QChartView>    m_tempChartView  = nullptr;
    QGraphicsLineItem       *m_hoverLine      = nullptr;  ///< vertical dotted line, child of the active chart
    QGraphicsRectItem       *m_hoverInfoBg    = nullptr;  ///< tooltip background, added to the view's scene
    QGraphicsSimpleTextItem *m_hoverInfoText  = nullptr;  ///< tooltip text, child of m_hoverInfoBg
    // Series actually attached to the active chart's axes, used for
    // QChart::mapToValue()/mapToPosition() in handleHoverEvent().
    QAbstractSeries *m_hoverMappingSeries = nullptr;
    // Chart owning m_hoverLine (m_tempChart or m_groupTempChart) — needed
    // since handleHoverEvent()/positionHoverInfoBox() must call plotArea()/
    // mapToValue() on whichever chart is currently active.
    QChart *m_hoverChart = nullptr;

    /// Returns the sample timestamp (ms since epoch) nearest to \a targetMs,
    /// looked up from the cached raw history (m_owner.m_device or
    /// m_groupTempMembers) rather than the (possibly downsampled) display
    /// series, so the tooltip always reflects real polled data.
    qint64 findNearestTimestamp(qint64 targetMs) const;

    /// Builds the tooltip text for the sample nearest \a tsMs: a single
    /// "Temperature" line for a single device, or one line per group member.
    QString formatTooltip(qint64 tsMs) const;

    /// Hides the crosshair line and tooltip box.
    void hideHoverCrosshair();

    /// Positions/resizes the tooltip box near the given viewport-local mouse
    /// position, flipping to the opposite side when it would overflow the
    /// viewport bounds.
    void positionHoverInfoBox(const QPoint &viewportPos);

    // Allow ChartWidget to access applyTimeWindow axes
    friend class ChartWidget;
};
