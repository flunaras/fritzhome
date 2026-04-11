/// \file chartutils.cpp
/// \brief Implementation of shared chart utility functions.
///
/// These are pure helper functions extracted from chartwidget.cpp to be shared
/// across ChartWidget and its builder classes.

#include "chartutils.h"
#include "i18n_shim.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QLabel>
#include <QScrollBar>
#include <QComboBox>
#include <QCheckBox>
#include <QIcon>
#include <QPalette>

#include <cmath>
#include <algorithm>

// ── Constants ────────────────────────────────────────────────────────────────

const qint64 kWindowMs[] = {
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

const char * const kWindowLabels[] = {
    "5 min", "15 min", "30 min", "1 h", "2 h", "4 h", "8 h", "16 h", "24 h"
};

const QColor kChartPalette[] = {
    QColor(0,   120, 215),
    QColor(220, 80,  0  ),
    QColor(0,   153, 76 ),
    QColor(180, 0,   180),
    QColor(200, 160, 0  ),
    QColor(0,   180, 200),
    QColor(220, 50,  50 ),
    QColor(80,  80,  200),
};
const int kChartPaletteSize = static_cast<int>(std::size(kChartPalette));

// ── Tab-text utility ────────────────────────────────────────────────────────

QString plainTabText(const QString &raw)
{
    QString s = raw;
    s.remove(QLatin1Char('&'));
    return s;
}

// ── Translated month abbreviation ───────────────────────────────────────────

QString monthAbbr(int month)
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

// ── Y-axis rounding ─────────────────────────────────────────────────────────

double niceStep(double rawStep)
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

AxisRange roundAxisRange(double rawMin, double rawMax, int targetTicks)
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

void applyAxisRange(QValueAxis *axis, const AxisRange &r)
{
    axis->setRange(r.lo, r.hi);
    axis->setTickInterval(r.step);
    axis->setTickAnchor(r.lo);
    axis->setTickType(QValueAxis::TicksDynamic);
}

// ── Time-axis ticks ─────────────────────────────────────────────────────────

qint64 niceTimeTickIntervalMs(qint64 windowMs)
{
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

void applyTimeAxisTicks(QDateTimeAxis *axis, qint64 windowMs)
{
    qint64 intervalMs = niceTimeTickIntervalMs(windowMs);
    int tickCount = qMax(2, static_cast<int>(windowMs / intervalMs) + 1);
    axis->setTickCount(tickCount);
}

// ── Chart factory helpers ───────────────────────────────────────────────────

QChartView *makeChartView(QChart *chart)
{
    QChartView *view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setMinimumHeight(200);
    return view;
}

QChart *makeBaseChart(const QString &title)
{
    QChart *chart = new QChart();
    chart->setTitle(title);
    chart->legend()->hide();
    chart->setAnimationOptions(QChart::NoAnimation);
    chart->setTheme(QChart::ChartThemeQt);
    return chart;
}

void configureTimeAxis(QDateTimeAxis *axis, const QString &label)
{
    axis->setFormat("hh:mm");
    axis->setTitleText(label);
}

QWidget *wrapInFramedContainer(QWidget *innerWidget)
{
    QWidget *panel = new QWidget();
    QVBoxLayout *outerVl = new QVBoxLayout(panel);

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

QWidget *createErrorDisplayWidget(const QString &caption, const QStringList &errors)
{
    QWidget *container = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

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

    if (!errors.isEmpty()) {
        QLabel *errorListLabel = new QLabel();
        errorListLabel->setWordWrap(true);

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

QWidget *makeChartTab(QChart *chart, const QString &currentValueText,
                      QPointer<QLabel> *outLabel,
                      QScrollBar *scrollBar,
                      QCheckBox *lockCheckBox,
                      QComboBox *windowCombo)
{
    QChartView *view = makeChartView(chart);

    QLabel *valueLabel = new QLabel(currentValueText);
    valueLabel->setObjectName("currentValueLabel");
    valueLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    QFont f = valueLabel->font();
    f.setPointSize(18);
    f.setBold(true);
    valueLabel->setFont(f);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
    valueLabel->setStyleSheet("color: #333333; background: transparent; padding: 6px 10px 0 0;");

    QWidget *chartStack = new QWidget();
    QStackedLayout *stack = new QStackedLayout(chartStack);
    stack->setStackingMode(QStackedLayout::StackAll);
    stack->addWidget(view);
    stack->addWidget(valueLabel);
    valueLabel->raise();
    if (outLabel)
        *outLabel = valueLabel;

    if (windowCombo) {
        QLabel *caption = new QLabel(i18n("Time window:"), chartStack);
        caption->setStyleSheet("background: transparent;");
        caption->adjustSize();

        windowCombo->setParent(chartStack);
        windowCombo->setStyleSheet("QComboBox { background: palette(button); }");
        windowCombo->show();
        windowCombo->adjustSize();

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

    if (lockCheckBox) {
        lockCheckBox->setParent(chartStack);
        lockCheckBox->adjustSize();
        lockCheckBox->show();
        auto positionCb = [lockCheckBox, chartStack]() {
            lockCheckBox->adjustSize();
            QSize cs = chartStack->size();
            QSize ls = lockCheckBox->sizeHint();
            lockCheckBox->move(6, cs.height() - ls.height() - 6);
            lockCheckBox->raise();
        };
        positionCb();
        struct ResizeFilter : public QObject {
            QCheckBox *cb;
            ResizeFilter(QCheckBox *c, QObject *parent) : QObject(parent), cb(c) {}
            bool eventFilter(QObject *, QEvent *e) override {
                if (e->type() == QEvent::Resize || e->type() == QEvent::Show) {
                    QWidget *p = qobject_cast<QWidget*>(parent());
                    if (p) {
                        cb->adjustSize();
                        QSize cs = p->size();
                        QSize ls = cb->sizeHint();
                        cb->move(6, cs.height() - ls.height() - 6);
                        cb->raise();
                    }
                }
                return false;
            }
        };
        chartStack->installEventFilter(new ResizeFilter(lockCheckBox, chartStack));
        lockCheckBox->raise();
    }

    if (scrollBar) {
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

// ── History range scanning ──────────────────────────────────────────────────

bool scanHistoryRange(
    const QList<QPair<QDateTime, double>> &history,
    qint64 minMs, qint64 maxMs,
    double &outMin, double &outMax)
{
    if (history.isEmpty())
        return false;

    outMin = std::numeric_limits<double>::max();
    outMax = std::numeric_limits<double>::lowest();
    bool found = false;

    for (const auto &p : history) {
        qint64 t = p.first.toMSecsSinceEpoch();
        if (t >= minMs && t <= maxMs) {
            outMin = qMin(outMin, p.second);
            outMax = qMax(outMax, p.second);
            found = true;
        }
    }
    if (!found) {
        for (const auto &p : history) {
            outMin = qMin(outMin, p.second);
            outMax = qMax(outMax, p.second);
        }
    }
    return true;
}

bool scanSeriesRange(
    const QList<QXYSeries *> &seriesList,
    qint64 minMs, qint64 maxMs,
    double &outMin, double &outMax)
{
    outMin = std::numeric_limits<double>::max();
    outMax = std::numeric_limits<double>::lowest();
    bool found = false;

    for (QXYSeries *s : seriesList) {
        if (!s) continue;
        const auto pts = s->points();
        for (const auto &pt : pts) {
            qint64 t = static_cast<qint64>(pt.x());
            if (t >= minMs && t <= maxMs) {
                outMin = qMin(outMin, pt.y());
                outMax = qMax(outMax, pt.y());
                found = true;
            }
        }
    }
    if (!found) {
        for (QXYSeries *s : seriesList) {
            if (!s) continue;
            const auto pts = s->points();
            for (const auto &pt : pts) {
                outMin = qMin(outMin, pt.y());
                outMax = qMax(outMax, pt.y());
                found = true;
            }
        }
    }
    return found;
}

// ── Series downsampling ─────────────────────────────────────────────────────

QList<QPointF> downsampleMinMax(const QList<QPointF> &points)
{
    const int n = points.size();
    if (n <= kMaxSeriesPoints)
        return points;                 // implicit-sharing: no deep copy

    // Divide the X range into kMaxSeriesPoints/2 equal-width buckets.
    // For each bucket we emit the min-Y and max-Y points (in time order)
    // so that visual extremes (spikes, dips) are preserved.
    const int nBuckets  = kMaxSeriesPoints / 2;
    const double xFirst = points.first().x();
    const double xLast  = points.last().x();
    const double xSpan  = xLast - xFirst;

    if (xSpan <= 0.0)
        return points;                 // degenerate: all same timestamp

    const double bucketW = xSpan / nBuckets;

    QList<QPointF> out;
    out.reserve(kMaxSeriesPoints + 2);  // +2 for first/last guarantees

    // Always keep the very first point for correct area-fill anchoring.
    out.append(points.first());

    int i = 0;
    for (int b = 0; b < nBuckets; ++b) {
        const double bStart = xFirst + b * bucketW;
        const double bEnd   = bStart + bucketW;

        // Find all points in this bucket
        int bucketBegin = i;
        while (i < n && points.at(i).x() < bEnd)
            ++i;
        int bucketEnd = i;             // exclusive

        if (bucketBegin >= bucketEnd)
            continue;                  // empty bucket

        // Find min and max Y within the bucket
        int minIdx = bucketBegin, maxIdx = bucketBegin;
        double minY = points.at(bucketBegin).y();
        double maxY = minY;
        for (int j = bucketBegin + 1; j < bucketEnd; ++j) {
            double y = points.at(j).y();
            if (y < minY) { minY = y; minIdx = j; }
            if (y > maxY) { maxY = y; maxIdx = j; }
        }

        // Emit min and max in time order (avoid duplicates if same point)
        if (minIdx == maxIdx) {
            out.append(points.at(minIdx));
        } else if (minIdx < maxIdx) {
            out.append(points.at(minIdx));
            out.append(points.at(maxIdx));
        } else {
            out.append(points.at(maxIdx));
            out.append(points.at(minIdx));
        }
    }

    // Always keep the very last point for correct area-fill anchoring.
    if (out.last() != points.last())
        out.append(points.last());

    return out;
}
