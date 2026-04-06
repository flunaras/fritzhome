#include "chartwidget.h"
#include "i18n_shim.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QLabel>
#include <QSlider>
#include <QScrollBar>
#include <QComboBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QTextOption>
#include <QIcon>
#include <QDateTime>
#include <QSettings>
#include <QToolTip>
#include <QCursor>
#include <QHelpEvent>
#include <QDebug>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QTimer>
#include <QMap>
#include <QVector>
#include <QGraphicsLineItem>
#include <limits>
#include <cmath>
#include <algorithm>
#include <functional>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QXYSeries>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>

// Qt5 requires QT_CHARTS_USE_NAMESPACE to pull chart types into the global namespace.
// Qt6 has no such macro (types are already global) — so guard it.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

// ---------------------------------------------------------------------------
// Slider step index -> window duration in milliseconds
// Steps: 0=5min, 1=15min, 2=30min, 3=1h, 4=2h
// ---------------------------------------------------------------------------
static const qint64 kWindowMs[] = {
    5  * 60 * 1000LL,    // 0: 5 min
    15 * 60 * 1000LL,    // 1: 15 min
    30 * 60 * 1000LL,    // 2: 30 min
    60 * 60 * 1000LL,    // 3: 1 h
    120 * 60 * 1000LL,   // 4: 2 h
    240 * 60 * 1000LL,   // 5: 4 h
    480 * 60 * 1000LL,   // 6: 8 h
    960 * 60 * 1000LL,   // 7: 16 h
    1440 * 60 * 1000LL   // 8: 24 h
};

static const char * const kWindowLabels[] = {
    "5 min", "15 min", "30 min", "1 h", "2 h", "4 h", "8 h", "16 h", "24 h"
};

// Qt (or the KDE style) may inject keyboard-shortcut mnemonics (e.g. '&') into
// tab labels.  Strip them before comparing or persisting tab titles so that
// "Te&mperature" == "Temperature" etc.
static QString plainTabText(const QString &raw)
{
    QString s = raw;
    s.remove(QLatin1Char('&'));
    return s;
}

// ---------------------------------------------------------------------------
// Translated abbreviated month name (1-based: 1 = January, 12 = December).
// Uses i18n() so the strings follow the application language (KDE locale on
// KF builds, QTranslator on Qt-only builds) rather than QLocale::system(),
// which may differ from the UI language.
// ---------------------------------------------------------------------------
static QString monthAbbr(int month)
{
    switch (month) {
    case  1: return i18n("Jan");
    case  2: return i18n("Feb");
    case  3: return i18n("Mar");
    case  4: return i18n("Apr");
    case  5: return i18n("May");
    case  6: return i18n("Jun");
    case  7: return i18n("Jul");
    case  8: return i18n("Aug");
    case  9: return i18n("Sep");
    case 10: return i18n("Oct");
    case 11: return i18n("Nov");
    case 12: return i18n("Dec");
    default: return QString::number(month);
    }
}

// Forward declaration — defined below, before PieTooltipFilter.
static void updatePieSliceLabels(QPieSeries *series, QPieSlice *explodedSlice);

void ChartWidget::updateGroupPieLabels()
{
    if (!m_groupEnergyPie)
        return;

    // Compute total to decide on unit scaling (Wh vs kWh)
    double totalWh = 0.0;
    for (QPieSlice *s : m_groupEnergyPie->slices()) {
        totalWh += s->value();
    }
    // Use kWh when typical absolute values exceed 1000 Wh
    const bool useKwh = (totalWh >= 1000.0);
    const double scale = useKwh ? 0.001 : 1.0;
    const QString unit = useKwh ? i18n("kWh") : i18n("Wh");

    // Update each slice label to include both absolute and percent values.
    for (QPieSlice *s : m_groupEnergyPie->slices()) {
        double val = s->value();
        double pct = (totalWh > 0.0) ? (val / totalWh * 100.0) : 0.0;
        const double absVal = val * scale;
        QString name = s->property("memberName").toString();
        if (name.isEmpty()) name = s->label();
        // Example: "Living Room: 1.234 kWh (12.3 %)"
        s->setLabel(QString::fromUtf8("%1: %2 %3 (%4 %)")
                    .arg(name)
                    .arg(absVal, 0, 'f', useKwh ? 3 : 1)
                    .arg(unit)
                    .arg(pct, 0, 'f', 1));
    }

    // Delegate label visibility to the shared helper: no slice is exploded
    // at build time, so this applies the idle-mode overlap logic.
    updatePieSliceLabels(m_groupEnergyPie, nullptr);
}

// layoutGroupPieLabelsSimple removed; hover-explode approach used instead.

// Layout pie labels manually to avoid overlaps. Qt Charts places labels
// naively and they can collide when slices are small or many. This function
// creates QGraphicsTextItem and QGraphicsLineItem children on the chart's
// scene and attempts a simple force-directed layout along radial lines.
// layoutGroupPieLabels removed during rollback.

// ---------------------------------------------------------------------------
// Time-axis tick helpers
// ---------------------------------------------------------------------------

/// Returns a "nice" tick interval in milliseconds for a time axis spanning
/// windowMs milliseconds, targeting roughly 5–6 visible tick marks.
/// Intervals snap to round clock values: 1 min, 2 min, 5 min, 10 min,
/// 15 min, 30 min, 1 h, 2 h, 4 h, 6 h, 12 h, 24 h.
static qint64 niceTimeTickIntervalMs(qint64 windowMs)
{
    // Candidate intervals in milliseconds (ascending)
    static const qint64 kCandidates[] = {
        1  * 60 * 1000LL,   //  1 min
        2  * 60 * 1000LL,   //  2 min
        5  * 60 * 1000LL,   //  5 min
        10 * 60 * 1000LL,   // 10 min
        15 * 60 * 1000LL,   // 15 min
        30 * 60 * 1000LL,   // 30 min
        60 * 60 * 1000LL,   //  1 h
        2  * 3600 * 1000LL, //  2 h
        4  * 3600 * 1000LL, //  4 h
        6  * 3600 * 1000LL, //  6 h
        12 * 3600 * 1000LL, // 12 h
        24 * 3600 * 1000LL, // 24 h
    };
    const int n = static_cast<int>(sizeof(kCandidates) / sizeof(kCandidates[0]));
    const int targetTicks = 5;
    qint64 rawStep = windowMs / targetTicks;
    for (int i = 0; i < n; ++i) {
        if (kCandidates[i] >= rawStep)
            return kCandidates[i];
    }
    return kCandidates[n - 1];
}

/// Applies dynamic ticks to a QDateTimeAxis so that tick marks land on
/// absolute round-clock times (e.g. :00, :15, :30) and scroll smoothly
/// as the visible window moves.  Call this whenever the window range changes.
static void applyTimeAxisTicks(QDateTimeAxis *axis, qint64 windowMs)
{
    // QDateTimeAxis only supports setTickCount(int) in both Qt5 and Qt6.
    // (setTickType/setTickInterval/setTickAnchor exist only on QValueAxis.)
    // Derive a tick count from the window size so ticks are not too dense.
    qint64 intervalMs = niceTimeTickIntervalMs(windowMs);
    int tickCount = qMax(2, static_cast<int>(windowMs / intervalMs) + 1);
    axis->setTickCount(tickCount);
}

// ---------------------------------------------------------------------------
// Y-axis rounding helpers
// ---------------------------------------------------------------------------

/// Returns the smallest "nice" step size >= rawStep from the sequence
/// { 1, 2, 2.5, 5 } × 10^k.  This ensures tick marks always land on
/// round numbers (or multiples of 0.5 / 0.1 for small temperature ranges).
static double niceStep(double rawStep)
{
    if (rawStep <= 0.0)
        return 1.0;
    double magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
    double normalised = rawStep / magnitude;   // in [1, 10)
    double niceFraction;
    if      (normalised <= 1.0) niceFraction = 1.0;
    else if (normalised <= 2.0) niceFraction = 2.0;
    else if (normalised <= 2.5) niceFraction = 2.5;
    else if (normalised <= 5.0) niceFraction = 5.0;
    else                        niceFraction = 10.0;
    return niceFraction * magnitude;
}

/// Result of roundAxisRange: rounded axis bounds and the tick step.
struct AxisRange {
    double lo;
    double hi;
    double step;
};

/// Snaps [rawMin, rawMax] outward to a multiple of a "nice" step derived from
/// the data range and the desired number of tick intervals (defaulting to 5).
/// lo <= rawMin, hi >= rawMax, and both are exact multiples of step.
///
/// For temperature data (small ranges) this produces 0.5 °C or 1 °C steps;
/// for power/energy data it produces round-number steps (1 W, 2 W, 5 W, …).
static AxisRange roundAxisRange(double rawMin, double rawMax, int targetTicks = 5)
{
    if (targetTicks < 1) targetTicks = 1;
    double span = rawMax - rawMin;
    if (span <= 0.0) span = 1.0;
    double step = niceStep(span / targetTicks);
    double lo   = std::floor(rawMin / step) * step;
    double hi   = std::ceil (rawMax / step) * step;
    // Guarantee at least one step above rawMax so the top tick is always visible.
    if (hi < rawMax + step * 0.01)
        hi += step;
    return { lo, hi, step };
}

/// Applies a rounded axis range to a QValueAxis: sets the range bounds AND
/// the tick interval so that tick marks always land on round numbers.
static void applyAxisRange(QValueAxis *axis, const AxisRange &r)
{
    axis->setRange(r.lo, r.hi);
    axis->setTickInterval(r.step);
    axis->setTickAnchor(r.lo);
    axis->setTickType(QValueAxis::TicksDynamic);
}

// ---------------------------------------------------------------------------
// Small helpers shared by multiple builders
// ---------------------------------------------------------------------------

static QChartView *makeChartView(QChart *chart)
{
    QChartView *view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setMinimumHeight(200);
    return view;
}

// Helper function to wrap a widget with the grey frame border effect
// (matching the look of charts in other tabs)
static QWidget *wrapInFramedContainer(QWidget *innerWidget)
{
    // Outer panel keeps the default (grey) tab background so that the
    // QTabWidget frame stays visible — matching other tabs.
    QWidget *panel = new QWidget();
    QVBoxLayout *outerVl = new QVBoxLayout(panel);
    // NOTE: Do NOT set margins to 0 — the default margins create the grey frame effect!

    // Inner content widget with a white background
    QWidget *inner = new QWidget();
    QPalette pal = inner->palette();
    pal.setColor(QPalette::Window, Qt::white);
    inner->setPalette(pal);
    inner->setAutoFillBackground(true);

    QVBoxLayout *innerVl = new QVBoxLayout(inner);
    innerVl->setContentsMargins(0, 0, 0, 0);
    innerWidget->setParent(inner);
    innerVl->addWidget(innerWidget);

    outerVl->addWidget(inner);
    return panel;
}

// Helper function to create a formatted error display widget with icon and bullet list
static QWidget *createErrorDisplayWidget(const QString &caption, const QStringList &errors)
{
    QWidget *container = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Header with icon and caption
    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);

    QLabel *iconLabel = new QLabel();
    QIcon errorIcon = QIcon::fromTheme(QStringLiteral("dialog-error"));
    if (!errorIcon.isNull()) {
        iconLabel->setPixmap(errorIcon.pixmap(32, 32));
    }
    headerLayout->addWidget(iconLabel);

    QLabel *captionLabel = new QLabel(caption);
    QFont captionFont = captionLabel->font();
    captionFont.setBold(true);
    captionFont.setPointSize(captionFont.pointSize() + 1);
    captionLabel->setFont(captionFont);
    captionLabel->setWordWrap(true);
    headerLayout->addWidget(captionLabel, 1);

    mainLayout->addLayout(headerLayout);

    // Error messages as bullet list
    if (!errors.isEmpty()) {
        QLabel *errorListLabel = new QLabel();
        errorListLabel->setWordWrap(true);

        // Build HTML for bullet list
        QString htmlContent = "<ul style='margin-top: 0; margin-bottom: 0;'>";
        for (const QString &error : errors) {
            htmlContent += "<li>" + error.toHtmlEscaped() + "</li>";
        }
        htmlContent += "</ul>";

        errorListLabel->setText(htmlContent);
        mainLayout->addWidget(errorListLabel);
    }

    mainLayout->addStretch();
    return container;
}

// ---------------------------------------------------------------------------
// Pie-label visibility helper
// ---------------------------------------------------------------------------
// Updates which pie-slice labels are visible depending on whether a slice is
// currently exploded (hovered).
//
//  * If explodedSlice != nullptr  →  show ONLY that slice's label.
//  * If explodedSlice == nullptr  →  show all labels, but hide overlapping
//    ones, keeping the label of the larger (by value) slice in each
//    conflicting pair.
//
// "Overlap" is detected by angular proximity: two labels whose slice
// mid-angles are closer than kMinAngularSep degrees are considered
// overlapping.  This is a heuristic — actual rendered label bounding
// boxes are internal to Qt Charts and not accessible.
static void updatePieSliceLabels(QPieSeries *series, QPieSlice *explodedSlice)
{
    if (!series)
        return;
    const QList<QPieSlice*> slices = series->slices();

    if (explodedSlice) {
        // ── Exploded mode: show only the exploded slice's label ──────
        for (QPieSlice *s : slices)
            s->setLabelVisible(s == explodedSlice);
        return;
    }

    // ── Idle mode: show all labels, then hide overlapping smaller ones ──
    // First make every non-zero slice label visible.
    for (QPieSlice *s : slices)
        s->setLabelVisible(s->angleSpan() > 0.0);

    // Minimum angular separation (degrees) between mid-angles for two
    // labels to be considered non-overlapping.  Labels whose mid-angles
    // are closer than this will overlap; the smaller slice's label is
    // hidden.
    const double kMinAngularSep = 22.0;

    // Build a list of (midAngle, index) for visible slices, sorted by
    // midAngle so we can check neighbours efficiently.
    struct SliceInfo {
        double midAngle;
        int    index;
    };
    QVector<SliceInfo> infos;
    infos.reserve(slices.size());
    for (int i = 0; i < slices.size(); ++i) {
        QPieSlice *s = slices[i];
        if (s->angleSpan() <= 0.0) continue;
        double mid = fmod(s->startAngle() + s->angleSpan() * 0.5, 360.0);
        infos.append({mid, i});
    }
    // Sort by midAngle
    std::sort(infos.begin(), infos.end(),
              [](const SliceInfo &a, const SliceInfo &b) { return a.midAngle < b.midAngle; });

    // For each consecutive pair (including wrap-around last↔first), if
    // they are too close hide the smaller slice's label.
    auto angularDist = [](double a, double b) -> double {
        double d = std::fabs(a - b);
        return d > 180.0 ? 360.0 - d : d;
    };
    const int n = infos.size();
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        if (i == j) break; // single slice
        double sep = angularDist(infos[i].midAngle, infos[j].midAngle);
        if (sep < kMinAngularSep) {
            // Hide the label of the smaller slice
            QPieSlice *si = slices[infos[i].index];
            QPieSlice *sj = slices[infos[j].index];
            if (si->value() < sj->value())
                si->setLabelVisible(false);
            else
                sj->setLabelVisible(false);
        }
    }
}

// Event filter used to show hover tooltips for pie slices in a QChartView.
// Maps the mouse position to the pie center/radius and finds the slice whose
// angular span contains the cursor. A QToolTip is shown with absolute and
// percent values. Implemented here to avoid QPieSlice::setToolTip which is not
// available across all Qt versions.
class PieTooltipFilter : public QObject {
    Q_OBJECT
public:
    PieTooltipFilter(QPieSeries *series, QObject *parent = nullptr)
        : QObject(parent), m_series(series), m_lastSlice(nullptr), m_candidateSlice(nullptr), m_candidateCount(0) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        QPieSeries *series = m_series.data();
        if (!series || !watched)
            return QObject::eventFilter(watched, event);

        // When the mouse leaves the viewport, collapse any exploded slice
        // and restore idle-mode label visibility.
        if (event->type() == QEvent::Leave) {
            if (m_lastSlice) {
                m_lastSlice->setExploded(false);
                m_lastSlice = nullptr;
                updatePieSliceLabels(series, nullptr);
            }
            m_candidateSlice = nullptr;
            m_candidateCount = 0;
            return QObject::eventFilter(watched, event);
        }

