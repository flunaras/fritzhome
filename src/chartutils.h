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

// ── Time-axis ticks ─────────────────────────────────────────────────────────

/// Returns a "nice" tick interval in milliseconds for a time axis spanning
/// windowMs milliseconds, targeting roughly 5-6 visible tick marks.
qint64 niceTimeTickIntervalMs(qint64 windowMs);

/// Applies dynamic ticks to a QDateTimeAxis so that tick marks land on
/// absolute round-clock times and scroll smoothly as the visible window moves.
void applyTimeAxisTicks(QDateTimeAxis *axis, qint64 windowMs);

// ── Chart factory helpers ───────────────────────────────────────────────────

/// Create a QChartView with antialiasing and minimum height.
QChartView *makeChartView(QChart *chart);

/// Create a styled QChart with hidden legend and no animation.
QChart *makeBaseChart(const QString &title);

/// Configure a QDateTimeAxis with "hh:mm" format and a title.
void configureTimeAxis(QDateTimeAxis *axis, const QString &label);

/// Wrap a widget with the grey frame border effect (matching chart tabs).
QWidget *wrapInFramedContainer(QWidget *innerWidget);

/// Create a formatted error display widget with icon and bullet list.
QWidget *createErrorDisplayWidget(const QString &caption, const QStringList &errors);

/// Create a chart tab container with value label overlay, optional scroll bar,
/// optional lock checkbox, and optional time-window combo.
QWidget *makeChartTab(QChart *chart, const QString &currentValueText,
                      QPointer<QLabel> *outLabel = nullptr,
                      QScrollBar *scrollBar = nullptr,
                      QCheckBox *lockCheckBox = nullptr,
                      QComboBox *windowCombo = nullptr);

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
