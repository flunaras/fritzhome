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

QT_FORWARD_DECLARE_CLASS(QGraphicsLineItem)
QT_FORWARD_DECLARE_CLASS(QGraphicsRectItem)
QT_FORWARD_DECLARE_CLASS(QGraphicsSimpleTextItem)
QT_FORWARD_DECLARE_CLASS(QEvent)
// QAbstractSeries is already made visible (via QT_CHARTS_USE_NAMESPACE) by
// the QtCharts headers pulled in through chartutils.h above — do NOT forward
// declare it again here: under Qt5 it lives in the QtCharts:: namespace, and
// a bare QT_FORWARD_DECLARE_CLASS(QAbstractSeries) would create a clashing
// *global*-namespace declaration (see energyhistorybuilder.h for the same
// issue with `using QtCharts::QAbstractSeries;`).

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

    // -- Hover crosshair / persistent tooltip ------------------------------
    // Called once per power-tab (re)build; installs a mouse-tracking event
    // filter on the view's viewport (forwarded via ChartWidget::eventFilter,
    // since QGraphicsView::viewport() is a child widget of ChartWidget's tab
    // hierarchy) and creates the crosshair line + tooltip graphics items.
    void installHoverGraphics(QChartView *view);

    /// Returns true when \a watched is the viewport of the power chart's
    /// QChartView — used by ChartWidget::eventFilter() to decide whether to
    /// forward the event to handleHoverEvent().
    bool ownsViewport(QObject *watched) const;

    /// Handles a QEvent::MouseMove / QEvent::Leave on the power chart
    /// viewport: draws a vertical dotted crosshair at the hovered timestamp
    /// and shows a persistent tooltip box with the data at that timestamp.
    /// Unlike QToolTip, the tooltip does not auto-hide on a timer — it is
    /// only hidden when the mouse leaves the chart view.
    void handleHoverEvent(QEvent *event);

private:
    ChartWidget &m_owner;

    // Power chart (single-device or stacked-group share the axes)
    QChart        *m_powerChart       = nullptr;
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

    // Hover crosshair / persistent tooltip (Power tab only)
    QPointer<QChartView>    m_powerChartView  = nullptr;
    QGraphicsLineItem       *m_hoverLine      = nullptr;  ///< vertical dotted line, child of m_powerChart
    QGraphicsRectItem       *m_hoverInfoBg    = nullptr;  ///< tooltip background, added to the view's scene
    QGraphicsSimpleTextItem *m_hoverInfoText  = nullptr;  ///< tooltip text, child of m_hoverInfoBg
    // Series actually attached to m_powerAxisX/m_powerAxisY via attachAxis(),
    // used for QChart::mapToValue()/mapToPosition() in handleHoverEvent().
    // NOTE: m_powerSeries / m_powerStackedUpper entries are the *boundary*
    // QLineSeries owned by a QAreaSeries — only the QAreaSeries itself (or
    // m_powerNetSeries) is ever attached to the axes, so mapping calls MUST
    // use this pointer, not m_powerSeries/m_powerStackedUpper directly.
    QAbstractSeries *m_hoverMappingSeries = nullptr;

    /// Returns the sample timestamp (ms since epoch) nearest to \a targetMs,
    /// looked up from the cached raw history (m_owner.m_device / m_memberDevices)
    /// rather than the (possibly downsampled) display series, so the tooltip
    /// always reflects real polled data.
    qint64 findNearestTimestamp(qint64 targetMs) const;

    /// Builds the multi-line tooltip text for the sample nearest \a tsMs:
    /// one line per group member (or a single "Power" line for a single
    /// device), plus a "Net" line when the net overlay series exists.
    QString formatTooltip(qint64 tsMs) const;

    /// Hides the crosshair line and tooltip box.
    void hideHoverCrosshair();

    /// Positions/resizes the tooltip box near the given viewport-local mouse
    /// position, flipping to the opposite side when it would overflow the
    /// viewport bounds.
    void positionHoverInfoBox(const QPoint &viewportPos);

    friend class ChartWidget;
};