        if (event->type() == QEvent::ToolTip || event->type() == QEvent::MouseMove) {
            QWidget *w = qobject_cast<QWidget*>(watched);
            QPoint pos;
            // Handle QHelpEvent for ToolTip and QMouseEvent for MouseMove to
            // avoid casting to the wrong event subclass which is undefined
            // behaviour and can crash on some Qt versions.
            if (event->type() == QEvent::ToolTip) {
                QHelpEvent *he = static_cast<QHelpEvent*>(event);
                pos = he->pos();
            } else {
                QMouseEvent *me = static_cast<QMouseEvent*>(event);
                // Use pos() which returns a QPoint (widget coordinates)
                pos = me->pos();
            }
            // Try to compute the pie center from the QChart's plotArea so the
            // angle math matches the actual drawn pie.  plotArea() returns
            // coordinates in the QChart's **local item** coordinate system,
            // so we must map the cursor into that same space.  The chain is:
            //   viewport coords  →  scene coords  →  chart-local coords
            //   cv->mapToScene()     chart->mapFromScene()
            //
            // Fallback to widget center when chart/view isn't available.
            QPointF center;
            QChartView *cv = qobject_cast<QChartView*>(w->parentWidget());
            QRectF pa;
            QPointF cursorLocal;  // cursor position in the same coord space as pa / center
            if (cv && cv->chart()) {
                pa = cv->chart()->plotArea();
                center = pa.center();
                // Map cursor: viewport → scene → chart-local
                QPointF scenePos = cv->mapToScene(pos);
                cursorLocal = cv->chart()->mapFromScene(scenePos);
            } else {
                // viewport fallback — treat the viewport as the plot area
                pa = QRectF(0, 0, w->width(), w->height());
                center = QPointF(w->width() / 2.0, w->height() / 2.0);
                cursorLocal = QPointF(pos);
            }
            QPointF delta = cursorLocal - center;
            const double dist = std::hypot(delta.x(), delta.y());
            // Disable exploding when the cursor is within a small center radius
            // (25% of the smaller plot-area dimension) because hovering near
            // the center often ambiguously maps to multiple slices.
            const double centerRadius = qMin(cv ? cv->chart()->plotArea().width() : w->width(),
                                             cv ? cv->chart()->plotArea().height() : w->height()) * 0.25;
            if (dist < centerRadius) {
                // Collapse last exploded slice if any
                if (m_lastSlice) {
                    m_lastSlice->setExploded(false);
                    m_lastSlice = nullptr;
                    updatePieSliceLabels(series, nullptr);
                }
                return false; // let normal tooltip processing continue
            }

            // ── Convert cursor angle from atan2 to Qt-pie convention ──────
            // Screen atan2: 0°=right, +90°=down (Y-down screen coords)
            // Qt pie chart: 0°=top (12 o'clock), angles increase clockwise
            // Conversion:   qt_angle = atan2_angle + 90°
            //
            // The pie is always rendered as a **circle** inscribed in the
            // shorter dimension of the plotArea — NOT as an ellipse filling
            // the full rectangle.  Therefore we must NOT normalise the delta
            // by the plotArea semi-axes; doing so would skew the angle and
            // cause the wrong slice to be detected when the plotArea is
            // non-square (e.g. 749×166).  Use the raw delta directly.
            double atan2Deg = std::atan2(delta.y(), delta.x()) * 180.0 / M_PI;
            double angle = fmod(atan2Deg + 90.0, 360.0);
            if (angle < 0) angle += 360.0;

            // Compute total once for the tooltip percentage display
            double total = 0.0;
            for (QPieSlice *s : series->slices()) total += s->value();

            // ── Slice detection: use QPieSlice::startAngle() / angleSpan() ──
            // These are the authoritative angles Qt uses to render each slice
            // (Qt-pie convention: 0°=top, clockwise).  We simply check which
            // slice's angular range contains the cursor angle we computed above.
            bool found = false;
            QPieSlice *foundSlice = nullptr;
            for (QPieSlice *s : series->slices()) {
                if (s->angleSpan() <= 0.0)
                    continue;                       // skip zero-size slices
                double sliceStart = fmod(s->startAngle(), 360.0);
                if (sliceStart < 0) sliceStart += 360.0;
                double sliceEnd = sliceStart + s->angleSpan();
                // Normal case: the slice does not wrap around 360°
                if (sliceEnd <= 360.0) {
                    if (angle >= sliceStart && angle < sliceEnd) {
                        found = true;
                        foundSlice = s;
                        break;
                    }
                } else {
                    // Slice wraps around 360° → 0°
                    if (angle >= sliceStart || angle < fmod(sliceEnd, 360.0)) {
                        found = true;
                        foundSlice = s;
                        break;
                    }
                }
            }

            if (!found) {
                // No slice under cursor: hide tooltip and collapse any previously exploded slice
                QToolTip::hideText();
                if (m_lastSlice) {
                    m_lastSlice->setExploded(false);
                    updatePieSliceLabels(series, nullptr);
                }
                m_lastSlice = nullptr;
                m_candidateSlice = nullptr;
                m_candidateCount = 0;
                return QObject::eventFilter(watched, event);
            }

            // Compose the tooltip for foundSlice
            QPieSlice *s = foundSlice;
            double pct = (s->value() / total) * 100.0;
            QString unit = (total >= 1000.0) ? i18n("kWh") : i18n("Wh");
            double scale = (total >= 1000.0) ? 0.001 : 1.0;
            QString name = s->property("memberName").toString();
            if (name.isEmpty()) name = s->label();
            QString tip = QString::fromUtf8("%1:\n%2 %3 (%4 %)")
                          .arg(name)
                          .arg(s->value() * scale, 0, 'f', (unit == "kWh") ? 3 : 1)
                          .arg(unit)
                          .arg(pct, 0, 'f', 1);
            QToolTip::showText(w->mapToGlobal(pos), tip, w);

            // Explode logic: introduce a tiny hysteresis so noisy moves near
            // boundaries don't cause rapid flip-flopping. Require two consecutive
            // detections of the same slice before switching the exploded slice.
            if (m_lastSlice == s) {
                // already exploded — nothing to do
            } else if (m_candidateSlice == s) {
                // second consecutive detection -> commit
                ++m_candidateCount;
                const int kConfirmCount = 2;
                if (m_candidateCount >= kConfirmCount) {
                    if (m_lastSlice) m_lastSlice->setExploded(false);
                    s->setExploded(true);
                    m_lastSlice = s;
                    m_candidateSlice = nullptr;
                    m_candidateCount = 0;
                    updatePieSliceLabels(series, s);
                }
            } else {
                // new candidate
                m_candidateSlice = s;
                m_candidateCount = 1;
            }

            return true; // handled
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QPieSeries> m_series;
    QPointer<QPieSlice>  m_lastSlice;
    QPointer<QPieSlice>  m_candidateSlice;
    int                  m_candidateCount;
};

// Q_OBJECT macro requires moc; include moc at end of file via generated rcc/moc
// Include the moc generated file so PieTooltipFilter's Q_OBJECT is processed by moc.
#include "chartwidget.moc"

/// Wraps a QChartView in a container that shows a large current-value label
/// in the top-right corner, overlaid via an absolute-positioned label on top
/// of the chart view inside a QStackedLayout.
/// If outLabel is non-null, the created QLabel pointer is stored there for
/// later in-place text updates.
/// If scrollBar is non-null it is placed below the chart (reparented into the
/// container so it is shown/hidden with the tab).
/// If lockCheckBox is non-null it is overlaid inside the chart area, anchored
/// to the bottom-left corner.
static QWidget *makeChartTab(QChart *chart, const QString &currentValueText,
                             QPointer<QLabel> *outLabel = nullptr,
                             QScrollBar *scrollBar = nullptr,
                             QCheckBox *lockCheckBox = nullptr,
                             QComboBox *windowCombo = nullptr)
{
    QChartView *view = makeChartView(chart);

    // Value label — large, bold, right-aligned, floating top-right.
    // Must be transparent for mouse events so overlays beneath it remain clickable.
    QLabel *valueLabel = new QLabel(currentValueText);
    valueLabel->setObjectName("currentValueLabel");
    valueLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QFont f = valueLabel->font();
    f.setPointSize(18);
    f.setBold(true);
    valueLabel->setFont(f);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
    valueLabel->setStyleSheet("color: #333333; background: transparent; padding: 6px 10px 0 0;");

    // Inner stacked widget: chart view underneath, overlays on top
    QWidget *chartStack = new QWidget();
    QStackedLayout *stack = new QStackedLayout(chartStack);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->addWidget(view);
    stack->addWidget(valueLabel);
    valueLabel->raise();
    if (outLabel)
        *outLabel = valueLabel;

    // Time-window combo — direct child of chartStack, anchored top-left.
    // Placed outside QStackedLayout so it doesn't create a full-size sibling
    // that would eat mouse events across the whole chart area.
    // Note: setParent(chartStack) transfers ownership to chartStack, so the
    // combo is destroyed when the tab is torn down. The QPointer in the caller
    // goes null, and the rescue guard in each build function recreates it.
    if (windowCombo) {
        // Caption label sits to the left of the combo
        QLabel *caption = new QLabel(i18n("Time window:"), chartStack);
        caption->setStyleSheet("background: transparent;");
        caption->adjustSize();

        windowCombo->setParent(chartStack);
        windowCombo->setStyleSheet("QComboBox { background: palette(button); }");
        windowCombo->show();
        windowCombo->adjustSize();

        // Position both widgets at top-left with a small margin.
        // Use an event filter on chartStack to reposition on resize.
        struct ComboFilter : public QObject {
            QLabel    *cap;
            QComboBox *cmb;
            ComboFilter(QLabel *c, QComboBox *b, QObject *parent)
                : QObject(parent), cap(c), cmb(b) {}
            void reposition() {
                QWidget *p = qobject_cast<QWidget*>(parent());
                if (!p) return;
                cap->adjustSize();
                cmb->adjustSize();
                int x = 6, y = 4;
                cap->move(x, y + (cmb->height() - cap->height()) / 2);
                cmb->move(x + cap->width() + 4, y);
            }
            bool eventFilter(QObject *, QEvent *e) override {
                if (e->type() == QEvent::Resize || e->type() == QEvent::Show)
                    reposition();
                return false;
            }
        };
        ComboFilter *cf = new ComboFilter(caption, windowCombo, chartStack);
        chartStack->installEventFilter(cf);
        cf->reposition();
        caption->raise();
        windowCombo->raise();
    }

    // Lock checkbox — overlaid inside the chart area, anchored bottom-left.
    // Placed as a direct child of chartStack (not inside the QStackedLayout)
    // so it doesn't create a full-size sibling that would eat mouse events.
    if (lockCheckBox) {
        lockCheckBox->setParent(chartStack);
        lockCheckBox->show();
        // Position it bottom-left (6 px margin) initially, and reposition on every resize.
        auto positionCb = [lockCheckBox, chartStack]() {
            QSize cs = chartStack->size();
            QSize ls = lockCheckBox->sizeHint();
            lockCheckBox->move(6, cs.height() - ls.height() - 6);
        };
        positionCb();
        // Use a plain QObject event-filter lambda via a helper class
        struct ResizeFilter : public QObject {
            QCheckBox *cb;
            ResizeFilter(QCheckBox *c, QObject *parent) : QObject(parent), cb(c) {}
            bool eventFilter(QObject *, QEvent *e) override {
                if (e->type() == QEvent::Resize) {
                    QWidget *p = qobject_cast<QWidget*>(parent());
                    if (p) {
                        QSize cs = p->size();
                        QSize ls = cb->sizeHint();
                        cb->move(6, cs.height() - ls.height() - 6);
                    }
                }
                return false;
            }
        };
        chartStack->installEventFilter(new ResizeFilter(lockCheckBox, chartStack));
        lockCheckBox->raise();
    }

    if (scrollBar) {
        // Outer container: chart stack on top, scroll bar pinned at bottom
        QWidget *container = new QWidget();
        QVBoxLayout *vl = new QVBoxLayout(container);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);
        vl->addWidget(chartStack, /*stretch=*/1);
        scrollBar->setParent(container);
        scrollBar->show();
        vl->addWidget(scrollBar, /*stretch=*/0);
        return container;
    }

    return chartStack;
}

static QChart *makeBaseChart(const QString &title)
{
    QChart *chart = new QChart();
    chart->setTitle(title);
    chart->legend()->hide();
    chart->setAnimationOptions(QChart::NoAnimation);
    chart->setTheme(QChart::ChartThemeQt);
    return chart;
}

static void configureTimeAxis(QDateTimeAxis *axis, const QString &label)
{
    axis->setFormat("hh:mm");
    axis->setTitleText(label);
    // Tick count and interval are applied dynamically via applyTimeAxisTicks()
    // whenever the window range is set, so we don't set them here.
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ChartWidget::ChartWidget(QWidget *parent)
    : QWidget(parent)
    , m_tabs(new QTabWidget(this))
    // per-tab combos are created on demand; selected index persisted in m_windowComboIndex
    , m_scrollBar(new QScrollBar(Qt::Horizontal, this))
    , m_powerScrollBar(new QScrollBar(Qt::Horizontal, this))
{
    // Time-window combos: 9 options matching kWindowLabels[], default index 2 (30 min).
    // m_windowCombo is for the Power chart; m_windowComboTemp is for Temperature.
    // They are always kept in sync — changing one updates the other silently.
    // No persistent combo objects here; per-tab combos are created by builders using
    // the persisted m_windowComboIndex and update that index on change.
    // Save active tab whenever the user switches tabs.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        saveChartState();
        updateSliderVisibility();
    });

    // Scroll bar: allows viewing any part of the collected history.
    // Range and page step are kept in sync with the data span and window size.
    // When at maximum the view follows live data ("auto-scroll").
    m_scrollBar->setRange(0, 0);
    m_scrollBar->setSingleStep(60);   // 1-minute steps with arrow keys
    m_scrollBar->hide();              // hidden until embedded in a Temperature tab
    connect(m_scrollBar, &QScrollBar::valueChanged, this, &ChartWidget::onScrollBarChanged);

    m_powerScrollBar->setRange(0, 0);
    m_powerScrollBar->setSingleStep(60);
    m_powerScrollBar->hide();         // hidden until embedded in the Power tab
    connect(m_powerScrollBar, &QScrollBar::valueChanged, this, &ChartWidget::onScrollBarChanged);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tabs, /*stretch=*/1);

    // Restore previously saved window-combo index (tab is restored per-device in updateDevice)
    loadChartState();
}

// ---------------------------------------------------------------------------
// Slider visibility helper — now a no-op; combo is embedded as chart overlay
// ---------------------------------------------------------------------------

