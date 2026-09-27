#pragma once

/// \file chartutils.h
/// \brief Shared chart utility functions and types used by ChartWidget and its
///        builder classes (TemperatureChartBuilder, PowerChartBuilder,
///        EnergyGaugeBuilder, EnergyHistoryBuilder).
///
/// These are pure helper functions with no dependencies on ChartWidget state.
/// They are declared in a shared header so that every builder translation unit
/// can use them without duplicating code.

#include <QDateTime>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QColor>

#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QXYSeries>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QCheckBox)
QT_FORWARD_DECLARE_CLASS(QComboBox)
QT_FORWARD_DECLARE_CLASS(QScrollBar)
QT_FORWARD_DECLARE_CLASS(QGraphicsLineItem)
QT_FORWARD_DECLARE_CLASS(QGraphicsSimpleTextItem)
// QAbstractSeries is already made visible (via QT_CHARTS_USE_NAMESPACE and the
// QXYSeries include above) — do NOT forward declare it again here: under Qt5
// it lives in the QtCharts:: namespace, and a bare
// QT_FORWARD_DECLARE_CLASS(QAbstractSeries) would create a clashing
// *global*-namespace declaration (see powerchartbuilder.h for the same issue).

// ── Constants ────────────────────────────────────────────────────────────────

/// Slider step index -> window duration in milliseconds.
/// Steps: 0=5min, 1=15min, 2=30min, 3=1h, 4=2h, 5=4h, 6=8h, 7=16h, 8=24h
extern const qint64 kWindowMs[];

/// Human-readable labels for kWindowMs[].
extern const char * const kWindowLabels[];

/// Shared colour palette for multi-series charts (group temperature, stacked
/// power, group energy pie, stacked energy history bar chart).
extern const QColor kChartPalette[];
extern const int kChartPaletteSize;

// ── Y-axis rounding ─────────────────────────────────────────────────────────

/// Result of roundAxisRange: rounded axis bounds and the tick step.
struct AxisRange {
    double lo;
    double hi;
    double step;
};

/// Returns the smallest "nice" step size >= rawStep from the sequence
/// { 1, 2, 2.5, 5 } x 10^k.
double niceStep(double rawStep);

/// Snaps [rawMin, rawMax] outward to a multiple of a "nice" step derived from
/// the data range and the desired number of tick intervals (defaulting to 5).
AxisRange roundAxisRange(double rawMin, double rawMax, int targetTicks = 5);

/// Applies a rounded axis range to a QValueAxis: sets the range bounds AND
/// the tick interval so that tick marks always land on round numbers.
void applyAxisRange(QValueAxis *axis, const AxisRange &r);

/// Like applyAxisRange but derives the target tick count from \a pixelHeight
/// so that the Y-axis density scales with the chart area.  When \a pixelHeight
/// is 0 (chart not yet laid out) it falls back to 5 ticks.
void applyAxisRangeDynamic(QValueAxis *axis, double rawMin, double rawMax,
                           int pixelHeight = 0);

// ── Time-axis ticks ─────────────────────────────────────────────────────────

/// Returns a "nice" tick interval in milliseconds for a time axis spanning
/// \a windowMs milliseconds targeting approximately \a targetTicks visible marks.
qint64 niceTimeTickIntervalMs(qint64 windowMs, int targetTicks = 10);

/// Applies dynamic ticks to a QDateTimeAxis so that tick marks land on
/// absolute round-clock times and scroll smoothly as the visible window moves.
/// Pass \a pixelWidth > 0 (the chart plot area width in pixels) to scale the
/// tick density to the available space; 0 falls back to ~10 ticks.
void applyTimeAxisTicks(QDateTimeAxis *axis, qint64 windowMs, int pixelWidth = 0);

// ── Exact time-axis tick overlay ─────────────────────────────────────────────
//
// QDateTimeAxis's native ticks are always evenly spaced between min()/max()
// into exactly tickCount() intervals, with no "round clock time" snapping.
// For most window durations that doesn't land on whole seconds/minutes/hours
// — e.g. a 15-minute window divided into 10 gaps yields 1.5-minute ticks, so
// consecutive native labels round inconsistently (+1 min, then +2 min, ...).
// computeNiceTimeStep() + updateTimeAxisOverlay() replace the native labels
// with custom-drawn tick marks + "hh:mm"/"hh:mm:ss" labels positioned at
// exact, round timestamps, so the displayed time always matches the
// underlying data and shifts smoothly (moves with the data) as the visible
// window scrolls or live-tracks new samples.

/// Returns the smallest candidate step (whole seconds/minutes/hours, from an
/// internal fixed table) that keeps the number of ticks across \a rangeMs at
/// or below \a maxTicks — i.e. the highest-resolution round step that still
/// respects the tick budget.
qint64 computeNiceTimeStep(qint64 rangeMs, int maxTicks);

/// Holds the QGraphicsItems used to draw the custom time-axis tick overlay
/// for one chart's X axis. Items are parented directly to the QChart, so they
/// are automatically destroyed when the chart/tab is torn down — callers only
/// need to clear() these lists (not delete the items) when the owning chart
/// is discarded (see e.g. PowerChartBuilder::reset()).
struct TimeAxisOverlay {
    QList<QGraphicsLineItem *> ticks;
    QList<QGraphicsSimpleTextItem *> labels;
};