void ChartWidget::updateSliderVisibility()
{
    // The time-window combo is overlaid directly inside each Temperature/Power
    // chart tab by makeChartTab(); nothing to show/hide here.
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ChartWidget::onWindowComboChanged(int /*index*/)
{
    updateScrollBar();
    applyTimeWindow();
}

void ChartWidget::onScrollBarChanged(int value)
{
    // Determine which bar fired and sync the peer silently.
    QScrollBar *peer = (sender() == m_scrollBar) ? m_powerScrollBar : m_scrollBar;
    if (peer) {
        peer->blockSignals(true);
        peer->setValue(value);
        peer->blockSignals(false);
    }
    // Use whichever non-null bar has a valid range to read the maximum.
    QScrollBar *any = m_scrollBar ? m_scrollBar : m_powerScrollBar;
    if (!any) return;
    m_scrollAtEnd = (value >= any->maximum());
    applyTimeWindow();
}

void ChartWidget::onEnergyResolutionChanged(int index)
{
    if (!m_groupHistoryMode && !m_lastEnergyStats.valid)
        return;

    // Persist the user's choice before we tear down the combo.
    m_energyResSelectedIdx = index;

    // Null out m_energyResCombo first to avoid re-entrancy:
    // buildEnergyHistoryChart* will create a new combo and assign it.
    m_energyResCombo = nullptr;
    m_energyChartView = nullptr;
    // If a previous group pie view / series existed and is about to be destroyed
    // when we delete the tabs below, remove its event filter and clear cached
    // pointers so we don't later dereference freed objects.
    if (m_groupPieTooltipFilter) {
        if (m_groupEnergyPieView)
            m_groupEnergyPieView->viewport()->removeEventFilter(m_groupPieTooltipFilter);
        // The filter's parent is the viewport so it will be deleted with the view;
        // just drop our pointer to avoid use-after-free.
        m_groupPieTooltipFilter = nullptr;
    }
    m_groupEnergyPie = nullptr;
    m_groupEnergyPieView = nullptr;

    if (m_energyHistoryTabIndex >= 0 && m_energyHistoryTabIndex < m_tabs->count()) {
        // Remember which tab the user is on before we touch anything.
        const QString savedActiveTab = plainTabText(m_tabs->tabText(m_tabs->currentIndex()));

        // Block signals so that removeTab/insertTab do not fire currentChanged
        // and corrupt QSettings with a transient wrong tab name.
        m_tabs->blockSignals(true);

        QWidget *old = m_tabs->widget(m_energyHistoryTabIndex);
        m_tabs->removeTab(m_energyHistoryTabIndex);
        // Use deleteLater() instead of delete: we are currently executing inside
        // a signal emitted by a child widget of 'old' (the resolution combo).
        // Calling delete here would destroy the sender mid-signal and crash.
        old->deleteLater();

        if (m_groupHistoryMode)
            buildEnergyHistoryChartStacked(m_lastGroupMemberStats);
        else
            buildEnergyHistoryChart(m_lastEnergyStats);

        // Restore the tab the user was on before the rebuild.
        int restoreIdx = m_tabs->currentIndex(); // fallback: whatever Qt picked
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (plainTabText(m_tabs->tabText(i)) == savedActiveTab) {
                restoreIdx = i;
                break;
            }
        }
        m_tabs->setCurrentIndex(restoreIdx);

        m_tabs->blockSignals(false);

        // Fire side-effects manually now that the correct tab is selected.
        saveChartState();
        updateSliderVisibility();
    } else {
        if (m_groupHistoryMode)
            buildEnergyHistoryChartStacked(m_lastGroupMemberStats);
        else
            buildEnergyHistoryChart(m_lastEnergyStats);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ChartWidget::updateDevice(const FritzDevice &device,
                               const FritzDeviceList &memberDevices)
{
    // Detect whether the selected device has changed
    const bool deviceChanged = (m_device.ain != device.ain);

    // Reset resolution selection when switching to a different device
    if (deviceChanged) {
        m_groupHistoryMode = false;
        m_lastGroupMemberStats.clear();
        m_energyError.clear();
        m_groupEnergyErrors.clear();
        // Unlock the power Y scale so it auto-fits the new device's data
        m_powerScaleLocked = false;
        if (m_powerLockCheckBox) {
            m_powerLockCheckBox->blockSignals(true);
            m_powerLockCheckBox->setChecked(false);
            m_powerLockCheckBox->blockSignals(false);
        }
        // Unlock the temperature Y scale so it auto-fits the new device's data
        m_tempScaleLocked = false;
        if (m_tempLockCheckBox) {
            m_tempLockCheckBox->blockSignals(true);
            m_tempLockCheckBox->setChecked(false);
            m_tempLockCheckBox->blockSignals(false);
        }
    }

    // Decide which tab to restore after the rebuild:
    //   • Same device → keep whatever tab the user is currently on (local var)
    //   • Different device → use the persisted QSettings preference so the user's
    //     preferred tab (e.g. "Power") survives device switches
    QString activeTabText;
    if (!deviceChanged && m_tabs->currentIndex() >= 0) {
        activeTabText = plainTabText(m_tabs->tabText(m_tabs->currentIndex()));
    } else {
        QSettings s;
        activeTabText = s.value(QStringLiteral("ui/chartTab")).toString();
    }

    // Cache device for applyTimeWindow / rescale helpers
    m_device = device;
    m_memberDevices = memberDevices;

    // Null out stored axis/series/label pointers – they will be owned by the new charts
    m_tempAxisX       = nullptr;
    m_tempAxisY       = nullptr;
    m_tempSeries      = nullptr;
    m_tempValueLabel  = nullptr;
    m_powerAxisX       = nullptr;
    m_powerAxisY       = nullptr;
    m_powerSeries      = nullptr;
    m_powerLowerSeries = nullptr;
    m_powerValueLabel  = nullptr;
    m_powerStackedUpper.clear();
    m_powerStackedLower.clear();
    m_groupTempAxisX = nullptr;
    m_groupTempAxisY = nullptr;
    m_groupTempSeries.clear();
    m_humiditySeries  = nullptr;
    m_gaugeKwhLabel     = nullptr;
    m_gaugePowerLabel   = nullptr;
    m_gaugeVoltageLabel = nullptr;
    m_energyHistoryTabIndex = -1;
    m_activeEnergyGrid      = 0;
    m_lastAvailableGrids    = 0;
    m_lastEnergyStats       = DeviceBasicStats();
    m_energyResCombo = nullptr;
    m_energyChartView = nullptr;
    // The lock checkboxes are owned by the tab widgets (reparented in makeChartTab).
    // Reset to nullptr here so the "if (m_powerLockCheckBox)" guards in the next
    // device-switch don't dereference the now-deleted widgets.
    m_powerLockCheckBox = nullptr;
    m_tempLockCheckBox  = nullptr;

    // Re-parent both scroll bars back to this widget before destroying the old tabs,
    // so they are not deleted when the tab containers they were embedded in are deleted.
    // Reparent transient controls back to this widget to avoid them being
    // deleted when their previous tab container is removed. Use QPointer
    // guards where members may be null or already deleted.
    if (QPointer<QScrollBar> sb = m_scrollBar) sb->setParent(this);
    if (QPointer<QScrollBar> psb = m_powerScrollBar) psb->setParent(this);

    // Block currentChanged signals during the entire clear+rebuild cycle so that
    // intermediate tab changes (caused by removeTab/addTab) do not fire
    // saveChartState() with transient/wrong tab names.
    m_tabs->blockSignals(true);

    // Clear all tabs. When deleting a tab that may own the pie/chart view,
    // remove the pie event filter and clear cached pointers to avoid
    // use-after-free if the ChartWidget keeps a raw pointer to the series
    // or view that will be deleted with the tab widget.
    while (m_tabs->count() > 0) {
        QWidget *w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        // If the widget being deleted contains the pie view's viewport, make
        // sure to remove the installed event filter and drop our pointers.
        if (m_groupEnergyPieView && w && (w == m_groupEnergyPieView || w->isAncestorOf(m_groupEnergyPieView))) {
         if (m_groupPieTooltipFilter && m_groupEnergyPieView)
                m_groupEnergyPieView->viewport()->removeEventFilter(m_groupPieTooltipFilter);
            m_groupPieTooltipFilter = nullptr;
            m_groupEnergyPie = nullptr;
            m_groupEnergyPieView = nullptr;
        }
        // Several UI widgets stored as member pointers live inside tab widgets
        // (labels, combos). If the tab being deleted owns them, clear our cached
        // pointers so we don't later dereference freed widgets.
        if (w) {
            if (m_gaugeKwhLabel && (w == m_gaugeKwhLabel || w->isAncestorOf(m_gaugeKwhLabel)))
                m_gaugeKwhLabel = nullptr;
            if (m_gaugePowerLabel && (w == m_gaugePowerLabel || w->isAncestorOf(m_gaugePowerLabel)))
                m_gaugePowerLabel = nullptr;
            if (m_gaugeVoltageLabel && (w == m_gaugeVoltageLabel || w->isAncestorOf(m_gaugeVoltageLabel)))
                m_gaugeVoltageLabel = nullptr;
            // pie mode combo is not a per-tab owned widget; we clear any cached
            // transient pointers in the tab as done for other labels above.
            if (m_powerValueLabel && (w == m_powerValueLabel || w->isAncestorOf(m_powerValueLabel)))
                m_powerValueLabel = nullptr;
            if (m_tempValueLabel && (w == m_tempValueLabel || w->isAncestorOf(m_tempValueLabel)))
                m_tempValueLabel = nullptr;
        }
        delete w;
    }

    if (device.isGroup()) {
        // For groups: show a multi-line temperature chart for all temp-capable members.
        // buildGroupTemperatureChart is a no-op if no member has temperature.
        buildGroupTemperatureChart(memberDevices);
    } else if (device.hasTemperature() || device.hasThermostat()) {
        buildTemperatureChart(device);
    }
    if (device.hasEnergyMeter()) {
        buildPowerChart(device, memberDevices);
        buildEnergyGauge(device, memberDevices);
        // Build energy history chart only on first display or device switch.
        // On repeated poll-driven updateDevice calls for the same device the
        // energy history tab is left in place; it is rebuilt by updateEnergyStats
        // when a real throttled stats fetch completes.
        if (device.basicStats.valid && deviceChanged && !device.isGroup()) {
            m_lastEnergyStats = device.basicStats;
            buildEnergyHistoryChart(device.basicStats);
        } else if (!device.basicStats.valid || (device.isGroup() && deviceChanged)) {
            // Placeholder until getbasicdevicestats arrives (single device),
            // or until group member stats are fetched (group device).
            QLabel *placeholder = new QLabel(i18n("Fetching energy history…"), this);
            placeholder->setAlignment(Qt::AlignCenter);
            m_energyHistoryTabIndex = m_tabs->addTab(placeholder, i18n("Energy History"));
        } else {
            // Same device, stats already valid — re-insert the existing chart.
            // updateEnergyStats / updateGroupEnergyStats will rebuild it when the
            // next throttled fetch fires.
            if (m_groupHistoryMode)
                buildEnergyHistoryChartStacked(m_lastGroupMemberStats);
            else
                buildEnergyHistoryChart(m_lastEnergyStats);
        }
    }
    if (device.hasHumidity()) {
        buildHumidityChart(device);
    }

    if (m_tabs->count() == 0) {
        QLabel *placeholder = new QLabel(i18n("No chart data available for this device."), this);
        placeholder->setAlignment(Qt::AlignCenter);
        m_tabs->addTab(placeholder, i18n("Info"));
    }

    // Restore the previously active tab (or fall back to tab 0).
    {
        int restoreIdx = 0;
        if (!activeTabText.isEmpty()) {
            for (int i = 0; i < m_tabs->count(); ++i) {
                if (plainTabText(m_tabs->tabText(i)) == activeTabText) {
                    restoreIdx = i;
                    break;
                }
            }
        }
        m_tabs->setCurrentIndex(restoreIdx);
    }

    // Re-enable signals now that the correct tab is selected. The setCurrentIndex
    // above did NOT emit currentChanged (signals were blocked), so we fire the
    // relevant side-effects manually below.
    m_tabs->blockSignals(false);

    // Apply the current slider window to the freshly built axes
    m_scrollAtEnd = true;
    updateScrollBar();
    applyTimeWindow();

    // Persist the restored tab and sync slider visibility.
    saveChartState();
    updateSliderVisibility();
}

void ChartWidget::updateRollingCharts(const FritzDevice &device,
                                      const FritzDeviceList &memberDevices)
{
    // Update the cached device so rescale helpers use fresh history
    m_device = device;
    // Note: m_memberDevices is updated below in the stacked path (filtered to
    // energy-capable members); for single-device mode it stays as-is.

    // Helper: rebuild a QXYSeries from a history list using clear+append.
    // replace() on a series owned by QAreaSeries is unreliable in Qt5;
    // clear()+append() fires the correct signals that QAreaSeries listens to.
    auto reloadSeries = [](QXYSeries *series,
                           const QList<QPair<QDateTime, double>> &history,
                           double fallbackValue,
                           bool hasFallback,
                           qint64 fallbackTs)
    {
        if (!series)
            return;
        series->clear();
        for (const auto &p : history)
            series->append(p.first.toMSecsSinceEpoch(), p.second);
        if (history.isEmpty() && hasFallback)
            series->append(fallbackTs, fallbackValue);
    };

    // --- Temperature (single-device) ---
    {
        qint64 fallbackTs = QDateTime::currentMSecsSinceEpoch();
        bool hasFallback = (device.temperature > -273.0);
        reloadSeries(m_tempSeries, device.temperatureHistory,
                     device.temperature, hasFallback, fallbackTs);
        if (m_tempValueLabel && device.temperature > -273.0)
            m_tempValueLabel->setText(
                QString::number(device.temperature, 'f', 1) + " \u00B0C");
    }

    // --- Temperature (group: one series per temp-capable member) ---
    if (!m_groupTempSeries.isEmpty()) {
        // Filter to temp-capable members in the same order as buildGroupTemperatureChart
        FritzDeviceList tempMembers;
        for (const FritzDevice &m : memberDevices) {
            if (m.hasTemperature() || m.hasThermostat())
                tempMembers.append(m);
        }
        // Only update if the member count matches what was built (avoid mismatches)
        if (tempMembers.size() == m_groupTempSeries.size()) {
            for (int i = 0; i < tempMembers.size(); ++i) {
                const FritzDevice &mem = tempMembers.at(i);
                qint64 fallbackTs = QDateTime::currentMSecsSinceEpoch();
                bool hasFallback = (mem.temperature > -273.0);
                reloadSeries(m_groupTempSeries.at(i), mem.temperatureHistory,
                             mem.temperature, hasFallback, fallbackTs);
            }
        }
    }

    // --- Power (single-device or group fallback) ---
    {
        qint64 fallbackTs = QDateTime::currentMSecsSinceEpoch();
        bool hasFallback = device.energyStats.valid;
        reloadSeries(m_powerSeries, device.powerHistory,
                     device.energyStats.power, hasFallback, fallbackTs);
        // Keep the lower (zero baseline) series in sync with the upper series
        // time range so QAreaSeries renders the fill correctly for all points.
        if (m_powerLowerSeries) {
            m_powerLowerSeries->clear();
            if (!device.powerHistory.isEmpty()) {
                m_powerLowerSeries->append(
                    device.powerHistory.first().first.toMSecsSinceEpoch(), 0);
                m_powerLowerSeries->append(
                    device.powerHistory.last().first.toMSecsSinceEpoch(), 0);
            } else if (hasFallback) {
                m_powerLowerSeries->append(fallbackTs - 3600000LL, 0);
                m_powerLowerSeries->append(fallbackTs, 0);
            }
        }
        if (m_powerSeries && m_powerValueLabel && device.energyStats.valid)
            m_powerValueLabel->setText(
                QString::number(device.energyStats.power, 'f', 1) + " W");
    }

    // --- Stacked power (group mode) ---
    if (!m_powerStackedUpper.isEmpty()
        && m_powerStackedUpper.size() == m_powerStackedLower.size()) {

        const int n = m_powerStackedUpper.size();  // == energyMembers count at build time

        // Filter incoming memberDevices to energy-capable members only — same
        // filter used in buildPowerChart so the ordering is consistent.
        FritzDeviceList energyMembers;
        for (const FritzDevice &m : memberDevices) {
            if (m.hasEnergyMeter())
                energyMembers.append(m);
        }

        // If the energy member count changed (device added/removed), a full
        // rebuild is needed; skip rolling update and let the next updateDevice
        // call handle it.  Update the value label at minimum.
        if (energyMembers.size() != n) {
            if (m_powerValueLabel && device.energyStats.valid)
                m_powerValueLabel->setText(
                    QString::number(device.energyStats.power, 'f', 1) + " W");
        } else {
            // Update cached member list with fresh filtered devices
            m_memberDevices = energyMembers;

            // Build timestamp union across all members
            QMap<qint64, QVector<double>> tsMap;
            for (int i = 0; i < n; ++i) {
                for (const auto &pt : m_memberDevices.at(i).powerHistory) {
                    qint64 ts = pt.first.toMSecsSinceEpoch();
                    if (!tsMap.contains(ts))
                        tsMap[ts] = QVector<double>(n, 0.0);
                    tsMap[ts][i] = pt.second;
                }
            }

            // Rebuild each stacked layer in-place
            const QList<qint64> timestamps = tsMap.keys();
            for (int i = 0; i < n; ++i) {
                QXYSeries *upper = m_powerStackedUpper.at(i);
                QXYSeries *lower = m_powerStackedLower.at(i);
                if (!upper || !lower)
                    continue;
                upper->clear();
                lower->clear();
                for (qint64 ts : timestamps) {
                    const QVector<double> &vals = tsMap[ts];
                    double lowerVal = 0.0;
                    for (int j = 0; j < i; ++j) lowerVal += vals[j];
                    double upperVal = lowerVal + vals[i];
                    upper->append(ts, upperVal);
                    lower->append(ts, lowerVal);
                }
            }

            // Update power label to show group total
            if (m_powerValueLabel && device.energyStats.valid)
                m_powerValueLabel->setText(
                    QString::number(device.energyStats.power, 'f', 1) + " W");
        }
    }

    // --- Humidity ---
    {
        qint64 fallbackTs = QDateTime::currentMSecsSinceEpoch();
        bool hasFallback = device.humidityStats.valid;
        reloadSeries(m_humiditySeries, device.humidityHistory,
                     device.humidityStats.humidity, hasFallback, fallbackTs);
    }

    // --- Energy gauge tab labels ---
    if (device.energyStats.valid) {
        if (m_gaugeKwhLabel)
            m_gaugeKwhLabel->setText(
                QString::number(device.energyStats.energy / 1000.0, 'f', 3) + " kWh");
        if (m_gaugePowerLabel)
            m_gaugePowerLabel->setText(
                QString::number(device.energyStats.power, 'f', 1) + " W");
        if (m_gaugeVoltageLabel && device.energyStats.voltage > 0)
            m_gaugeVoltageLabel->setText(
                QString::number(device.energyStats.voltage, 'f', 1) + " V");
    }

    // Rescale axes to the current time window
    updateScrollBar();
    applyTimeWindow();
}

// ---------------------------------------------------------------------------
// Returns the values list of the energy series matching the given grid from
// stats, or an empty list.  Used to detect no-op refreshes.
// ---------------------------------------------------------------------------
static QList<double> energyValuesForGrid(const DeviceBasicStats &stats, int grid)
{
    for (const StatSeries &ss : stats.energy)
        if (ss.grid == grid)
            return ss.values;
    return {};
}

// Concatenated values for the given grid across all members, in member order.
static QList<double> groupEnergyValuesForGrid(
    const QList<QPair<QString, DeviceBasicStats>> &memberStats, int grid)
{
    QList<double> combined;
    for (const auto &pair : memberStats)
        combined += energyValuesForGrid(pair.second, grid);
    return combined;
}

// Compute a bitmask indicating which energy grids are available (non-empty) in stats.
// Bit 0 = grid 900, Bit 1 = grid 86400, Bit 2 = grid 2678400.
// Returns 0 if no grids are available.
static int computeAvailableGrids(const DeviceBasicStats &stats)
{
    int availableGrids = 0;
    for (const StatSeries &s : stats.energy) {
        if (s.grid == 900 && !s.values.isEmpty())
            availableGrids |= (1 << 0);
        if (s.grid == 86400 && !s.values.isEmpty())
            availableGrids |= (1 << 1);
        if (s.grid == 2678400 && !s.values.isEmpty())
            availableGrids |= (1 << 2);
    }
    return availableGrids;
}

// Compute a bitmask for group member stats: returns non-zero if ANY member has each grid.
static int computeAvailableGridsForGroup(
    const QList<QPair<QString, DeviceBasicStats>> &memberStats)
{
    int availableGrids = 0;
    for (const auto &pair : memberStats)
        availableGrids |= computeAvailableGrids(pair.second);
    return availableGrids;
}

void ChartWidget::updateEnergyStats(const DeviceBasicStats &stats)
{
    // Clear any previous error when new data arrives successfully
    m_energyError.clear();

    // Compute which grids are available in the new stats
    int currentAvailableGrids = computeAvailableGrids(stats);

    // Skip the full tab rebuild when:
    // 1. The displayed grid's values haven't changed, AND
    // 2. The set of available grids hasn't changed (no new data)
    // This prevents the UI from rebuilding when partial data is already showing.
    if (m_activeEnergyGrid > 0
            && energyValuesForGrid(stats, m_activeEnergyGrid)
               == energyValuesForGrid(m_lastEnergyStats, m_activeEnergyGrid)
            && currentAvailableGrids == m_lastAvailableGrids) {
        m_lastEnergyStats = stats;   // still update cache (fetchTime etc. may differ)
        return;
    }

    m_lastEnergyStats = stats;
    m_lastAvailableGrids = currentAvailableGrids;

    if (!m_device.hasEnergyMeter())
        return;

    if (m_energyHistoryTabIndex >= 0 && m_energyHistoryTabIndex < m_tabs->count()) {
        // Remember which tab the user is on before we touch anything.
        const QString savedTitle = plainTabText(m_tabs->tabText(m_tabs->currentIndex()));

        // Block signals so that removeTab/insertTab do not fire currentChanged
        // and corrupt QSettings with a transient wrong tab name.
        m_tabs->blockSignals(true);

        QWidget *old = m_tabs->widget(m_energyHistoryTabIndex);
        m_tabs->removeTab(m_energyHistoryTabIndex);
        delete old;
        m_energyResCombo = nullptr;
    m_energyChartView = nullptr;
        // Insert the real chart at the same position
        buildEnergyHistoryChart(stats);

        // Restore the previously active tab
        int restoreIdx = m_tabs->currentIndex(); // fallback
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (plainTabText(m_tabs->tabText(i)) == savedTitle) {
                restoreIdx = i;
                break;
            }
        }
        m_tabs->setCurrentIndex(restoreIdx);

        m_tabs->blockSignals(false);

        // Fire side-effects manually now that the correct tab is selected.
        saveChartState();
        updateSliderVisibility();
    } else {
        // Energy history tab doesn't exist yet — rebuild everything is safest
        // but expensive. Instead just record and let the next updateDevice pick it up.
        // (This can happen if the device was not selected when stats arrived.)
    }
}

void ChartWidget::updateGroupEnergyStats(const QList<QPair<QString, DeviceBasicStats>> &memberStats)
{
    // Clear any previous errors when new data arrives successfully
    m_groupEnergyErrors.clear();

    // Compute which grids are available in the new member stats
    int currentAvailableGrids = computeAvailableGridsForGroup(memberStats);

    // Skip rebuild when:
    // 1. The displayed grid's values haven't changed, AND
    // 2. The set of available grids hasn't changed (no new data)
    // This prevents the UI from rebuilding when partial data is already showing.
    if (m_activeEnergyGrid > 0
            && groupEnergyValuesForGrid(memberStats, m_activeEnergyGrid)
               == groupEnergyValuesForGrid(m_lastGroupMemberStats, m_activeEnergyGrid)
            && currentAvailableGrids == m_lastAvailableGrids) {
        m_lastGroupMemberStats = memberStats;
        m_groupHistoryMode     = true;
        return;
    }

    m_lastGroupMemberStats = memberStats;
    m_groupHistoryMode     = true;
    m_lastAvailableGrids   = currentAvailableGrids;

    if (!m_device.hasEnergyMeter())
        return;

    if (m_energyHistoryTabIndex >= 0 && m_energyHistoryTabIndex < m_tabs->count()) {
        const QString savedTitle = plainTabText(m_tabs->tabText(m_tabs->currentIndex()));

        m_tabs->blockSignals(true);

        QWidget *old = m_tabs->widget(m_energyHistoryTabIndex);
        m_tabs->removeTab(m_energyHistoryTabIndex);
        delete old;
        m_energyResCombo = nullptr;
    m_energyChartView = nullptr;
        buildEnergyHistoryChartStacked(memberStats);

        int restoreIdx = m_tabs->currentIndex();
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (plainTabText(m_tabs->tabText(i)) == savedTitle) {
                restoreIdx = i;
                break;
            }
        }
        m_tabs->setCurrentIndex(restoreIdx);

        m_tabs->blockSignals(false);

        saveChartState();
        updateSliderVisibility();
    }

    // Note: Do NOT rebuild the group energy pie chart here.
    // The pie chart must use lifetime cumulative energy from energyStats (same as the member's
    // individual Energy gauge), not time-series values. Since DeviceBasicStats only contains
    // time-series data (not energyStats), we can't construct the correct pie here.
    // Instead, the pie chart is correctly built in buildEnergyGauge() which has access to
    // the full EnergyStats via memberDevices. That function is called whenever device data
    // is updated, which includes after new energy history stats arrive.
}