/// (Re)draws the exact-time tick marks + labels for \a axisX on \a chart,
/// replacing the axis's native (evenly-spaced, non-round) labels — callers
/// must hide the native labels once via configureTimeAxis(). \a mappingSeries
/// must be a series currently attached to \a axisX (used for
/// QChart::mapToPosition() to convert timestamps to pixel X coordinates).
/// Must be (re)called after \a axisX's range has been set — e.g. from
/// ChartWidget::applyTimeWindow() right after axis->setRange(...) — and again
/// on chart/view resize, since the plot area (and therefore every tick's
/// pixel position) changes independently of the data.
void updateTimeAxisOverlay(TimeAxisOverlay &overlay, QChart *chart,
                           QDateTimeAxis *axisX, QAbstractSeries *mappingSeries);

// ── Chart factory helpers ───────────────────────────────────────────────────

/// Create a QChartView with antialiasing and minimum height.
QChartView *makeChartView(QChart *chart);

/// Create a styled QChart with hidden legend and no animation.
QChart *makeBaseChart(const QString &title);

/// Configure a QDateTimeAxis with "hh:mm" format and a title, hides its
/// native labels/gridlines/line (replaced by the custom overlay drawn by
/// updateTimeAxisOverlay()), and reserves extra bottom margin on \a chart so
/// the overlay's tick marks + labels — drawn just below the plot area — are
/// not clipped by the chart's own bounding rect.
void configureTimeAxis(QDateTimeAxis *axis, const QString &label, QChart *chart);

/// Wrap a widget with the grey frame border effect (matching chart tabs).
QWidget *wrapInFramedContainer(QWidget *innerWidget);

/// Create a formatted error display widget with icon and bullet list.
QWidget *createErrorDisplayWidget(const QString &caption, const QStringList &errors);

/// Create a chart tab container with value label overlay, optional scroll bar,
/// optional lock checkbox, and optional time-window combo.
/// \a outView, when non-null, receives the QChartView created internally so
/// callers can install hover/tooltip event filters on its viewport.
QWidget *makeChartTab(QChart *chart, const QString &currentValueText,
                      QPointer<QLabel> *outLabel = nullptr,
                      QScrollBar *scrollBar = nullptr,
                      QCheckBox *lockCheckBox = nullptr,
                      QComboBox *windowCombo = nullptr,
                      QPointer<QChartView> *outView = nullptr);

// ── Tab-text utility ────────────────────────────────────────────────────────

/// Strip keyboard-shortcut mnemonics (e.g. '&') from tab labels so that
/// "Te&mperature" == "Temperature" etc.
QString plainTabText(const QString &raw);

// ── Translated month abbreviation ───────────────────────────────────────────

/// Translated abbreviated month name (1-based: 1 = January, 12 = December).
QString monthAbbr(int month);

// ── History range scanning ──────────────────────────────────────────────────

/// Scan a time-stamped history list for min/max Y values within
/// [minMs, maxMs].  Falls back to scanning the entire list if no
/// points fall within the window.
/// Returns false if the list is empty (no data at all).
bool scanHistoryRange(
    const QList<QPair<QDateTime, double>> &history,
    qint64 minMs, qint64 maxMs,
    double &outMin, double &outMax);

/// Scan a list of QXYSeries for min/max Y values within [minMs, maxMs].
/// Falls back to scanning all points if none fall within the window.
/// Returns false if all series are empty or null.
bool scanSeriesRange(
    const QList<QXYSeries *> &seriesList,
    qint64 minMs, qint64 maxMs,
    double &outMin, double &outMax);

// ── Series downsampling ─────────────────────────────────────────────────────

/// Maximum number of data points kept in any chart series.  Beyond this
/// threshold the series is downsampled using a min/max-per-bucket algorithm
/// that preserves visual peaks and troughs while dramatically reducing the
/// number of points Qt Charts must render.
///
/// 2 000 points is well above the pixel resolution of any reasonable display
/// (charts are typically 800–2 000 pixels wide) so visual fidelity is
/// unaffected, yet it caps the rendering cost at a constant bound even when
/// the history list grows to 17 000+ entries after 24 hours of 5-second
/// polling.
constexpr int kMaxSeriesPoints = 2000;

/// Downsample a list of QPointF using min/max-per-bucket envelope decimation.
/// Divides the X range into kMaxSeriesPoints/2 equal buckets; for each bucket
/// emits the point with the minimum Y and the point with the maximum Y (in
/// time order).  This preserves visual extremes (spikes, dips) while keeping
/// the output at or below kMaxSeriesPoints entries.
///
/// If \a points already contains kMaxSeriesPoints or fewer entries, it is
/// returned unmodified (zero-copy via implicit sharing).
QList<QPointF> downsampleMinMax(const QList<QPointF> &points);

// ── Battery state normalization ──────────────────────────────────────────────

/// Battery level thresholds for 5-state normalization (0-20-40-60-80-100).
/// All battery displays use these thresholds for consistent UI.
constexpr int kBatteryEmpty = 20;    ///< Critical: 0–20%
constexpr int kBatteryLow = 40;      ///< Low: 20–40%
constexpr int kBatteryFair = 60;     ///< Fair: 40–60%
constexpr int kBatteryGood = 80;     ///< Good: 60–80%
///                                      Excellent: 80–100%

/// Get the normalized battery state color for the given battery level (0–100).
/// Returns gray (#999999) if level < 0 (N/A).
QColor batteryColorForLevel(int level);

/// Get the battery icon (unicode) for the given battery level (0–100).
/// Returns generic battery emoji "🔋" for N/A (level < 0).
QString batteryIconForLevel(int level);

/// Get the human-readable status text for the given battery level and low flag.
/// Translatable status descriptions for the 5 battery states plus N/A.
QString batteryStatusTextForLevel(int level, bool lowFlag);