void ChartWidget::updateEnergyStatsError(const QString &error)
{
    // Store the error and update the placeholder if it's currently displayed
    m_energyError = error;

    if (m_energyHistoryTabIndex >= 0 && m_energyHistoryTabIndex < m_tabs->count()) {
        QWidget *current = m_tabs->widget(m_energyHistoryTabIndex);

        // If it's already a framed container (error display), update the content inside
        QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(current->layout());
        if (layout && layout->count() > 0) {
            // This is the outer panel; get the inner widget
            QWidget *inner = layout->itemAt(0)->widget();
            if (inner) {
                QVBoxLayout *innerLayout = qobject_cast<QVBoxLayout *>(inner->layout());
                if (innerLayout && innerLayout->count() > 0) {
                    // If the inner widget is our error display widget, replace it
                    QWidget *oldWidget = innerLayout->itemAt(0)->widget();
                    if (oldWidget) {
                        innerLayout->removeWidget(oldWidget);
                        delete oldWidget;

                        // Create new error display with updated message
                        QString caption = i18n("Error fetching energy history");
                        QStringList errorList;
                        errorList.append(error);
                        QWidget *errorWidget = createErrorDisplayWidget(caption, errorList);
                        innerLayout->addWidget(errorWidget);
                        return;
                    }
                }
            }
        }

        // Otherwise, replace the placeholder with a framed error display
        m_tabs->blockSignals(true);
        QWidget *old = m_tabs->widget(m_energyHistoryTabIndex);
        m_tabs->removeTab(m_energyHistoryTabIndex);
        delete old;
        m_energyResCombo = nullptr;
        m_energyChartView = nullptr;

        // Create formatted error display with framed styling
        QString caption = i18n("Error fetching energy history");
        QStringList errorList;
        errorList.append(error);
        QWidget *errorWidget = createErrorDisplayWidget(caption, errorList);
        QWidget *framedWidget = wrapInFramedContainer(errorWidget);
        m_tabs->insertTab(m_energyHistoryTabIndex, framedWidget, i18n("Energy History"));
        m_tabs->blockSignals(false);
    }
}

void ChartWidget::updateGroupEnergyStatsError(const QString &memberName, const QString &error)
{
    // Accumulate error messages for group members
    QString errorMsg = memberName.isEmpty()
        ? error
        : i18n("%1: %2", memberName, error);

    if (!m_groupEnergyErrors.contains(errorMsg))
        m_groupEnergyErrors.append(errorMsg);

    if (m_energyHistoryTabIndex >= 0 && m_energyHistoryTabIndex < m_tabs->count()) {
        QWidget *current = m_tabs->widget(m_energyHistoryTabIndex);

        // If it's already a framed container (error display), update the content inside
        QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(current->layout());
        if (layout && layout->count() > 0) {
            // This is the outer panel; get the inner widget
            QWidget *inner = layout->itemAt(0)->widget();
            if (inner) {
                QVBoxLayout *innerLayout = qobject_cast<QVBoxLayout *>(inner->layout());
                if (innerLayout && innerLayout->count() > 0) {
                    // If the inner widget is our error display widget, replace it
                    QWidget *oldWidget = innerLayout->itemAt(0)->widget();
                    if (oldWidget) {
                        innerLayout->removeWidget(oldWidget);
                        delete oldWidget;

                        // Create new error display with updated message list
                        QString caption = i18n("Error fetching energy history for group");
                        QWidget *errorWidget = createErrorDisplayWidget(caption, m_groupEnergyErrors);
                        innerLayout->addWidget(errorWidget);
                        return;
                    }
                }
            }
        }

        // Otherwise, replace the placeholder with a framed error display
        m_tabs->blockSignals(true);
        QWidget *old = m_tabs->widget(m_energyHistoryTabIndex);
        m_tabs->removeTab(m_energyHistoryTabIndex);
        delete old;
        m_energyResCombo = nullptr;
        m_energyChartView = nullptr;

        // Create formatted error display with framed styling
        QString caption = i18n("Error fetching energy history for group");
        QWidget *errorWidget = createErrorDisplayWidget(caption, m_groupEnergyErrors);
        QWidget *framedWidget = wrapInFramedContainer(errorWidget);
        m_tabs->insertTab(m_energyHistoryTabIndex, framedWidget, i18n("Energy History"));
        m_tabs->blockSignals(false);
    }
}

// ---------------------------------------------------------------------------
// Chart builders
// ---------------------------------------------------------------------------

void ChartWidget::buildTemperatureChart(const FritzDevice &dev)
{
    // Per-tab time-window combo is created below; persistent combo instances
    // are no longer reused via reparenting to avoid lifetime/reparenting issues.

    QLineSeries *series = new QLineSeries();
    series->setName(i18n("Temperature"));

    for (const auto &point : dev.temperatureHistory) {
        series->append(point.first.toMSecsSinceEpoch(), point.second);
    }

    // If no history yet but we have a current value, show a single point
    if (dev.temperatureHistory.isEmpty() && dev.temperature > -273.0) {
        series->append(QDateTime::currentDateTime().toMSecsSinceEpoch(), dev.temperature);
    }

    QChart *chart = makeBaseChart(i18n("Temperature History"));
    chart->addSeries(series);

    QDateTimeAxis *axisX = new QDateTimeAxis();
    configureTimeAxis(axisX, i18n("Time"));
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText(i18n("°C"));
    axisY->setLabelFormat("%.1f");
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    if (!dev.temperatureHistory.isEmpty()) {
        double minT = dev.temperatureHistory.first().second;
        double maxT = dev.temperatureHistory.first().second;
        for (const auto &p : dev.temperatureHistory) {
            minT = qMin(minT, p.second);
            maxT = qMax(maxT, p.second);
        }
        applyAxisRange(axisY, roundAxisRange(minT, maxT));
    } else {
        axisY->setRange(0, 30);
    }

    // Thermostat target line
    if (dev.hasThermostat() && dev.thermostatStats.targetTemp > 0
        && dev.thermostatStats.targetTemp < 253) {
        double targetC = (dev.thermostatStats.targetTemp - 16) * 0.5 + 8.0;
        QLineSeries *targetSeries = new QLineSeries();
        QPen pen(Qt::DashLine);
        pen.setColor(QColor(255, 100, 0));
        targetSeries->setPen(pen);
        targetSeries->setName(i18n("Target"));

        if (!dev.temperatureHistory.isEmpty()) {
            qint64 t0 = dev.temperatureHistory.first().first.toMSecsSinceEpoch();
            qint64 t1 = dev.temperatureHistory.last().first.toMSecsSinceEpoch();
            targetSeries->append(t0, targetC);
            targetSeries->append(t1, targetC);
        } else {
            qint64 now = QDateTime::currentDateTime().toMSecsSinceEpoch();
            targetSeries->append(now - 3600000, targetC);
            targetSeries->append(now, targetC);
        }
        chart->addSeries(targetSeries);
        targetSeries->attachAxis(axisX);
        targetSeries->attachAxis(axisY);
        chart->legend()->show();
    }

    // Store axis pointers for in-place updates
    m_tempAxisX = axisX;
    m_tempAxisY = axisY;
    m_tempSeries = series;

    // Create the "Lock Y scale" checkbox overlaid inside the chart area (bottom-left).
    // Created here (not in the constructor) so a fresh instance exists after every device switch.
    m_tempLockCheckBox = new QCheckBox(i18n("Lock Y scale"));
    m_tempLockCheckBox->setToolTip(i18n("When checked, the vertical axis range is frozen at its\n"
                                        "current scale and will not auto-adjust as new data arrives\n"
                                        "or the time window is scrolled."));
    // Restore persisted lock state for this checkbox instance
    m_tempLockCheckBox->setChecked(m_tempScaleLocked);
    connect(m_tempLockCheckBox, &QCheckBox::toggled, this, [this](bool locked) {
        m_tempScaleLocked = locked;
        if (locked && m_tempAxisY) {
            // Capture the current axis range at the moment the user locks it
            m_lockedTempMin = m_tempAxisY->min();
            m_lockedTempMax = m_tempAxisY->max();
        } else if (!locked) {
            // Immediately re-fit the axis to the currently visible data
            applyTimeWindow();
        }
        saveChartState();
    });

    // Format current temperature for the overlay label
    QString currentText;
    if (dev.temperature > -273.0)
        currentText = QString::number(dev.temperature, 'f', 1) + " °C";

    // Create a per-tab time-window combo and initialize from m_windowComboIndex
    QComboBox *tmpWindowCombo = new QComboBox();
    for (int i = 0; i < 9; ++i) tmpWindowCombo->addItem(i18n("Last %1", kWindowLabels[i]));
    tmpWindowCombo->setCurrentIndex(qBound(0, m_windowComboIndex, 8));
    connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx){ m_windowComboIndex = idx; onWindowComboChanged(idx); saveChartState(); });
    m_tabs->addTab(makeChartTab(chart, currentText, &m_tempValueLabel, m_scrollBar, m_tempLockCheckBox, tmpWindowCombo), i18n("Temperature"));
}

void ChartWidget::buildGroupTemperatureChart(const FritzDeviceList &memberDevices)
{
    // Per-tab time-window combo is created below; persistent combo instances
    // are no longer reused via reparenting to avoid lifetime/reparenting issues.

    // Collect temperature-capable members
    FritzDeviceList tempMembers;
    for (const FritzDevice &m : memberDevices) {
        if (m.hasTemperature() || m.hasThermostat())
            tempMembers.append(m);
    }
    if (tempMembers.isEmpty())
        return;

    static const QColor kPalette[] = {
        QColor(0,   120, 215),
        QColor(220, 80,  0  ),
        QColor(0,   153, 76 ),
        QColor(180, 0,   180),
        QColor(200, 160, 0  ),
        QColor(0,   180, 200),
        QColor(220, 50,  50 ),
        QColor(80,  80,  200),
    };
    const int paletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

    QChart *chart = makeBaseChart(i18n("Temperature History"));

    QDateTimeAxis *axisX = new QDateTimeAxis();
    configureTimeAxis(axisX, i18n("Time"));
    chart->addAxis(axisX, Qt::AlignBottom);

    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText(i18n("°C"));
    axisY->setLabelFormat("%.1f");
    chart->addAxis(axisY, Qt::AlignLeft);

    double minAll =  std::numeric_limits<double>::max();
    double maxAll =  std::numeric_limits<double>::lowest();
    bool anyData  = false;

    const int n = tempMembers.size();
    for (int i = 0; i < n; ++i) {
        const FritzDevice &mem = tempMembers.at(i);
        QLineSeries *series = new QLineSeries();
        series->setName(mem.name);

        QColor c = kPalette[i % paletteSize];
        QPen pen(c);
        pen.setWidth(2);
        series->setPen(pen);

        for (const auto &pt : mem.temperatureHistory) {
            series->append(pt.first.toMSecsSinceEpoch(), pt.second);
            minAll = qMin(minAll, pt.second);
            maxAll = qMax(maxAll, pt.second);
            anyData = true;
        }
        // Fallback single point if history is empty but current value is known
        if (mem.temperatureHistory.isEmpty() && mem.temperature > -273.0) {
            series->append(QDateTime::currentDateTime().toMSecsSinceEpoch(), mem.temperature);
            minAll = qMin(minAll, mem.temperature);
            maxAll = qMax(maxAll, mem.temperature);
            anyData = true;
        }

        chart->addSeries(series);
        series->attachAxis(axisX);
        series->attachAxis(axisY);

        m_groupTempSeries.append(series);
    }

    if (anyData) {
        applyAxisRange(axisY, roundAxisRange(minAll, maxAll));
    } else {
        axisY->setRange(0, 30);
    }

    chart->legend()->show();

    m_groupTempAxisX = axisX;
    m_groupTempAxisY = axisY;

    // Create the "Lock Y scale" checkbox overlaid inside the chart area (bottom-left).
    // Shares the same m_tempScaleLocked / m_lockedTempMin / m_lockedTempMax state as
    // buildTemperatureChart — only one of the two paths is ever built per device.
    m_tempLockCheckBox = new QCheckBox(i18n("Lock Y scale"));
    m_tempLockCheckBox->setToolTip(i18n("When checked, the vertical axis range is frozen at its\n"
                                        "current scale and will not auto-adjust as new data arrives\n"
                                        "or the time window is scrolled."));
    m_tempLockCheckBox->setChecked(m_tempScaleLocked);
    connect(m_tempLockCheckBox, &QCheckBox::toggled, this, [this](bool locked) {
        m_tempScaleLocked = locked;
        if (locked) {
            // Capture whichever temp axis is active (group path uses m_groupTempAxisY)
            QValueAxis *activeY = m_groupTempAxisY ? m_groupTempAxisY : m_tempAxisY;
            if (activeY) {
                m_lockedTempMin = activeY->min();
                m_lockedTempMax = activeY->max();
            }
        } else {
            // Immediately re-fit the axis to the currently visible data
            applyTimeWindow();
        }
        saveChartState();
    });

    QComboBox *tmpWindowCombo = new QComboBox();
    for (int i = 0; i < 9; ++i) tmpWindowCombo->addItem(i18n("Last %1", kWindowLabels[i]));
    tmpWindowCombo->setCurrentIndex(qBound(0, m_windowComboIndex, 8));
    connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx){ m_windowComboIndex = idx; onWindowComboChanged(idx); saveChartState(); });
    m_tabs->addTab(makeChartTab(chart, QString(), nullptr, m_scrollBar, m_tempLockCheckBox, tmpWindowCombo), i18n("Temperature"));
}

void ChartWidget::buildPowerChart(const FritzDevice &dev,
                                  const FritzDeviceList &memberDevices)
{
    // Per-tab time-window combo is created below; persistent combo instances
    // are no longer reused via reparenting to avoid lifetime/reparenting issues.

    // Determine whether to build a stacked chart (group with ≥2 energy-capable members)
    // or the standard single-device area chart.
    FritzDeviceList energyMembers;
    if (dev.isGroup()) {
        for (const FritzDevice &m : memberDevices) {
            if (m.hasEnergyMeter())
                energyMembers.append(m);
        }
    }
    const bool stackedMode = (energyMembers.size() >= 2);

    QChart *chart = makeBaseChart(i18n("Power Consumption"));
    QDateTimeAxis *axisX = new QDateTimeAxis();
    configureTimeAxis(axisX, i18n("Time"));
    chart->addAxis(axisX, Qt::AlignBottom);
    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText(i18n("Watts"));
    axisY->setLabelFormat("%.1f");
    axisY->setMin(0);
    chart->addAxis(axisY, Qt::AlignLeft);

    // Create the "Lock Y scale" checkbox that will be embedded inside the chart tab.
    // It is created here (not in the constructor) so a fresh instance exists after every
    // device switch.  The pointer is stored in m_powerLockCheckBox for use by rescaleYPower.
    m_powerLockCheckBox = new QCheckBox(i18n("Lock Y scale"));
    m_powerLockCheckBox->setToolTip(i18n("When checked, the vertical axis range is frozen at its\n"
                                         "current scale and will not auto-adjust as new data arrives\n"
                                         "or the time window is scrolled."));
    // Restore persisted lock state for this checkbox instance
    m_powerLockCheckBox->setChecked(m_powerScaleLocked);
    connect(m_powerLockCheckBox, &QCheckBox::toggled, this, [this](bool locked) {
        m_powerScaleLocked = locked;
        if (locked && m_powerAxisY) {
            // Capture the current axis range at the moment the user locks it
            m_lockedPowerMin = m_powerAxisY->min();
            m_lockedPowerMax = m_powerAxisY->max();
        } else if (!locked) {
            // Immediately re-fit the axis to the currently visible data
            applyTimeWindow();
        }
        saveChartState();
    });

    if (stackedMode) {
        // Color palette for stacked layers
        static const QColor kPalette[] = {
            QColor(0,   120, 215),
            QColor(220, 80,  0  ),
            QColor(0,   153, 76 ),
            QColor(180, 0,   180),
            QColor(200, 160, 0  ),
            QColor(0,   180, 200),
            QColor(220, 50,  50 ),
            QColor(80,  80,  200),
        };
        const int paletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));

        // Build cumulative time series: gather all unique timestamps
        const int n = energyMembers.size();
        QMap<qint64, QVector<double>> tsMap;
        for (int i = 0; i < n; ++i) {
            for (const auto &pt : energyMembers.at(i).powerHistory) {
                qint64 ts = pt.first.toMSecsSinceEpoch();
                if (!tsMap.contains(ts))
                    tsMap[ts] = QVector<double>(n, 0.0);
                tsMap[ts][i] = pt.second;
            }
        }
        QList<qint64> timestamps = tsMap.keys();

        double yMax = 0.0;
        for (int i = 0; i < n; ++i) {
            QColor c = kPalette[i % paletteSize];
            QLineSeries *upper = new QLineSeries();
            QLineSeries *lower = new QLineSeries();

            for (qint64 ts : timestamps) {
                const QVector<double> &vals = tsMap[ts];
                double lowerVal = 0.0;
                for (int j = 0; j < i; ++j) lowerVal += vals[j];
                double upperVal = lowerVal + vals[i];
                upper->append(ts, upperVal);
                lower->append(ts, lowerVal);
                if (upperVal > yMax) yMax = upperVal;
            }

            QAreaSeries *area = new QAreaSeries(upper, lower);
            QColor fill = c;
            fill.setAlpha(120);
            area->setBrush(QBrush(fill));
            QPen pen(c);
            pen.setWidth(1);
            area->setPen(pen);
            area->setName(energyMembers.at(i).name);
            chart->addSeries(area);
            area->attachAxis(axisX);
            area->attachAxis(axisY);

            // Store pointers for in-place rolling updates
            m_powerStackedUpper.append(upper);
            m_powerStackedLower.append(lower);
        }

        // Store energy members for rolling update
        m_memberDevices = energyMembers;

        if (yMax > 0) {
            applyAxisRange(axisY, roundAxisRange(0.0, yMax));
        } else {
            axisY->setRange(0, 10.0);
        }

        chart->legend()->show();

        // Format current power for the overlay label (group total)
        QString currentText;
        if (dev.energyStats.valid)
            currentText = QString::number(dev.energyStats.power, 'f', 1) + " W";

        m_powerAxisX = axisX;
        m_powerAxisY = axisY;
        // m_powerSeries / m_powerLowerSeries stay nullptr in stacked mode
        QComboBox *tmpWindowCombo = new QComboBox();
        for (int i = 0; i < 9; ++i) tmpWindowCombo->addItem(i18n("Last %1", kWindowLabels[i]));
        tmpWindowCombo->setCurrentIndex(qBound(0, m_windowComboIndex, 8));
        connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int idx){ m_windowComboIndex = idx; onWindowComboChanged(idx); saveChartState(); });
        m_tabs->addTab(makeChartTab(chart, currentText, &m_powerValueLabel, m_powerScrollBar, m_powerLockCheckBox, tmpWindowCombo), i18n("Power"));

    } else {
        // Single-device (or group with <2 energy members): original area chart
        QSplineSeries *series = new QSplineSeries();
        series->setName(i18n("Power (W)"));

        for (const auto &point : dev.powerHistory) {
            series->append(point.first.toMSecsSinceEpoch(), point.second);
        }

        if (dev.powerHistory.isEmpty() && dev.energyStats.valid) {
            series->append(QDateTime::currentDateTime().toMSecsSinceEpoch(), dev.energyStats.power);
        }

        // Shaded area under the curve
        QLineSeries *lower = new QLineSeries();
        if (!dev.powerHistory.isEmpty()) {
            lower->append(dev.powerHistory.first().first.toMSecsSinceEpoch(), 0);
            lower->append(dev.powerHistory.last().first.toMSecsSinceEpoch(), 0);
        } else {
            qint64 now = QDateTime::currentDateTime().toMSecsSinceEpoch();
            lower->append(now - 3600000, 0);
            lower->append(now, 0);
        }

        QAreaSeries *area = new QAreaSeries(series, lower);
        QColor fillColor(0, 120, 215, 80);
        area->setBrush(QBrush(fillColor));
        QPen linePen(QColor(0, 120, 215));
        linePen.setWidth(2);
        area->setPen(linePen);

        chart->addSeries(area);
        area->attachAxis(axisX);
        area->attachAxis(axisY);

        if (!dev.powerHistory.isEmpty()) {
            double maxP = 0;
            for (const auto &p : dev.powerHistory) maxP = qMax(maxP, p.second);
            applyAxisRange(axisY, roundAxisRange(0.0, maxP));
        }

        // Store axis/series pointers for in-place updates
        m_powerAxisX       = axisX;
        m_powerAxisY       = axisY;
        m_powerSeries      = series;
        m_powerLowerSeries = lower;

        // Format current power for the overlay label
        QString currentText;
        if (dev.energyStats.valid)
            currentText = QString::number(dev.energyStats.power, 'f', 1) + " W";

        QComboBox *tmpWindowCombo = new QComboBox();
        for (int i = 0; i < 9; ++i) tmpWindowCombo->addItem(i18n("Last %1", kWindowLabels[i]));
        tmpWindowCombo->setCurrentIndex(qBound(0, m_windowComboIndex, 8));
        connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int idx){ m_windowComboIndex = idx; onWindowComboChanged(idx); saveChartState(); });
        m_tabs->addTab(makeChartTab(chart, currentText, &m_powerValueLabel, m_powerScrollBar, m_powerLockCheckBox, tmpWindowCombo), i18n("Power"));
    }
}

void ChartWidget::buildHumidityChart(const FritzDevice &dev)
{
    QLineSeries *series = new QLineSeries();
    series->setName(i18n("Humidity (%)"));
    QPen pen(QColor(0, 180, 120));
    pen.setWidth(2);
    series->setPen(pen);

    for (const auto &point : dev.humidityHistory) {
        series->append(point.first.toMSecsSinceEpoch(), point.second);
    }

    if (dev.humidityHistory.isEmpty() && dev.humidityStats.valid) {
        series->append(QDateTime::currentDateTime().toMSecsSinceEpoch(),
                       dev.humidityStats.humidity);
    }

    QChart *chart = makeBaseChart(i18n("Humidity History"));
    chart->addSeries(series);

    QDateTimeAxis *axisX = new QDateTimeAxis();
    configureTimeAxis(axisX, i18n("Time"));
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText(i18n("% RH"));
    axisY->setLabelFormat("%d");
    axisY->setRange(0, 100);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    // Store series pointer for in-place updates
    m_humiditySeries = series;

    m_tabs->addTab(makeChartView(chart), i18n("Humidity"));
}

void ChartWidget::buildEnergyGauge(const FritzDevice &dev,
                                    const FritzDeviceList &memberDevices)
{
    // Outer panel keeps the default (grey) tab background so that the
    // QTabWidget frame stays visible — matching Temperature / Power tabs.
    QWidget *panel = new QWidget();
    QVBoxLayout *outerVl = new QVBoxLayout(panel);

    // Inner content widget with a white background — labels and chart
    // live here, giving a consistent white area framed by the grey tab border.
    QWidget *inner = new QWidget();
    QPalette pal = inner->palette();
    pal.setColor(QPalette::Window, Qt::white);
    inner->setPalette(pal);
    inner->setAutoFillBackground(true);
    outerVl->addWidget(inner);

    QVBoxLayout *vl = new QVBoxLayout(inner);
    vl->setAlignment(Qt::AlignCenter);

    auto makeLabel = [](const QString &text, int ptSize, bool bold) -> QLabel * {
        QLabel *lbl = new QLabel(text);
        QFont f = lbl->font();
        f.setPointSize(ptSize);
        f.setBold(bold);
        lbl->setFont(f);
        lbl->setAlignment(Qt::AlignCenter);
        return lbl;
    };

    if (dev.energyStats.valid) {
        double kwh = dev.energyStats.energy / 1000.0;
        vl->addWidget(makeLabel(i18n("Total Energy Consumed"), 11, false));
        m_gaugeKwhLabel = makeLabel(QString::number(kwh, 'f', 3) + " kWh", 28, true);
        vl->addWidget(m_gaugeKwhLabel);
        vl->addSpacing(16);

        vl->addWidget(makeLabel(i18n("Current Power"), 11, false));
        m_gaugePowerLabel = makeLabel(QString::number(dev.energyStats.power, 'f', 1) + " W", 22, true);
        vl->addWidget(m_gaugePowerLabel);

        if (dev.energyStats.voltage > 0) {
            vl->addSpacing(16);
            vl->addWidget(makeLabel(i18n("Voltage"), 11, false));
            m_gaugeVoltageLabel = makeLabel(QString::number(dev.energyStats.voltage, 'f', 1) + " V", 18, false);
            vl->addWidget(m_gaugeVoltageLabel);
        }
    } else {
        vl->addWidget(makeLabel(i18n("No energy data available"), 12, false));
    }

    // If this is a group device, and we have per-member energy stats available
    // in the memberDevices parameter, show a pie chart of each member's total energy share
    // (Wh). Keep this lightweight: only build when at least one member reports
    // valid energyStats.
    if (dev.isGroup()) {
        // Build pie only if we have member energy stats in the memberDevices parameter.
        QVector<QPair<QString, double>> memberEnergies;
        for (const FritzDevice &mem : memberDevices) {
            if (mem.hasEnergyMeter() && mem.energyStats.valid) {
                const QString label = mem.name.isEmpty() ? mem.ain : mem.name;
                memberEnergies.append({ label, mem.energyStats.energy });
            }
        }
    if (!memberEnergies.isEmpty()) {
        // Build or update the pie series and view stored in members so we
        // can update it dynamically when new per-member stats arrive.
        QPieSeries *pie = nullptr;
        if (m_groupEnergyPie) {
            pie = m_groupEnergyPie;
            pie->clear();
            // Ensure existing slices are deleted to avoid stale tooltips
            // (QPieSeries::clear() handles this on most Qt versions but be safe)
            while (pie->slices().size() > 0) {
                pie->remove(pie->slices().first());
            }
        } else {
            pie = new QPieSeries();
            m_groupEnergyPie = pie;
        }

            static const QColor kPiePalette[] = {
                QColor(0,   120, 215),
                QColor(220, 80,  0  ),
                QColor(0,   153, 76 ),
                QColor(180, 0,   180),
                QColor(200, 160, 0  ),
                QColor(0,   180, 200),
                QColor(220, 50,  50 ),
                QColor(80,  80,  200),
            };
            const int paletteSize = static_cast<int>(sizeof(kPiePalette) / sizeof(kPiePalette[0]));

            int idx = 0;
            double total = 0.0;
            for (const auto &p : memberEnergies) total += p.second;
            for (const auto &p : memberEnergies) {
                QPieSlice *slice = pie->append(p.first, p.second);
                QColor c = kPiePalette[idx % paletteSize];
                c.setAlpha(200);
                slice->setColor(c);
                // Store raw member name for later label/tooltip composition.
                slice->setProperty("memberName", p.first);
                ++idx;
            }

            // Set explode distance on every (re)build — new slices after
            // pie->clear() get the default factor (0.05) unless we re-apply.
            // Use a moderate factor (0.10) so the explode is visible but the
            // displaced slice (and its label) stays within the chart area.
            // Also shorten the label arm so that labels stay close to the
            // pie rim and are less likely to be clipped at the widget edge.
            for (QPieSlice *s : pie->slices()) {
                s->setExploded(false);
                s->setExplodeDistanceFactor(0.10);
                s->setLabelArmLengthFactor(0.05);
            }

            if (!m_groupEnergyPieView) {
                QChart *pieChart = makeBaseChart(i18n("Member Energy"));
                pieChart->addSeries(pie);
                // Hide legend because slice labels contain full info.
                pieChart->legend()->hide();
                pieChart->setTitle(i18n("Member Energy Distribution"));
                // Add generous margins so that labels of exploded slices
                // are not clipped at the widget boundary.  The pie circle
                // is inscribed in the plotArea, so wider margins shrink
                // the circle and leave room for the label text outside it.
                pieChart->setMargins(QMargins(10, 10, 10, 10));
                m_groupEnergyPieView = makeChartView(pieChart);
                m_groupEnergyPieView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
                // Tall enough that the pie stays usable despite the margins.
                m_groupEnergyPieView->setMinimumHeight(280);
                if (m_groupEnergyPie && !m_groupPieTooltipFilter) {
                    PieTooltipFilter *f = new PieTooltipFilter(m_groupEnergyPie, m_groupEnergyPieView->viewport());
                    m_groupEnergyPieView->viewport()->installEventFilter(f);
                    m_groupPieTooltipFilter = f;
                }
                // Ensure mouse tracking so hovered signals are delivered without mouse press
                m_groupEnergyPieView->setMouseTracking(true);
                if (m_groupEnergyPieView->viewport())
                    m_groupEnergyPieView->viewport()->setMouseTracking(true);

                // Place the pie directly under the "Total Energy Consumed" widget
                // by inserting it immediately after the KWh label.
                int insertPos = vl->indexOf(m_gaugeKwhLabel);
                if (insertPos >= 0)
                    vl->insertWidget(insertPos + 1, m_groupEnergyPieView);
                else
                    vl->addWidget(m_groupEnergyPieView);
            }

            // Update labels on every (re)build — not just first creation —
            // so that newly appended slices get proper label text and the
            // overlap-aware visibility logic runs.
            updateGroupPieLabels();
        }
    }

    m_tabs->addTab(panel, i18n("Energy"));
}

// ---------------------------------------------------------------------------
// Energy History — placeholder when selected view has no data yet
// ---------------------------------------------------------------------------

void ChartWidget::buildEnergyHistoryPlaceholder(const QStringList &viewLabels,
                                                 int selectedIdx)
{
    QLabel *noDataMsg = new QLabel(
        i18n("No energy data available for %1 yet.\nData will appear as it becomes available from Fritz!Box.",
             viewLabels.value(selectedIdx)), this);
    noDataMsg->setAlignment(Qt::AlignCenter);
    noDataMsg->setWordWrap(true);

    QComboBox *combo = new QComboBox();
    for (const QString &label : viewLabels)
        combo->addItem(label);
    combo->setCurrentIndex(selectedIdx);

    QWidget *comboOverlay = new QWidget();
    comboOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    comboOverlay->setMouseTracking(true);
    comboOverlay->setStyleSheet("background: transparent;");
    {
        QVBoxLayout *vl = new QVBoxLayout(comboOverlay);
        vl->setContentsMargins(6, 4, 0, 0);
        vl->setSpacing(0);
        QHBoxLayout *hl = new QHBoxLayout();
        hl->setSpacing(4);
        QLabel *viewLabel = new QLabel(i18n("View:"));
        viewLabel->setStyleSheet("background: transparent;");
        hl->addWidget(viewLabel);
        combo->setStyleSheet("QComboBox { background: palette(button); }");
        hl->addWidget(combo);
        hl->addStretch();
        vl->addLayout(hl);
        vl->addStretch();
    }

    QWidget *container = new QWidget();
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QStackedLayout *stack = new QStackedLayout(container);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->addWidget(noDataMsg);
    stack->addWidget(comboOverlay);
    comboOverlay->raise();

    m_energyHistoryTabIndex = m_tabs->insertTab(
        m_energyHistoryTabIndex >= 0 ? m_energyHistoryTabIndex : m_tabs->count(),
        container, i18n("Energy History"));

    m_energyResCombo = combo;
    m_energyChartView = nullptr;
    // Reset m_activeEnergyGrid to 0 so the skip-rebuild guards in
    // updateEnergyStats() / updateGroupEnergyStats() always trigger a full
    // rebuild when new data arrives while a placeholder is showing.
    m_activeEnergyGrid = 0;

    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChartWidget::onEnergyResolutionChanged);
}

// ---------------------------------------------------------------------------
// Energy History chart (getbasicdevicestats)
// ---------------------------------------------------------------------------

void ChartWidget::buildEnergyHistoryChart(const DeviceBasicStats &stats)
{
    // The Fritz!Box REST API provides three energy series:
    //   grid=900      (15-min, 96 values, 24 h)     newest-first
    //   grid=86400    (daily,  31 values, 1 month)   newest-first
    //   grid=2678400  (monthly, ~24 values, 2 years) newest-first
    //
    // Named views:
    //   "Last 24 hours"  → grid=900 series (15-min buckets)
    //   "Rolling month"  → last (prev-month's same day + 1) through today,
    //                       with a month-separator bar at the month boundary
    //   "Last 2 years"   → up to 24 bars from the monthly series

    const StatSeries *hourlySeries  = nullptr;  // grid=900 (15-min, 24 h)
    const StatSeries *dailySeries   = nullptr;
    const StatSeries *monthlySeries = nullptr;
    for (const StatSeries &s : stats.energy) {
        if (s.grid == 900 && !s.values.isEmpty())
            hourlySeries = &s;
        else if (s.grid == 86400 && !s.values.isEmpty())
            dailySeries = &s;
        else if (s.grid == 2678400 && !s.values.isEmpty())
            monthlySeries = &s;
    }

    if (!hourlySeries && !dailySeries && !monthlySeries) {
        // Show a message explaining *why* energy history is unavailable.
        // The Fritz!Box REST API sets a "statisticsState" per series entry
        // (e.g. "unknown", "notConnected") when data is not available.
        QString reason;
        if (stats.energyStatsState == QLatin1String("notConnected"))
            reason = i18n("Device is not connected — statistics are unavailable.");
        else if (stats.energyStatsState == QLatin1String("unknown"))
            reason = i18n("Statistics for this device are not yet available.\n"
                          "The Fritz!Box may still be collecting data.");
        else if (!stats.energyStatsState.isEmpty())
            reason = i18n("Fritz!Box reported energy statistics state: %1", stats.energyStatsState);
        else
            reason = i18n("No energy history available from Fritz!Box.");

        QLabel *noData = new QLabel(reason, this);
        noData->setAlignment(Qt::AlignCenter);
        noData->setWordWrap(true);
        m_energyHistoryTabIndex = m_tabs->insertTab(
            m_energyHistoryTabIndex >= 0 ? m_energyHistoryTabIndex : m_tabs->count(),
            noData, i18n("Energy History"));
        m_activeEnergyGrid = 0;
        return;
    }

    // Build the list of available views dynamically.
    // "Last 24 hours" is always included (even if no data yet), others only if data exists.
    struct ViewDef {
        const char       *label;
        int               bars;
        const StatSeries *series;  // which series to read from (nullptr if no data)
        int               grid;    // seconds per bar
    };

    QVector<ViewDef> views;
    // Always include "Last 24 hours" even if no data (will show placeholder)
    views.append({ QT_TR_NOOP("Last 24 hours"), 96, hourlySeries, 900 });
    if (dailySeries)
        views.append({ QT_TR_NOOP("Rolling month"), 31, dailySeries, 86400 });
    if (monthlySeries)
        views.append({ QT_TR_NOOP("Last 2 years"), 24, monthlySeries, 2678400 });

    const int nViews = views.size();
    int selectedIdx = qBound(0, m_energyResSelectedIdx, nViews - 1);
    const ViewDef &view = views[selectedIdx];

    // If the selected view has no data, show a placeholder message
    if (!view.series) {
        QStringList labels;
        for (int i = 0; i < nViews; ++i)
            labels << i18n(views[i].label);
        buildEnergyHistoryPlaceholder(labels, selectedIdx);
        return;
    }

    // ---- Build bar labels and values ----
    // series->values are newest-first; bar 0 = most recent completed period.
    // Snap fetchTime down to the last completed grid boundary.
    qint64 fetchSecs      = stats.fetchTime.toSecsSinceEpoch();
    qint64 newestBoundary = (fetchSecs / view.grid) * view.grid;

    // For the daily view: show from (previous-month's same day + 1) through today.
    // Example: today = 22 Mar → window is 23 Feb … 22 Mar (= daysInFeb = 28 bars).
    // A zero-value separator bar labelled with the current month abbreviation is
    // inserted between the last day of the previous month and day 1 of this month.
    // This keeps day-number labels unique (no cross-month duplicates) while still
    // filling the left portion with previous-month data.
    int barsToShow = view.bars;  // overridden below for daily view
    QStringList    categories;
    QStringList    tooltipLabels;
    QList<double>  barValues;
    QVector<int>   sepCatIndices;  // category indices where a separator line is drawn (grid==900 only)
    QStringList    sepHourLabels;  // hour text for each separator (overlay labels, grid==900 only)
    // For the two-row axis overlay (grid==86400 and grid==2678400):
    //   grid==86400:   monthBarLabels = day number per bar,  yearBarLabels = "MMM" per bar
    //   grid==2678400: monthBarLabels = "MMM" per bar,       yearBarLabels = "yyyy" per bar
    QStringList    monthBarLabels; // per-bar label for overlay row 1 (upper)
    QStringList    yearBarLabels;  // per-bar label for overlay row 2 grouping (lower)

    if (view.grid == 900) {
        // 15-minute bars, oldest left → newest right.
        // values[0] = most recent completed 15-min slot, values[95] = 24 h ago.
        // QBarCategoryAxis silently drops duplicate strings, so every non-label
        // bar gets a unique string of spaces (renders visually blank).
        // Axis labels are hidden for grid==900; hour labels are drawn as
        // QGraphicsTextItem overlays aligned to each separator line instead.
        barsToShow = qMin(barsToShow, view.series->values.size());
        int blankIdx = 0;
        for (int bar = barsToShow - 1; bar >= 0; --bar) {
            qint64 slotSecs = newestBoundary - static_cast<qint64>(bar) * view.grid;
            QDateTime slotDt = QDateTime::fromSecsSinceEpoch(slotSecs);
            // At each exact hour boundary (except the very first bar) record the
            // current category index so we can draw a separator line here later.
            const bool isHourBoundary = (slotDt.time().minute() == 0);
            if (isHourBoundary && bar < barsToShow - 1) {
                sepCatIndices << categories.size();
                sepHourLabels << slotDt.toString("H");
            }
            categories    << QString(++blankIdx, QLatin1Char(' '));
            tooltipLabels << slotDt.toString("dd.MM.yy HH:mm");
            double v = 0.0;
            if (bar < view.series->values.size()) {
                double raw = view.series->values.at(bar);
                if (!std::isnan(raw)) v = raw;
            }
            barValues << v;
        }
    } else if (view.grid == 86400) {
        QDateTime newestDt   = QDateTime::fromSecsSinceEpoch(newestBoundary);
        QDate     today      = newestDt.date();
        int       dayOfMonth = today.day();          // 1-31

        // Days from previous month that fit in the window:
        //   start = (same day + 1) in previous month
        //   end   = last day of previous month
        // = daysInPrevMonth - dayOfMonth  bars  (0 if today is the 1st)
        QDate prevMonthDate  = today.addMonths(-1);
        int   daysInPrev     = prevMonthDate.daysInMonth();
        int   prevMonthBars  = qMax(0, daysInPrev - dayOfMonth);   // bars from prev month
        int   curMonthBars   = dayOfMonth;                // bars from current month

        barsToShow = prevMonthBars + curMonthBars;

        // Fill in chronological order (oldest left → newest right).
        // No separator bar — the two-row overlay (day numbers + month name)
        // makes the month boundary clear without wasting a slot.
        //
        // Category strings use "d MMM" so each bar has a unique key
        // (day numbers alone repeat across months; QBarCategoryAxis deduplicates).
        // The built-in axis labels are hidden; the overlay shows day numbers
        // (row 1) and month abbreviations (row 2, one per contiguous month span).

        // Previous-month bars: day (dayOfMonth+1) .. daysInPrev
        for (int d = dayOfMonth + 1; d <= daysInPrev; ++d) {
            int daysAgo = curMonthBars + (daysInPrev - d);
            double v = 0.0;
            if (daysAgo < view.series->values.size()) {
                double raw = view.series->values.at(daysAgo);
                if (!std::isnan(raw)) v = raw;
            }
            QDate slotDate(prevMonthDate.year(), prevMonthDate.month(), d);
            categories    << slotDate.toString("d MMM");
            tooltipLabels << slotDate.toString("dd.MM.yy");
            barValues     << v;
            monthBarLabels << QString::number(d);
            yearBarLabels  << monthAbbr(slotDate.month());
        }

        // Current-month bars: day 1 .. dayOfMonth
        for (int d = 1; d <= dayOfMonth; ++d) {
            int daysAgo = dayOfMonth - d;
            double v = 0.0;
            if (daysAgo < view.series->values.size()) {
                double raw = view.series->values.at(daysAgo);
                if (!std::isnan(raw)) v = raw;
            }
            QDate slotDate(today.year(), today.month(), d);
            categories    << slotDate.toString("d MMM");
            tooltipLabels << slotDate.toString("dd.MM.yy");
            barValues     << v;
            monthBarLabels << QString::number(d);
            yearBarLabels  << monthAbbr(slotDate.month());
        }

    } else {
        // Monthly view: use "MMM yy" labels so each of the 24 bars has a unique
        // category string (month abbreviations alone repeat across 2 years and
        // QBarCategoryAxis silently deduplicates identical strings).
        // monthBarLabels / yearBarLabels are used for the two-row overlay below;
        // the axis labels themselves will be hidden.
        //
        // Derive month labels from calendar arithmetic (addMonths) rather than
        // subtracting grid*bar seconds, because 2678400 s = 31 days does not
        // match every month length and the drift accumulates over 24 bars.
        // Use fetchTime's calendar month (not newestBoundary, which is snapped
        // to a 31-day grid that can fall in the previous month).
        QDate fetchDate = stats.fetchTime.date();
        QDate newestMonth(fetchDate.year(), fetchDate.month(), 1);
        for (int bar = 0; bar < barsToShow; ++bar) {
            double v = 0.0;
            if (bar < view.series->values.size()) {
                double raw = view.series->values.at(bar);
                if (!std::isnan(raw))
                    v = raw;
            }
            QDate barMonth = newestMonth.addMonths(-bar);
            QString mon = monthAbbr(barMonth.month());
            categories.prepend(mon + QLatin1Char(' ') + barMonth.toString("yy"));
            tooltipLabels.prepend(mon + QLatin1Char(' ') + barMonth.toString("yyyy"));
            barValues.prepend(v);
            monthBarLabels.prepend(mon);
            yearBarLabels.prepend(barMonth.toString("yyyy"));
        }
    }

    // ---- Decide Y-axis unit (Wh vs kWh) ----
    double rawTotal = 0.0;
    for (double v : barValues) rawTotal += v;
    double maxVal = 0.0;
    for (double v : barValues) maxVal = qMax(maxVal, v);

    // All values are zero or NaN → show an informative placeholder instead of
    // rendering invisible zero-height bars (which looks like an empty/broken chart).
    if (rawTotal == 0.0) {
        QStringList labels;
        for (int i = 0; i < nViews; ++i)
            labels << i18n(views[i].label);
        buildEnergyHistoryPlaceholder(labels, selectedIdx);
        return;
    }

    // For the 15-minute view, individual slot values are small (typically < 100 Wh)
    // so always use Wh to avoid Y-axis labels collapsing to "0.0 kWh".
    // For daily/monthly views, auto-switch to kWh when total >= 1000 Wh.
    const bool   useKwh = (view.grid != 900) && (rawTotal >= 1000.0);
    const double scale  = useKwh ? 0.001 : 1.0;

    // ---- Build bar chart ----
    // Single QBarSet / QBarSeries — full-width bars, no slot-splitting.
    // On Qt6: the last bar (most recent, still-accumulating period) is marked
    // via selectBar() so it renders in a distinct half-opacity colour.
    // On Qt5: selectBar() is not available; the last bar looks identical to
    // the others (no visual distinction, but no layout artefacts either).
    // The newest (still-accumulating) bar is always at categories.size()-1 —
    // the rightmost position.  There is no padding bar.
    const int lastBar = categories.size() - 1;

    QBarSet *barSet = new QBarSet(useKwh ? i18n("kWh") : i18n("Wh"));
    barSet->setColor(QColor(0, 100, 200, 200));
    for (double v : barValues)
        *barSet << v * scale;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Half-opacity tint for the still-accumulating bar; selectBar() is Qt6-only.
    barSet->setSelectedColor(QColor(0, 100, 200, 80));
    barSet->selectBar(lastBar);
#else
    Q_UNUSED(lastBar)
#endif

    QBarSeries *barSeries = new QBarSeries();
    barSeries->append(barSet);
    barSeries->setBarWidth(0.8);

    QChart *chart = makeBaseChart(i18n("Energy History"));
    chart->addSeries(barSeries);

    QBarCategoryAxis *axisX = new QBarCategoryAxis();
    axisX->append(categories);
    if (view.grid == 900) {
        // Axis labels are replaced by QGraphicsTextItem overlays; hide them
        // so the bottom margin is reclaimed.
        axisX->setLabelsVisible(false);
        // Hide the per-category grid lines — we draw our own at hour boundaries.
        axisX->setGridLineVisible(false);
    }
    chart->addAxis(axisX, Qt::AlignBottom);
    barSeries->attachAxis(axisX);

    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText(useKwh ? i18n("kWh") : i18n("Wh"));
    axisY->setLabelFormat("%.1f");
    applyAxisRange(axisY, roundAxisRange(0.0, maxVal * scale));
    chart->addAxis(axisY, Qt::AlignLeft);
    barSeries->attachAxis(axisY);

    // ---- Two-row axis overlay (grid==86400 "Rolling month", grid==2678400 "Last 2 years") ----
    // Hide the built-in axis labels and replace them with two rows of
    // QGraphicsTextItem overlays:
    //   grid==86400:   Row 1 = day number per bar,         Row 2 = month name per span
    //   grid==2678400: Row 1 = month abbreviation per bar, Row 2 = year number per span
    if ((view.grid == 86400 || view.grid == 2678400) && !monthBarLabels.isEmpty()) {
        axisX->setLabelsVisible(false);
        // Increase the chart's bottom margin so both overlay rows are visible.
        // Default Qt bottom margin (~7 px) only fits the axis ticks; two text rows
        // need roughly 36 px.
        QMargins m = chart->margins();
        m.setBottom(36);
        chart->setMargins(m);

        const int nBars = monthBarLabels.size();

        // --- Month labels (one per bar) ---
        auto *monthLbls = new QVector<QGraphicsTextItem *>();
        for (int i = 0; i < nBars; ++i) {
            auto *lbl = new QGraphicsTextItem(monthBarLabels.at(i), chart);
            lbl->setZValue(11);
            lbl->setDefaultTextColor(QColor(80, 80, 80));
            QFont f = lbl->font();
            f.setPointSize(qMax(6, f.pointSize() - 1));
            lbl->setFont(f);
            monthLbls->append(lbl);
        }

        // --- Row 2 labels (one per contiguous span of identical yearBarLabels values) ---
        // For grid==2678400: year numbers.  For grid==86400: month abbreviations.
        struct YearSpan { QString year; int first; int last; };
        auto *yearSpans = new QVector<YearSpan>();
        for (int i = 0; i < nBars; ++i) {
            const QString &y = yearBarLabels.at(i);
            if (!yearSpans->isEmpty() && yearSpans->last().year == y)
                yearSpans->last().last = i;
            else
                yearSpans->append({y, i, i});
        }
        auto *yearLbls = new QVector<QGraphicsTextItem *>();
        for (const YearSpan &ys : *yearSpans) {
            auto *lbl = new QGraphicsTextItem(ys.year, chart);
            lbl->setZValue(11);
            lbl->setDefaultTextColor(QColor(60, 60, 60));
            QFont f = lbl->font();
            f.setPointSize(qMax(6, f.pointSize() - 1));
            f.setBold(true);
            lbl->setFont(f);
            yearLbls->append(lbl);
        }

        auto updateMonthOverlay = [chart, barSeries, monthLbls, yearLbls, yearSpans,
                                   nBars]() {
            const QRectF pa = chart->plotArea();
            if (nBars < 1) return;

            // Compute slot width from the center positions of bar 0 and bar 1.
            const double cx0 = chart->mapToPosition(QPointF(0, 0), barSeries).x();
            const double cx1 = (nBars > 1)
                ? chart->mapToPosition(QPointF(1, 0), barSeries).x()
                : cx0 + pa.width();
            const double slotW = cx1 - cx0;

            // Divide the bottom margin into three equal gaps so the spacing
            // between the x-axis ticks, month row, year row, and widget frame
            // is uniform.  axisOverhang accounts for the tick marks that extend
            // below pa.bottom(); the remaining space is split into three gaps.
            const double monthH = monthLbls->isEmpty()
                ? 16.0 : monthLbls->first()->boundingRect().height();
            const double yearH  = yearLbls->isEmpty()
                ? 16.0 : yearLbls->first()->boundingRect().height();
            constexpr double kAxisOverhang = 6.0;  // px below pa.bottom() used by axis ticks
            const double gap    = (36.0 - kAxisOverhang - monthH - yearH) / 3.0;
            const double monthY = pa.bottom() + kAxisOverhang + gap;
            const double yearY  = monthY + monthH + gap;

            // Row 1: month abbreviations.
            for (int i = 0; i < monthLbls->size(); ++i) {
                QGraphicsTextItem *lbl = monthLbls->at(i);
                const double cx = chart->mapToPosition(QPointF(i, 0), barSeries).x();
                const double lw = lbl->boundingRect().width();
                lbl->setPos(cx - lw * 0.5, monthY);
            }

            // Row 2: year labels, each centered over its span of bars.
            for (int j = 0; j < yearLbls->size(); ++j) {
                const YearSpan &ys = yearSpans->at(j);
                const double xLeft  = chart->mapToPosition(
                    QPointF(ys.first, 0), barSeries).x() - slotW * 0.5;
                const double xRight = chart->mapToPosition(
                    QPointF(ys.last,  0), barSeries).x() + slotW * 0.5;
                QGraphicsTextItem *lbl = yearLbls->at(j);
                const double lw = lbl->boundingRect().width();
                lbl->setPos((xLeft + xRight) * 0.5 - lw * 0.5, yearY);
            }
        };
        updateMonthOverlay();
        QObject::connect(chart, &QChart::plotAreaChanged, chart,
                         [updateMonthOverlay](const QRectF &) { updateMonthOverlay(); });
    }

    // ---- Hour separator lines (grid==900 only) ----
    // Draw a thin vertical line at each hour boundary.  The lines are
    // QGraphicsLineItem objects owned by the chart scene; they are repositioned
    // whenever the plot area changes (e.g. on resize).
    if (view.grid == 900 && !sepCatIndices.isEmpty()) {
        auto *sepLines  = new QVector<QGraphicsLineItem *>();
        auto *sepLabels = new QVector<QGraphicsTextItem *>();
        for (int i = 0; i < sepCatIndices.size(); ++i) {
            auto *line = new QGraphicsLineItem(chart);
            QPen pen(QColor(160, 160, 160, 180));
            pen.setWidth(1);
            line->setPen(pen);
            line->setZValue(10);
            sepLines->append(line);

            auto *lbl = new QGraphicsTextItem(sepHourLabels.at(i), chart);
            lbl->setZValue(11);
            lbl->setDefaultTextColor(QColor(80, 80, 80));
            QFont lblFont = lbl->font();
            lblFont.setPointSize(qMax(6, lblFont.pointSize() - 1));
            lbl->setFont(lblFont);
            sepLabels->append(lbl);
        }
        // Use chart->mapToPosition() to get the exact pixel center of each
        // :00 slot, then step back half a slot width to land on the boundary
        // between the previous hour's last bar and this hour's first bar.
        auto updateSepLines = [chart, barSeries, sepLines, sepLabels, sepCatIndices]() {
            const QRectF pa = chart->plotArea();
            // mapToPosition gives the center of slot idx in scene coordinates.
            // Half a slot width = distance between adjacent slot centers / 2.
            // We use two adjacent slots to compute the actual slot width so
            // the result is correct even if Qt adds edge padding.
            const double slotW = (sepCatIndices.size() >= 1 && sepCatIndices.at(0) > 0)
                ? chart->mapToPosition(QPointF(sepCatIndices.at(0),     0), barSeries).x()
                - chart->mapToPosition(QPointF(sepCatIndices.at(0) - 1, 0), barSeries).x()
                : pa.width();   // fallback (should never trigger)
            for (int i = 0; i < sepLines->size(); ++i) {
                const double cx = chart->mapToPosition(
                                      QPointF(sepCatIndices.at(i), 0), barSeries).x();
                const double x = cx - slotW * 0.5;
                sepLines->at(i)->setLine(x, pa.top(), x, pa.bottom());

                // Position label just below the plot area, horizontally centered on x.
                QGraphicsTextItem *lbl = sepLabels->at(i);
                const double lw = lbl->boundingRect().width();
                lbl->setPos(x - lw * 0.5, pa.bottom() + 3);
            }
        };
        updateSepLines();
        QObject::connect(chart, &QChart::plotAreaChanged, chart,
                         [updateSepLines](const QRectF &) { updateSepLines(); });
    }

    // ---- Wrap chart in a stacked container with overlaid controls ----
    // The view selector (top-left) and total energy value (top-right) are
    // overlaid directly on the chart using QStackedLayout::StackAll so the
    // chart fills the entire tab area without a separate header row.
    QChartView *chartView = makeChartView(chart);
    chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Combo (view selector) — top-left overlay
    QComboBox *combo = new QComboBox();
    for (int i = 0; i < nViews; ++i)
        combo->addItem(i18n(views[i].label));
    QWidget *comboOverlay = new QWidget();
    comboOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    comboOverlay->setMouseTracking(true);          // receive MouseMove without button press
    comboOverlay->setStyleSheet("background: transparent;");
    {
        QVBoxLayout *vl = new QVBoxLayout(comboOverlay);
        vl->setContentsMargins(6, 4, 0, 0);
        vl->setSpacing(0);
        QHBoxLayout *hl = new QHBoxLayout();
        hl->setSpacing(4);
        QLabel *viewLabel = new QLabel(i18n("View:"));
        viewLabel->setStyleSheet("background: transparent;");
        hl->addWidget(viewLabel);
        combo->setStyleSheet("QComboBox { background: palette(button); }");
        hl->addWidget(combo);
        hl->addStretch();
        vl->addLayout(hl);
        vl->addStretch();
    }

    // Window total label — top-right overlay
    QString totalText = useKwh
        ? QString("%1 kWh").arg(rawTotal / 1000.0, 0, 'f', 2)
        : QString("%1 Wh").arg(rawTotal, 0, 'f', 1);
    QLabel *totalLabel = new QLabel(totalText);
    {
        QFont f = totalLabel->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 2);
        totalLabel->setFont(f);
    }
    totalLabel->setStyleSheet("background: transparent;");
    QWidget *totalOverlay = new QWidget();
    totalOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    totalOverlay->setStyleSheet("background: transparent;");
    {
        QVBoxLayout *vl = new QVBoxLayout(totalOverlay);
        vl->setContentsMargins(0, 4, 10, 0);
        vl->setSpacing(0);
        QHBoxLayout *hl = new QHBoxLayout();
        hl->addStretch();
        hl->addWidget(totalLabel);
        vl->addLayout(hl);
        vl->addStretch();
    }

    QWidget *container = new QWidget();
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QStackedLayout *stack = new QStackedLayout(container);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->addWidget(chartView);
    stack->addWidget(comboOverlay);
    stack->addWidget(totalOverlay);
    comboOverlay->raise();
    totalOverlay->raise();

    // No pie mode combo — labels always show both absolute and percent values.

    // ---- Hover tooltip on each bar ----
    // We use the QHelpEvent mechanism (eventFilter below) to show the tooltip
    // when the mouse first enters a bar.  When moving between bars, Qt Charts
    // may fire hovered(true, newIndex) without an intervening hovered(false) so
    // no QHelpEvent fires and we must update the popup ourselves.
    //
    // Qt5 QToolTip reuses the same popup label and calls setText() on it, which
    // scrambles the window geometry when the text width changes.  The fix is to
    // call showText(pos, "") first — in Qt5 this destroys the popup synchronously
    // — then immediately call showText(pos, newTip) to create a fresh window.
    // In Qt6 the empty-string call is also synchronous so this is safe on both.
    //
    // barValues holds the unscaled values; read from it rather than QBarSet
    // so the tooltip works identically on Qt5 and Qt6.
    auto onBarHovered = [this, tooltipLabels, barValues, useKwh, chartView](bool status, int index) {
        if (!status) {
            m_energyBarTooltip.clear();
            QToolTip::hideText();
            return;
        }
        if (index < 0 || index >= tooltipLabels.size()) return;
        // Separator bar has an empty tooltip label — skip it
        if (tooltipLabels.at(index).isEmpty()) {
            m_energyBarTooltip.clear();
            QToolTip::hideText();
            return;
        }
        const QString unit = useKwh ? QStringLiteral("kWh") : QStringLiteral("Wh");
        const double  val  = barValues.at(index) * (useKwh ? 0.001 : 1.0);
        const QString newTip = QString("%1\n%2 %3")
                               .arg(tooltipLabels.at(index))
                               .arg(val, 0, 'f', useKwh ? 3 : 1)
                               .arg(unit);
        const bool changed = (newTip != m_energyBarTooltip);
        m_energyBarTooltip = newTip;
        if (changed && QToolTip::isVisible()) {
            // Destroy the existing popup by showing empty text, then immediately
            // show the new text — creates a clean new window at the right size.
            QToolTip::showText(QCursor::pos(), QString(), chartView);
            QToolTip::showText(QCursor::pos(), newTip,   chartView);
        }
    };
    connect(barSet, &QBarSet::hovered, this, onBarHovered);
    // The comboOverlay widget is stacked above chartView (StackAll) and has
    // WA_TransparentForMouseEvents=false so it receives QHelpEvent (tooltip)
    // when the cursor is over the chart area.  Install the filter on both so
    // the tooltip fires regardless of which widget the cursor is over.
    // Mouse move events from comboOverlay are forwarded to the chart viewport
    // by the event filter so that QBarSet::hovered signals still fire.
    chartView->viewport()->installEventFilter(this);
    comboOverlay->installEventFilter(this);

    m_energyChartView = chartView;
    m_energyResCombo = combo;
    combo->blockSignals(true);
    combo->setCurrentIndex(selectedIdx);
    combo->blockSignals(false);
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChartWidget::onEnergyResolutionChanged);

    m_activeEnergyGrid = view.grid;

    int insertAt = (m_energyHistoryTabIndex >= 0
                    && m_energyHistoryTabIndex <= m_tabs->count())
                   ? m_energyHistoryTabIndex
                   : m_tabs->count();
    m_energyHistoryTabIndex = m_tabs->insertTab(insertAt, container, i18n("Energy History"));
}

// ---------------------------------------------------------------------------
// Energy History chart — stacked (group mode, one QBarSet per member)
// ---------------------------------------------------------------------------

void ChartWidget::buildEnergyHistoryChartStacked(
    const QList<QPair<QString, DeviceBasicStats>> &memberStats)
{
    // Same three resolution views as buildEnergyHistoryChart.
    // We pick the *first* member that has data for each grid to drive category
    // building.  All members must report the same fetchTime (they all come from
    // the same Fritz!Box poll); we use the first valid one.

    // Gather per-grid series pointers for every member.
    // memberSeriesForGrid[gridIdx][memberIdx] = pointer into that member's stats
    // (nullptr if the member has no data for that grid).
    struct MemberSeriesSet {
        const StatSeries *s900     = nullptr;  // grid=900  (15-min, 24 h)
        const StatSeries *s86400   = nullptr;  // grid=86400 (daily, 1 month)
        const StatSeries *s2678400 = nullptr;  // grid=2678400 (monthly, 2 years)
    };

    QVector<MemberSeriesSet> memberSets;
    memberSets.resize(memberStats.size());
    QDateTime fetchTime;

    for (int m = 0; m < memberStats.size(); ++m) {
        const DeviceBasicStats &s = memberStats.at(m).second;
        if (!fetchTime.isValid() && s.fetchTime.isValid())
            fetchTime = s.fetchTime;
        for (const StatSeries &ss : s.energy) {
            if (ss.values.isEmpty()) continue;
            if      (ss.grid == 900)     memberSets[m].s900     = &ss;
            else if (ss.grid == 86400)   memberSets[m].s86400   = &ss;
            else if (ss.grid == 2678400) memberSets[m].s2678400 = &ss;
        }
    }

    if (!fetchTime.isValid())
        fetchTime = QDateTime::currentDateTime();

    // Any grid is "available" if at least one member has data for it.
    bool has900 = false, has86400 = false, has2678400 = false;
    for (const MemberSeriesSet &ms : memberSets) {
        if (ms.s900)     has900     = true;
        if (ms.s86400)   has86400   = true;
        if (ms.s2678400) has2678400 = true;
    }

    if (!has900 && !has86400 && !has2678400) {
        // Collect non-valid statisticsState values from all members to explain
        // why no energy data is available for the group.
        QStringList reasons;
        for (const auto &pair : memberStats) {
            const QString &state = pair.second.energyStatsState;
            if (state.isEmpty())
                continue;
            QString msg;
            if (state == QLatin1String("notConnected"))
                msg = i18n("%1: device not connected", pair.first);
            else if (state == QLatin1String("unknown"))
                msg = i18n("%1: statistics not yet available", pair.first);
            else
                msg = i18n("%1: %2", pair.first, state);
            if (!reasons.contains(msg))
                reasons.append(msg);
        }

        QString text;
        if (reasons.isEmpty()) {
            text = i18n("No energy history available from Fritz!Box.");
        } else {
            text = i18n("No energy history available from Fritz!Box:") +
                   QStringLiteral("\n\n") + reasons.join(QStringLiteral("\n"));
        }

        QLabel *noData = new QLabel(text, this);
        noData->setAlignment(Qt::AlignCenter);
        noData->setWordWrap(true);
        m_energyHistoryTabIndex = m_tabs->insertTab(
            m_energyHistoryTabIndex >= 0 ? m_energyHistoryTabIndex : m_tabs->count(),
            noData, i18n("Energy History"));
        m_activeEnergyGrid = 0;
        return;
    }

    // ---- Build view list ----
    // "Last 24 hours" is always included (even if no data yet), others only if data exists.
    struct ViewDef {
        const char *label;
        int         bars;
        int         grid;
        bool        hasData;  // whether this view has data
    };
    QVector<ViewDef> views;
    views.append({ QT_TR_NOOP("Last 24 hours"), 96,  900, has900 });
    if (has86400)   views.append({ QT_TR_NOOP("Rolling month"),  31, 86400, true });
    if (has2678400) views.append({ QT_TR_NOOP("Last 2 years"),   24, 2678400, true });

    const int nViews     = views.size();
    int selectedIdx      = qBound(0, m_energyResSelectedIdx, nViews - 1);
    const ViewDef &view  = views[selectedIdx];

    // If the selected view has no data, show a placeholder message
    if (!view.hasData) {
        QStringList labels;
        for (int i = 0; i < nViews; ++i)
            labels << i18n(views[i].label);
        buildEnergyHistoryPlaceholder(labels, selectedIdx);
        return;
    }

    // ---- Build shared category / tooltip lists ----
    // Category / tooltip logic is identical to the single-device builder; the
    // values differ per member but the labels are the same for every member.
    qint64 fetchSecs      = fetchTime.toSecsSinceEpoch();
    qint64 newestBoundary = (fetchSecs / view.grid) * view.grid;

    int barsToShow = view.bars;
    QStringList   categories;
    QStringList   tooltipLabels;
    QVector<int>  sepCatIndices;
    QStringList   sepHourLabels;
    QStringList   monthBarLabels;
    QStringList   yearBarLabels;

    // Also build per-member value lists in parallel with category building.
    // memberBarValues[m][slot] — unscaled (Wh), chronological left→right.
    const int nMembers = memberStats.size();
    QVector<QList<double>> memberBarValues(nMembers);

    // Helper: get a slot value for a given member and slot index within the
    // current view (slot 0 = leftmost / oldest after reversal).
    // The series is newest-first; barIndex counts from the newest (0 = most recent).
    auto slotValue = [&](int memberIdx, int barIndex) -> double {
        const MemberSeriesSet &ms = memberSets[memberIdx];
        const StatSeries *ss = (view.grid == 900)     ? ms.s900
                             : (view.grid == 86400)   ? ms.s86400
                                                      : ms.s2678400;
        if (!ss || barIndex >= ss->values.size()) return 0.0;
        double raw = ss->values.at(barIndex);
        return std::isnan(raw) ? 0.0 : raw;
    };

    if (view.grid == 900) {
        // Determine actual bar count from any member that has data
        int maxVals = 0;
        for (const MemberSeriesSet &ms : memberSets)
            if (ms.s900) maxVals = qMax(maxVals, ms.s900->values.size());
        barsToShow = qMin(barsToShow, maxVals);
        int blankIdx = 0;
        for (int bar = barsToShow - 1; bar >= 0; --bar) {
            qint64 slotSecs = newestBoundary - static_cast<qint64>(bar) * view.grid;
            QDateTime slotDt = QDateTime::fromSecsSinceEpoch(slotSecs);
            const bool isHourBoundary = (slotDt.time().minute() == 0);
            if (isHourBoundary && bar < barsToShow - 1) {
                sepCatIndices << categories.size();
                sepHourLabels << slotDt.toString("H");
            }
            categories    << QString(++blankIdx, QLatin1Char(' '));
            tooltipLabels << slotDt.toString("dd.MM.yy HH:mm");
            for (int m = 0; m < nMembers; ++m)
                memberBarValues[m] << slotValue(m, bar);
        }
    } else if (view.grid == 86400) {
        QDateTime newestDt   = QDateTime::fromSecsSinceEpoch(newestBoundary);
        QDate     today      = newestDt.date();
        int       dayOfMonth = today.day();
        QDate     prevMonthDate  = today.addMonths(-1);
        int       daysInPrev     = prevMonthDate.daysInMonth();
        int       prevMonthBars  = qMax(0, daysInPrev - dayOfMonth);
        int       curMonthBars   = dayOfMonth;
        barsToShow = prevMonthBars + curMonthBars;

        // No separator bar — the two-row overlay (day numbers + month name)
        // makes the month boundary clear without wasting a slot.
        // Category strings use "d MMM" for uniqueness.

        for (int d = dayOfMonth + 1; d <= daysInPrev; ++d) {
            int daysAgo = curMonthBars + (daysInPrev - d);
            QDate slotDate(prevMonthDate.year(), prevMonthDate.month(), d);
            categories    << slotDate.toString("d MMM");
            tooltipLabels << slotDate.toString("dd.MM.yy");
            monthBarLabels << QString::number(d);
            yearBarLabels  << monthAbbr(slotDate.month());
            for (int m = 0; m < nMembers; ++m)
                memberBarValues[m] << slotValue(m, daysAgo);
        }
        for (int d = 1; d <= dayOfMonth; ++d) {
            int daysAgo = dayOfMonth - d;
            QDate slotDate(today.year(), today.month(), d);
            categories    << slotDate.toString("d MMM");
            tooltipLabels << slotDate.toString("dd.MM.yy");
            monthBarLabels << QString::number(d);
            yearBarLabels  << monthAbbr(slotDate.month());
            for (int m = 0; m < nMembers; ++m)
                memberBarValues[m] << slotValue(m, daysAgo);
        }
    } else {
        // Monthly view — use calendar arithmetic (addMonths) rather than
        // subtracting grid*bar seconds, since 2678400 s = 31 days drifts.
        // Use fetchTime's calendar month (not newestBoundary, which is snapped
        // to a 31-day grid that can fall in the previous month).
        QDate fetchDate = fetchTime.date();
        QDate newestMonth(fetchDate.year(), fetchDate.month(), 1);
        for (int bar = 0; bar < barsToShow; ++bar) {
            QDate barMonth = newestMonth.addMonths(-bar);
            QString mon = monthAbbr(barMonth.month());
            categories.prepend(mon + QLatin1Char(' ') + barMonth.toString("yy"));
            tooltipLabels.prepend(mon + QLatin1Char(' ') + barMonth.toString("yyyy"));
            monthBarLabels.prepend(mon);
            yearBarLabels.prepend(barMonth.toString("yyyy"));
            for (int m = 0; m < nMembers; ++m)
                memberBarValues[m].prepend(slotValue(m, bar));
        }
    }

    // ---- Y-axis scale: kWh vs Wh ----
    double grandTotal = 0.0;
    double maxStack   = 0.0;
    {
        const int nCats = categories.size();
        for (int slot = 0; slot < nCats; ++slot) {
            double slotSum = 0.0;
            for (int m = 0; m < nMembers; ++m)
                slotSum += memberBarValues[m].value(slot, 0.0);
            grandTotal += slotSum;
            maxStack    = qMax(maxStack, slotSum);
        }
    }

    // All values are zero or NaN → show an informative placeholder instead of
    // rendering invisible zero-height bars (which looks like an empty/broken chart).
    if (grandTotal == 0.0) {
        QStringList labels;
        for (int i = 0; i < nViews; ++i)
            labels << i18n(views[i].label);
        buildEnergyHistoryPlaceholder(labels, selectedIdx);
        return;
    }

    const bool   useKwh = (view.grid != 900) && (grandTotal >= 1000.0);
    const double scale  = useKwh ? 0.001 : 1.0;

    // ---- Build stacked bar series ----
    static const QColor kPalette[] = {
        QColor(0,   120, 215),
        QColor(220, 80,  0  ),
        QColor(0,   153, 76 ),
        QColor(180, 0,   180),
        QColor(200, 160, 0  ),
        QColor(0,   180, 200),
        QColor(220, 50,  50 ),
        QColor(80,  80,  200),
    };
    const int paletteSize = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
    const int lastBar     = categories.size() - 1;

    QStackedBarSeries *barSeries = new QStackedBarSeries();
    barSeries->setBarWidth(0.8);

    QVector<QBarSet *> barSets;
    barSets.reserve(nMembers);
    for (int m = 0; m < nMembers; ++m) {
        const QString &memberName = memberStats.at(m).first;
        QBarSet *bs = new QBarSet(memberName);
        QColor c = kPalette[m % paletteSize];
        c.setAlpha(200);
        bs->setColor(c);

        const QList<double> &vals = memberBarValues[m];
        for (double v : vals)
            *bs << v * scale;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        // Half-opacity tint for the still-accumulating bar; selectBar() is
        // Qt6-only and must be called after values are appended (the bar at
        // lastBar must already exist, otherwise the selection is silently
        // ignored).
        QColor sel = c;
        sel.setAlpha(80);
        bs->setSelectedColor(sel);
        bs->selectBar(lastBar);
#endif

        barSeries->append(bs);
        barSets.append(bs);
    }

    QChart *chart = makeBaseChart(i18n("Energy History"));
    chart->addSeries(barSeries);
    chart->legend()->show();

    QBarCategoryAxis *axisX = new QBarCategoryAxis();
    axisX->append(categories);
    if (view.grid == 900) {
        axisX->setLabelsVisible(false);
        axisX->setGridLineVisible(false);
    }
    chart->addAxis(axisX, Qt::AlignBottom);
    barSeries->attachAxis(axisX);

    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText(useKwh ? i18n("kWh") : i18n("Wh"));
    axisY->setLabelFormat("%.1f");
    applyAxisRange(axisY, roundAxisRange(0.0, maxStack * scale));
    chart->addAxis(axisY, Qt::AlignLeft);
    barSeries->attachAxis(axisY);

    // ---- Two-row axis overlay (grid==86400 or grid==2678400) ----
    if ((view.grid == 86400 || view.grid == 2678400) && !monthBarLabels.isEmpty()) {
        axisX->setLabelsVisible(false);
        QMargins marg = chart->margins();
        marg.setBottom(36);
        chart->setMargins(marg);

        const int nBars = monthBarLabels.size();
        auto *monthLbls = new QVector<QGraphicsTextItem *>();
        for (int i = 0; i < nBars; ++i) {
            auto *lbl = new QGraphicsTextItem(monthBarLabels.at(i), chart);
            lbl->setZValue(11);
            lbl->setDefaultTextColor(QColor(80, 80, 80));
            QFont f = lbl->font();
            f.setPointSize(qMax(6, f.pointSize() - 1));
            lbl->setFont(f);
            monthLbls->append(lbl);
        }
        struct YearSpan { QString year; int first; int last; };
        auto *yearSpans = new QVector<YearSpan>();
        for (int i = 0; i < nBars; ++i) {
            const QString &y = yearBarLabels.at(i);
            if (!yearSpans->isEmpty() && yearSpans->last().year == y)
                yearSpans->last().last = i;
            else
                yearSpans->append({y, i, i});
        }
        auto *yearLbls = new QVector<QGraphicsTextItem *>();
        for (const YearSpan &ys : *yearSpans) {
            auto *lbl = new QGraphicsTextItem(ys.year, chart);
            lbl->setZValue(11);
            lbl->setDefaultTextColor(QColor(60, 60, 60));
            QFont f = lbl->font();
            f.setPointSize(qMax(6, f.pointSize() - 1));
            f.setBold(true);
            lbl->setFont(f);
            yearLbls->append(lbl);
        }
        auto updateMonthOverlay = [chart, barSeries, monthLbls, yearLbls, yearSpans, nBars]() {
            const QRectF pa = chart->plotArea();
            if (nBars < 1) return;
            const double cx0 = chart->mapToPosition(QPointF(0, 0), barSeries).x();
            const double cx1 = (nBars > 1)
                ? chart->mapToPosition(QPointF(1, 0), barSeries).x()
                : cx0 + pa.width();
            const double slotW = cx1 - cx0;
            const double monthH = monthLbls->isEmpty() ? 16.0 : monthLbls->first()->boundingRect().height();
            const double yearH  = yearLbls->isEmpty()  ? 16.0 : yearLbls->first()->boundingRect().height();
            constexpr double kAxisOverhang = 6.0;
            const double gap    = (36.0 - kAxisOverhang - monthH - yearH) / 3.0;
            const double monthY = pa.bottom() + kAxisOverhang + gap;
            const double yearY  = monthY + monthH + gap;
            for (int i = 0; i < monthLbls->size(); ++i) {
                QGraphicsTextItem *lbl = monthLbls->at(i);
                const double cx = chart->mapToPosition(QPointF(i, 0), barSeries).x();
                lbl->setPos(cx - lbl->boundingRect().width() * 0.5, monthY);
            }
            for (int j = 0; j < yearLbls->size(); ++j) {
                const YearSpan &ys = yearSpans->at(j);
                const double xLeft  = chart->mapToPosition(QPointF(ys.first, 0), barSeries).x() - slotW * 0.5;
                const double xRight = chart->mapToPosition(QPointF(ys.last,  0), barSeries).x() + slotW * 0.5;
                QGraphicsTextItem *lbl = yearLbls->at(j);
                lbl->setPos((xLeft + xRight) * 0.5 - lbl->boundingRect().width() * 0.5, yearY);
            }
        };
        updateMonthOverlay();
        QObject::connect(chart, &QChart::plotAreaChanged, chart,
                         [updateMonthOverlay](const QRectF &) { updateMonthOverlay(); });
    }

    // ---- Hour separator lines (grid==900 only) ----
    if (view.grid == 900 && !sepCatIndices.isEmpty()) {
        auto *sepLines  = new QVector<QGraphicsLineItem *>();
        auto *sepLabels = new QVector<QGraphicsTextItem *>();
        for (int i = 0; i < sepCatIndices.size(); ++i) {
            auto *line = new QGraphicsLineItem(chart);
            QPen pen(QColor(160, 160, 160, 180));
            pen.setWidth(1);
            line->setPen(pen);
            line->setZValue(10);
            sepLines->append(line);
            auto *lbl = new QGraphicsTextItem(sepHourLabels.at(i), chart);
            lbl->setZValue(11);
            lbl->setDefaultTextColor(QColor(80, 80, 80));
            QFont lblFont = lbl->font();
            lblFont.setPointSize(qMax(6, lblFont.pointSize() - 1));
            lbl->setFont(lblFont);
            sepLabels->append(lbl);
        }
        auto updateSepLines = [chart, barSeries, sepLines, sepLabels, sepCatIndices]() {
            const QRectF pa = chart->plotArea();
            const double slotW = (sepCatIndices.size() >= 1 && sepCatIndices.at(0) > 0)
                ? chart->mapToPosition(QPointF(sepCatIndices.at(0),     0), barSeries).x()
                - chart->mapToPosition(QPointF(sepCatIndices.at(0) - 1, 0), barSeries).x()
                : pa.width();
            for (int i = 0; i < sepLines->size(); ++i) {
                const double cx = chart->mapToPosition(
                    QPointF(sepCatIndices.at(i), 0), barSeries).x();
                const double x = cx - slotW * 0.5;
                sepLines->at(i)->setLine(x, pa.top(), x, pa.bottom());
                QGraphicsTextItem *lbl = sepLabels->at(i);
                lbl->setPos(x - lbl->boundingRect().width() * 0.5, pa.bottom() + 3);
            }
        };
        updateSepLines();
        QObject::connect(chart, &QChart::plotAreaChanged, chart,
                         [updateSepLines](const QRectF &) { updateSepLines(); });
    }

    // ---- Overlay layout: chartView + comboOverlay + totalOverlay ----
    QChartView *chartView = makeChartView(chart);
    chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QComboBox *combo = new QComboBox();
    for (int i = 0; i < nViews; ++i)
        combo->addItem(i18n(views[i].label));
    QWidget *comboOverlay = new QWidget();
    comboOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    comboOverlay->setMouseTracking(true);          // receive MouseMove without button press
    comboOverlay->setStyleSheet("background: transparent;");
    {
        QVBoxLayout *vl = new QVBoxLayout(comboOverlay);
        vl->setContentsMargins(6, 4, 0, 0);
        vl->setSpacing(0);
        QHBoxLayout *hl = new QHBoxLayout();
        hl->setSpacing(4);
        QLabel *viewLabel = new QLabel(i18n("View:"));
        viewLabel->setStyleSheet("background: transparent;");
        hl->addWidget(viewLabel);
        combo->setStyleSheet("QComboBox { background: palette(button); }");
        hl->addWidget(combo);
        hl->addStretch();
        vl->addLayout(hl);
        vl->addStretch();
    }

    // Grand total overlay (top-right)
    QString totalText = useKwh
        ? QString("%1 kWh").arg(grandTotal / 1000.0, 0, 'f', 2)
        : QString("%1 Wh").arg(grandTotal, 0, 'f', 1);
    QLabel *totalLabel = new QLabel(totalText);
    {
        QFont f = totalLabel->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 2);
        totalLabel->setFont(f);
    }
    totalLabel->setStyleSheet("background: transparent;");
    QWidget *totalOverlay = new QWidget();
    totalOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    totalOverlay->setStyleSheet("background: transparent;");
    {
        QVBoxLayout *vl = new QVBoxLayout(totalOverlay);
        vl->setContentsMargins(0, 4, 10, 0);
        vl->setSpacing(0);
        QHBoxLayout *hl = new QHBoxLayout();
        hl->addStretch();
        hl->addWidget(totalLabel);
        vl->addLayout(hl);
        vl->addStretch();
    }

    QWidget *container = new QWidget();
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QStackedLayout *stack = new QStackedLayout(container);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->addWidget(chartView);
    stack->addWidget(comboOverlay);
    stack->addWidget(totalOverlay);
    comboOverlay->raise();
    totalOverlay->raise();

    // ---- Hover tooltips ----
    // Each QBarSet gets its own hovered lambda; tooltip format includes member name.
    for (int m = 0; m < nMembers; ++m) {
        const QString memberName = memberStats.at(m).first;
        QBarSet *bs              = barSets.at(m);
        const QList<double> &vals = memberBarValues[m];
        auto onBarHovered = [this, tooltipLabels, vals, memberName, useKwh, chartView]
                            (bool status, int index) {
            if (!status) {
                m_energyBarTooltip.clear();
                QToolTip::hideText();
                return;
            }
            if (index < 0 || index >= tooltipLabels.size()) return;
            if (tooltipLabels.at(index).isEmpty()) {
                m_energyBarTooltip.clear();
                QToolTip::hideText();
                return;
            }
            const QString unit = useKwh ? QStringLiteral("kWh") : QStringLiteral("Wh");
            const double  val  = vals.value(index, 0.0) * (useKwh ? 0.001 : 1.0);
            const QString newTip = QString("%1\n%2: %3 %4")
                                   .arg(tooltipLabels.at(index))
                                   .arg(memberName)
                                   .arg(val, 0, 'f', useKwh ? 3 : 1)
                                   .arg(unit);
            const bool changed = (newTip != m_energyBarTooltip);
            m_energyBarTooltip = newTip;
            if (changed && QToolTip::isVisible()) {
                QToolTip::showText(QCursor::pos(), QString(), chartView);
                QToolTip::showText(QCursor::pos(), newTip,   chartView);
            }
        };
        connect(bs, &QBarSet::hovered, this, onBarHovered);
    }
    // Install event filter on both layers so QHelpEvent triggers the tooltip
    // regardless of which widget the cursor is over.
    // Mouse move events from comboOverlay are forwarded to the chart viewport
    // by the event filter so that QBarSet::hovered signals still fire.
    chartView->viewport()->installEventFilter(this);
    comboOverlay->installEventFilter(this);

    m_energyChartView = chartView;
    m_energyResCombo = combo;
    combo->blockSignals(true);
    combo->setCurrentIndex(selectedIdx);
    combo->blockSignals(false);
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChartWidget::onEnergyResolutionChanged);

    m_activeEnergyGrid = view.grid;

    int insertAt = (m_energyHistoryTabIndex >= 0
                    && m_energyHistoryTabIndex <= m_tabs->count())
                   ? m_energyHistoryTabIndex
                   : m_tabs->count();
    m_energyHistoryTabIndex = m_tabs->insertTab(insertAt, container, i18n("Energy History"));
}

// ---------------------------------------------------------------------------
// Time-window helpers
// ---------------------------------------------------------------------------

qint64 ChartWidget::windowMs() const
{
    int idx = qBound(0, m_windowComboIndex, 8);
    return kWindowMs[idx];
}

void ChartWidget::updateScrollBar()
{
    // Helper: apply the computed range/step/value to one scroll bar without
    // triggering its valueChanged signal (which would call applyTimeWindow again).
    auto applyToBar = [&](QScrollBar *bar, int pageStepSec, int spanSec) {
        if (!bar) return;
        bar->blockSignals(true);
        bar->setPageStep(pageStepSec);
        bar->setRange(pageStepSec, spanSec);
        if (m_scrollAtEnd)
            bar->setValue(bar->maximum());
        bar->blockSignals(false);
    };

    // Compute the earliest timestamp across all active rolling series.
    qint64 dataStartMs = std::numeric_limits<qint64>::max();

    auto updateFrom = [&](const QList<QPair<QDateTime, double>> &history) {
        if (!history.isEmpty()) {
            qint64 t = history.first().first.toMSecsSinceEpoch();
            dataStartMs = qMin(dataStartMs, t);
        }
    };

    updateFrom(m_device.temperatureHistory);
    for (const FritzDevice &mem : m_memberDevices) {
        updateFrom(mem.temperatureHistory);
        updateFrom(mem.powerHistory);
    }
    updateFrom(m_device.powerHistory);

    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

    if (dataStartMs == std::numeric_limits<qint64>::max() || dataStartMs >= nowMs) {
        // No data yet — reset both bars.
        for (QScrollBar *bar : {m_scrollBar, m_powerScrollBar}) {
            if (!bar) continue;
            bar->blockSignals(true);
            bar->setRange(0, 0);
            bar->setValue(0);
            bar->blockSignals(false);
        }
        return;
    }

    // Total history span in seconds
    qint64 spanSec    = (nowMs - dataStartMs) / 1000;
    int pageStepSec   = static_cast<int>(qMin(windowMs() / 1000, spanSec));

    applyToBar(m_scrollBar,      pageStepSec, static_cast<int>(spanSec));
    applyToBar(m_powerScrollBar, pageStepSec, static_cast<int>(spanSec));
}

void ChartWidget::applyTimeWindow()
{
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    qint64 winMs = windowMs();

    // Determine the right edge of the visible window.
    // When pinned to the live end (m_scrollAtEnd) use the current clock time.
    // Otherwise derive maxMs from the scroll-bar position (both bars are kept
    // in sync, so either one gives the same value; prefer m_scrollBar).
    QScrollBar *anyBar = m_scrollBar ? m_scrollBar : m_powerScrollBar;
    qint64 maxMs;
    if (m_scrollAtEnd || !anyBar || anyBar->maximum() == 0) {
        maxMs = nowMs;
    } else {
        // Compute data start from scroll bar range (rangeMax = total span in sec)
        qint64 spanSec     = anyBar->maximum();
        qint64 dataStartMs = nowMs - spanSec * 1000LL;
        maxMs = dataStartMs + (qint64)anyBar->value() * 1000LL;
        // Don't let maxMs exceed now to avoid showing a blank future region.
        if (maxMs > nowMs) maxMs = nowMs;
    }
    qint64 minMs = maxMs - winMs;

    QDateTime minDt = QDateTime::fromMSecsSinceEpoch(minMs);
    QDateTime maxDt = QDateTime::fromMSecsSinceEpoch(maxMs);

    if (m_tempAxisX) {
        m_tempAxisX->setRange(minDt, maxDt);
        applyTimeAxisTicks(m_tempAxisX, winMs);
        rescaleYTemp(minMs, maxMs);
    }
    if (m_groupTempAxisX) {
        m_groupTempAxisX->setRange(minDt, maxDt);
        applyTimeAxisTicks(m_groupTempAxisX, winMs);
        rescaleYGroupTemp(minMs, maxMs);
    }
    if (m_powerAxisX) {
        m_powerAxisX->setRange(minDt, maxDt);
        applyTimeAxisTicks(m_powerAxisX, winMs);
        rescaleYPower(minMs, maxMs);
    }
}

void ChartWidget::rescaleYTemp(qint64 minMs, qint64 maxMs)
{
    if (!m_tempAxisY)
        return;

    // When the scale is locked, restore the captured range and do nothing else.
    if (m_tempScaleLocked) {
        m_tempAxisY->setRange(m_lockedTempMin, m_lockedTempMax);
        return;
    }

    const auto &history = m_device.temperatureHistory;
    if (history.isEmpty()) {
        m_tempAxisY->setRange(0, 30);
        return;
    }

    double minVal = std::numeric_limits<double>::max();
    double maxVal = std::numeric_limits<double>::lowest();
    bool found = false;

    for (const auto &p : history) {
        qint64 t = p.first.toMSecsSinceEpoch();
        if (t >= minMs && t <= maxMs) {
            minVal = qMin(minVal, p.second);
            maxVal = qMax(maxVal, p.second);
            found = true;
        }
    }

    if (!found) {
        // No data in window — fall back to full range
        minVal = history.first().second;
        maxVal = history.first().second;
        for (const auto &p : history) {
            minVal = qMin(minVal, p.second);
            maxVal = qMax(maxVal, p.second);
        }
    }

    double margin = qMax(1.0, (maxVal - minVal) * 0.1);
    applyAxisRange(m_tempAxisY, roundAxisRange(minVal - margin, maxVal + margin));
}

void ChartWidget::rescaleYGroupTemp(qint64 minMs, qint64 maxMs)
{
    if (!m_groupTempAxisY || m_groupTempSeries.isEmpty())
        return;

    // When the scale is locked, restore the captured range and do nothing else.
    if (m_tempScaleLocked) {
        m_groupTempAxisY->setRange(m_lockedTempMin, m_lockedTempMax);
        return;
    }

    double minVal =  std::numeric_limits<double>::max();
    double maxVal =  std::numeric_limits<double>::lowest();
    bool found = false;

    // Scan each member's history for points within the window
    const int n = m_groupTempSeries.size();
    for (int i = 0; i < n; ++i) {
        QXYSeries *s = m_groupTempSeries.at(i);
        if (!s)
            continue;
        const auto pts = s->points();
        for (const auto &pt : pts) {
            qint64 t = static_cast<qint64>(pt.x());
            if (t >= minMs && t <= maxMs) {
                minVal = qMin(minVal, pt.y());
                maxVal = qMax(maxVal, pt.y());
                found = true;
            }
        }
    }
    if (!found) {
        // No data in window — scan full series range
        for (int i = 0; i < n; ++i) {
            QXYSeries *s = m_groupTempSeries.at(i);
            if (!s)
                continue;
            const auto pts = s->points();
            for (const auto &pt : pts) {
                minVal = qMin(minVal, pt.y());
                maxVal = qMax(maxVal, pt.y());
                found = true;
            }
        }
    }
    if (!found) {
        m_groupTempAxisY->setRange(0, 30);
        return;
    }
    double margin = qMax(1.0, (maxVal - minVal) * 0.1);
    applyAxisRange(m_groupTempAxisY, roundAxisRange(minVal - margin, maxVal + margin));
}

void ChartWidget::rescaleYPower(qint64 minMs, qint64 maxMs)
{
    if (!m_powerAxisY)
        return;

    // When the scale is locked, restore the captured range and do nothing else.
    if (m_powerScaleLocked) {
        m_powerAxisY->setRange(m_lockedPowerMin, m_lockedPowerMax);
        return;
    }

    // Stacked mode: Y-max = sum of all member maxima within the window
    if (!m_powerStackedUpper.isEmpty() && !m_memberDevices.isEmpty()) {
        // At each timestamp within the window, compute the cumulative sum.
        // Simplification: sum up the per-device max values within the window.
        double yMax = 0.0;
        bool found = false;

        // Build timestamp union again for the rescale window
        const int n = m_memberDevices.size();
        QMap<qint64, QVector<double>> tsMap;
        for (int i = 0; i < n; ++i) {
            for (const auto &pt : m_memberDevices.at(i).powerHistory) {
                qint64 ts = pt.first.toMSecsSinceEpoch();
                if (ts >= minMs && ts <= maxMs) {
                    if (!tsMap.contains(ts))
                        tsMap[ts] = QVector<double>(n, 0.0);
                    tsMap[ts][i] = pt.second;
                    found = true;
                }
            }
        }
        if (!found) {
            // Fall back to all history
            for (int i = 0; i < n; ++i) {
                for (const auto &pt : m_memberDevices.at(i).powerHistory) {
                    qint64 ts = pt.first.toMSecsSinceEpoch();
                    if (!tsMap.contains(ts))
                        tsMap[ts] = QVector<double>(n, 0.0);
                    tsMap[ts][i] = pt.second;
                }
            }
        }
        for (auto it = tsMap.constBegin(); it != tsMap.constEnd(); ++it) {
            double sum = 0.0;
            for (double v : it.value()) sum += v;
            yMax = qMax(yMax, sum);
        }
        m_powerAxisY->setMin(0);
        applyAxisRange(m_powerAxisY, roundAxisRange(0.0, yMax));
        return;
    }

    // Single-device mode
    const auto &history = m_device.powerHistory;
    if (history.isEmpty()) {
        m_powerAxisY->setRange(0, 10);
        return;
    }

    double maxVal = 0.0;
    bool found = false;

    for (const auto &p : history) {
        qint64 t = p.first.toMSecsSinceEpoch();
        if (t >= minMs && t <= maxMs) {
            maxVal = qMax(maxVal, p.second);
            found = true;
        }
    }

    if (!found) {
        for (const auto &p : history)
            maxVal = qMax(maxVal, p.second);
    }

    m_powerAxisY->setMin(0);
    applyAxisRange(m_powerAxisY, roundAxisRange(0.0, maxVal));
}

// ---------------------------------------------------------------------------
// Settings persistence
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Event filter — responds to QHelpEvent (QEvent::ToolTip) on the energy
// history chart view viewport.  This is the same mechanism used internally
// by QAbstractItemView for item tooltips: the tooltip is shown exactly once
// per QHelpEvent and stays alive as long as the mouse is over the widget,
// with no artificial timeout.
// ---------------------------------------------------------------------------
bool ChartWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *he = static_cast<QHelpEvent *>(event);
        if (!m_energyBarTooltip.isEmpty())
            QToolTip::showText(he->globalPos(), m_energyBarTooltip);
        else
            QToolTip::hideText();
        return true;   // event handled — don't propagate
    }

    // Forward mouse-move events from the comboOverlay to the chart viewport so
    // that Qt Charts' internal hover tracking can fire QBarSet::hovered signals.
    // Without this, comboOverlay (WA_TransparentForMouseEvents=false, sitting on
    // top of chartView in StackAll) swallows all mouse moves.
    // The overlay has setMouseTracking(true) so it receives MouseMove even without
    // a button press.
    if (m_energyChartView
        && watched != m_energyChartView->viewport()
        && event->type() == QEvent::MouseMove) {
        QWidget *viewport = m_energyChartView->viewport();
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        // Map the position from the overlay widget to the chart viewport.
        QWidget *srcWidget = qobject_cast<QWidget *>(watched);
        QPoint viewportPos = srcWidget
            ? viewport->mapFromGlobal(srcWidget->mapToGlobal(me->pos()))
            : me->pos();
        QMouseEvent forwarded(QEvent::MouseMove, viewportPos,
                              viewport->mapToGlobal(viewportPos),
                              me->button(), me->buttons(), me->modifiers());
        QCoreApplication::sendEvent(viewport, &forwarded);
        // Don't return true — let the overlay also process the event normally
        // (so the combo box hover effects, cursor shapes, etc. still work).
    }

    return QWidget::eventFilter(watched, event);
}

void ChartWidget::saveChartState() const
{
    QSettings s;
    s.setValue(QStringLiteral("ui/chartSlider"), m_windowComboIndex);
    if (m_tabs->currentIndex() >= 0) {
        const QString tabName = plainTabText(m_tabs->tabText(m_tabs->currentIndex()));
        s.setValue(QStringLiteral("ui/chartTab"), tabName);
    }
    s.setValue(QStringLiteral("ui/energyResIdx"), m_energyResSelectedIdx);
    s.setValue(QStringLiteral("ui/powerScaleLocked"), m_powerScaleLocked);
    s.setValue(QStringLiteral("ui/lockedPowerMin"), m_lockedPowerMin);
    s.setValue(QStringLiteral("ui/lockedPowerMax"), m_lockedPowerMax);
    s.setValue(QStringLiteral("ui/tempScaleLocked"), m_tempScaleLocked);
    s.setValue(QStringLiteral("ui/lockedTempMin"), m_lockedTempMin);
    s.setValue(QStringLiteral("ui/lockedTempMax"), m_lockedTempMax);
}

void ChartWidget::loadChartState()
{
    QSettings s;
    if (s.contains(QStringLiteral("ui/chartSlider"))) {
        m_windowComboIndex = qBound(0, s.value(QStringLiteral("ui/chartSlider")).toInt(), 8);
    }
    // Pie display mode removed; labels always show both absolute and percent.
    m_energyResSelectedIdx = s.value(QStringLiteral("ui/energyResIdx"), 0).toInt();
    m_powerScaleLocked = s.value(QStringLiteral("ui/powerScaleLocked"), false).toBool();
    m_lockedPowerMin   = s.value(QStringLiteral("ui/lockedPowerMin"),   0.0).toDouble();
    m_lockedPowerMax   = s.value(QStringLiteral("ui/lockedPowerMax"),  10.0).toDouble();
    if (m_powerLockCheckBox) {
        m_powerLockCheckBox->blockSignals(true);
        m_powerLockCheckBox->setChecked(m_powerScaleLocked);
        m_powerLockCheckBox->blockSignals(false);
    }
    m_tempScaleLocked = s.value(QStringLiteral("ui/tempScaleLocked"), false).toBool();
    m_lockedTempMin   = s.value(QStringLiteral("ui/lockedTempMin"),   0.0).toDouble();
    m_lockedTempMax   = s.value(QStringLiteral("ui/lockedTempMax"),  30.0).toDouble();
    if (m_tempLockCheckBox) {
        m_tempLockCheckBox->blockSignals(true);
        m_tempLockCheckBox->setChecked(m_tempScaleLocked);
        m_tempLockCheckBox->blockSignals(false);
    }
}
