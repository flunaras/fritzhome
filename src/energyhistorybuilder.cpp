#include "energyhistorybuilder.h"
#include "chartwidget.h"
#include "chartutils.h"
#include "i18n_shim.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedLayout>
#include <QLabel>
#include <QScrollBar>
#include <QComboBox>
#include <QSettings>
#include <QToolTip>
#include <QCursor>
#include <QDebug>
#include <QVector>
#include <QGraphicsLineItem>
#include <limits>
#include <cmath>
#include <algorithm>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QValueAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

// ── Static helpers (file-scope) ─────────────────────────────────────────────

// Build a two-row axis overlay below the plot area using QGraphicsTextItem.
static void buildAxisOverlay(QChart *chart,
                             QAbstractSeries *series,
                             const QStringList &monthBarLabels,
                             const QStringList &yearBarLabels)
{
    const int nBars = monthBarLabels.size();
    if (nBars < 1)
        return;

    QMargins m = chart->margins();
    m.setBottom(36);
    chart->setMargins(m);

    // --- Row 1 labels (one per bar) ---
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

    // --- Row 2 labels (one per contiguous span of identical values) ---
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

    auto updateOverlay = [chart, series, monthLbls, yearLbls, yearSpans, nBars]() {
        const QRectF pa = chart->plotArea();
        if (nBars < 1) return;

        const double cx0 = chart->mapToPosition(QPointF(0, 0), series).x();
        const double cx1 = (nBars > 1)
            ? chart->mapToPosition(QPointF(1, 0), series).x()
            : cx0 + pa.width();
        const double slotW = cx1 - cx0;

        const double monthH = monthLbls->isEmpty()
            ? 16.0 : monthLbls->first()->boundingRect().height();
        const double yearH = yearLbls->isEmpty()
            ? 16.0 : yearLbls->first()->boundingRect().height();
        constexpr double kAxisOverhang = 6.0;
        const double gap    = (36.0 - kAxisOverhang - monthH - yearH) / 3.0;
        const double monthY = pa.bottom() + kAxisOverhang + gap;
        const double yearY  = monthY + monthH + gap;

        for (int i = 0; i < monthLbls->size(); ++i) {
            QGraphicsTextItem *lbl = monthLbls->at(i);
            const double cx = chart->mapToPosition(QPointF(i, 0), series).x();
            lbl->setPos(cx - lbl->boundingRect().width() * 0.5, monthY);
        }
        for (int j = 0; j < yearLbls->size(); ++j) {
            const YearSpan &ys = yearSpans->at(j);
            const double xLeft  = chart->mapToPosition(
                QPointF(ys.first, 0), series).x() - slotW * 0.5;
            const double xRight = chart->mapToPosition(
                QPointF(ys.last,  0), series).x() + slotW * 0.5;
            QGraphicsTextItem *lbl = yearLbls->at(j);
            lbl->setPos((xLeft + xRight) * 0.5 - lbl->boundingRect().width() * 0.5, yearY);
        }
    };
    updateOverlay();
    QObject::connect(chart, &QChart::plotAreaChanged, chart,
                     [updateOverlay](const QRectF &) { updateOverlay(); });
}

// Draw thin vertical separator lines at hour boundaries (grid==900 only).
static void buildHourSeparators(QChart *chart,
                                QAbstractSeries *series,
                                const QVector<int> &sepCatIndices,
                                const QStringList &sepHourLabels)
{
    if (sepCatIndices.isEmpty())
        return;

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

    auto updateLines = [chart, series, sepLines, sepLabels, sepCatIndices]() {
        const QRectF pa = chart->plotArea();
        const double slotW = (sepCatIndices.size() >= 1 && sepCatIndices.at(0) > 0)
            ? chart->mapToPosition(QPointF(sepCatIndices.at(0),     0), series).x()
            - chart->mapToPosition(QPointF(sepCatIndices.at(0) - 1, 0), series).x()
            : pa.width();
        for (int i = 0; i < sepLines->size(); ++i) {
            const double cx = chart->mapToPosition(
                QPointF(sepCatIndices.at(i), 0), series).x();
            const double x = cx - slotW * 0.5;
            sepLines->at(i)->setLine(x, pa.top(), x, pa.bottom());
            QGraphicsTextItem *lbl = sepLabels->at(i);
            lbl->setPos(x - lbl->boundingRect().width() * 0.5, pa.bottom() + 3);
        }
    };
    updateLines();
    QObject::connect(chart, &QChart::plotAreaChanged, chart,
                     [updateLines](const QRectF &) { updateLines(); });
}

// ── Constructor ─────────────────────────────────────────────────────────────

EnergyHistoryBuilder::EnergyHistoryBuilder(ChartWidget &owner)
    : m_owner(owner)
{
}

// ── Reset / teardown ────────────────────────────────────────────────────────

void EnergyHistoryBuilder::reset()
{
    m_energyResCombo     = nullptr;
    m_energyChartView    = nullptr;
    m_energyBarTooltip.clear();
}

void EnergyHistoryBuilder::nullifyWidgetPointers(QWidget *w)
{
    if (m_energyResCombo && m_energyResCombo->parent()
        && (m_energyResCombo->parent() == w
            || m_energyResCombo->parent()->parent() == w
            || (m_energyResCombo->parent()->parent()
                && m_energyResCombo->parent()->parent()->parent() == w)))
        m_energyResCombo = nullptr;
    if (m_energyChartView && m_energyChartView->parent()
        && (m_energyChartView->parent() == w
            || m_energyChartView->parent()->parent() == w
            || (m_energyChartView->parent()->parent()
                && m_energyChartView->parent()->parent()->parent() == w)))
        m_energyChartView = nullptr;
}

// ── Save / Load ─────────────────────────────────────────────────────────────

void EnergyHistoryBuilder::saveState() const
{
    QSettings s;
    s.setValue(QStringLiteral("chart/energyResolution"), m_energyResSelectedIdx);
}

void EnergyHistoryBuilder::loadState()
{
    QSettings s;
    m_energyResSelectedIdx = s.value(QStringLiteral("chart/energyResolution"), 0).toInt();
}

// ── Resolution combo changed ────────────────────────────────────────────────

void EnergyHistoryBuilder::onEnergyResolutionChanged(int index)
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
    m_owner.m_gaugeBuilder.cleanupPieFilter();

    if (m_energyHistoryTabIndex >= 0 && m_energyHistoryTabIndex < m_owner.m_tabs->count()) {
        // Remember which tab the user is on before we touch anything.
        const QString savedActiveTab = plainTabText(
            m_owner.m_tabs->tabText(m_owner.m_tabs->currentIndex()));

        // Block signals so that removeTab/insertTab do not fire currentChanged
        // and corrupt QSettings with a transient wrong tab name.
        m_owner.m_tabs->blockSignals(true);

        QWidget *old = m_owner.m_tabs->widget(m_energyHistoryTabIndex);
        m_owner.m_tabs->removeTab(m_energyHistoryTabIndex);
        // Use deleteLater() instead of delete: we are currently executing inside
        // a signal emitted by a child widget of 'old' (the resolution combo).
        old->deleteLater();

        if (m_groupHistoryMode)
            buildEnergyHistoryChartStacked(m_lastGroupMemberStats);
        else
            buildEnergyHistoryChart(m_lastEnergyStats);

        // Restore the tab the user was on before the rebuild.
        int restoreIdx = m_owner.m_tabs->currentIndex();
        for (int i = 0; i < m_owner.m_tabs->count(); ++i) {
            if (plainTabText(m_owner.m_tabs->tabText(i)) == savedActiveTab) {
                restoreIdx = i;
                break;
            }
        }
        m_owner.m_tabs->setCurrentIndex(restoreIdx);

        m_owner.m_tabs->blockSignals(false);

        // Fire side-effects manually now that the correct tab is selected.
        m_owner.saveChartState();
        m_owner.updateSliderVisibility();
    } else {
        if (m_groupHistoryMode)
            buildEnergyHistoryChartStacked(m_lastGroupMemberStats);
        else
            buildEnergyHistoryChart(m_lastEnergyStats);
    }
}

// ── Error display ───────────────────────────────────────────────────────────

void EnergyHistoryBuilder::showEnergyHistoryError(const QString &caption,
                                                   const QStringList &errors)
{
    if (m_energyHistoryTabIndex < 0 || m_energyHistoryTabIndex >= m_owner.m_tabs->count())
        return;

    QWidget *current = m_owner.m_tabs->widget(m_energyHistoryTabIndex);

    // If the tab already contains a framed error display, update it in-place.
    QVBoxLayout *layout = qobject_cast<QVBoxLayout *>(current->layout());
    if (layout && layout->count() > 0) {
        QWidget *inner = layout->itemAt(0)->widget();
        if (inner) {
            QVBoxLayout *innerLayout = qobject_cast<QVBoxLayout *>(inner->layout());
            if (innerLayout && innerLayout->count() > 0) {
                QWidget *oldWidget = innerLayout->itemAt(0)->widget();
                if (oldWidget) {
                    innerLayout->removeWidget(oldWidget);
                    delete oldWidget;

                    QWidget *errorWidget = createErrorDisplayWidget(caption, errors);
                    innerLayout->addWidget(errorWidget);
                    return;
                }
            }
        }
    }

    // Otherwise, replace the placeholder with a framed error display.
    m_owner.m_tabs->blockSignals(true);
    QWidget *old = m_owner.m_tabs->widget(m_energyHistoryTabIndex);
    m_owner.m_tabs->removeTab(m_energyHistoryTabIndex);
    delete old;
    m_energyResCombo = nullptr;
    m_energyChartView = nullptr;

    QWidget *errorWidget = createErrorDisplayWidget(caption, errors);
    QWidget *framedWidget = wrapInFramedContainer(errorWidget);
    m_owner.m_tabs->insertTab(m_energyHistoryTabIndex, framedWidget, i18n("Energy History"));
    m_owner.m_tabs->blockSignals(false);
}

// ── Placeholder ─────────────────────────────────────────────────────────────

void EnergyHistoryBuilder::buildEnergyHistoryPlaceholder(
    const QStringList &viewLabels, int selectedIdx)
{
    QLabel *noDataMsg = new QLabel(
        i18n("No energy data available for %1 yet.\nData will appear as it becomes available from Fritz!Box.",
             viewLabels.value(selectedIdx)), &m_owner);
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

    m_energyHistoryTabIndex = m_owner.m_tabs->insertTab(
        m_energyHistoryTabIndex >= 0 ? m_energyHistoryTabIndex : m_owner.m_tabs->count(),
        container, i18n("Energy History"));

    m_energyResCombo = combo;
    m_energyChartView = nullptr;
    m_activeEnergyGrid = 0;

    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &m_owner, [this](int idx) { onEnergyResolutionChanged(idx); });
}

// ── Category / axis builder ─────────────────────────────────────────────────

EnergyCategories EnergyHistoryBuilder::buildEnergyCategories(
    int grid, int maxBars, qint64 fetchSecs, const QDateTime &fetchTime) const
{
    EnergyCategories cats;

    const qint64 newestBoundary = (fetchSecs / grid) * grid;

    if (grid == 900) {
        cats.barsToShow = maxBars;
        int blankIdx = 0;
        for (int bar = cats.barsToShow - 1; bar >= 0; --bar) {
            qint64 slotSecs = newestBoundary - static_cast<qint64>(bar) * grid;
            QDateTime slotDt = QDateTime::fromSecsSinceEpoch(slotSecs);
            const bool isHourBoundary = (slotDt.time().minute() == 0);
            if (isHourBoundary && bar < cats.barsToShow - 1) {
                cats.sepCatIndices << cats.categories.size();
                cats.sepHourLabels << slotDt.toString("H");
            }
            cats.categories    << QString(++blankIdx, QLatin1Char(' '));
            cats.tooltipLabels << slotDt.toString("dd.MM.yy HH:mm");
            cats.barIndices    << bar;
        }
    } else if (grid == 86400) {
        QDateTime newestDt   = QDateTime::fromSecsSinceEpoch(newestBoundary);
        QDate     today      = newestDt.date();
        int       dayOfMonth = today.day();
        QDate     prevMonthDate = today.addMonths(-1);
        int       daysInPrev    = prevMonthDate.daysInMonth();
        int       prevMonthBars = qMax(0, daysInPrev - dayOfMonth);
        int       curMonthBars  = dayOfMonth;

        cats.barsToShow = prevMonthBars + curMonthBars;

        for (int d = dayOfMonth + 1; d <= daysInPrev; ++d) {
            int daysAgo = curMonthBars + (daysInPrev - d);
            QDate slotDate(prevMonthDate.year(), prevMonthDate.month(), d);
            cats.categories    << slotDate.toString("d MMM");
            cats.tooltipLabels << slotDate.toString("dd.MM.yy");
            cats.barIndices    << daysAgo;
            cats.monthBarLabels << QString::number(d);
            cats.yearBarLabels  << monthAbbr(slotDate.month());
        }

        for (int d = 1; d <= dayOfMonth; ++d) {
            int daysAgo = dayOfMonth - d;
            QDate slotDate(today.year(), today.month(), d);
            cats.categories    << slotDate.toString("d MMM");
            cats.tooltipLabels << slotDate.toString("dd.MM.yy");
            cats.barIndices    << daysAgo;
            cats.monthBarLabels << QString::number(d);
            cats.yearBarLabels  << monthAbbr(slotDate.month());
        }
    } else {
        cats.barsToShow = maxBars;
        QDate fetchDate = fetchTime.date();
        QDate newestMonth(fetchDate.year(), fetchDate.month(), 1);
        for (int bar = 0; bar < cats.barsToShow; ++bar) {
            QDate barMonth = newestMonth.addMonths(-bar);
            QString mon = monthAbbr(barMonth.month());
            cats.categories.prepend(mon + QLatin1Char(' ') + barMonth.toString("yy"));
            cats.tooltipLabels.prepend(mon + QLatin1Char(' ') + barMonth.toString("yyyy"));
            cats.barIndices.prepend(bar);
            cats.monthBarLabels.prepend(mon);
            cats.yearBarLabels.prepend(barMonth.toString("yyyy"));
        }
    }

    return cats;
}

// ── Axis setup ──────────────────────────────────────────────────────────────

QValueAxis *EnergyHistoryBuilder::setupEnergyHistoryAxes(
    QChart *chart, QAbstractSeries *barSeries,
    const EnergyCategories &cats,
    int grid, bool useKwh, double maxY)
{
    QBarCategoryAxis *axisX = new QBarCategoryAxis();
    axisX->append(cats.categories);
    if (grid == 900) {
        axisX->setLabelsVisible(false);
        axisX->setGridLineVisible(false);
    }
    chart->addAxis(axisX, Qt::AlignBottom);
    barSeries->attachAxis(axisX);

    QValueAxis *axisY = new QValueAxis();
    axisY->setTitleText(useKwh ? i18n("kWh") : i18n("Wh"));
    axisY->setLabelFormat("%.1f");
    applyAxisRange(axisY, roundAxisRange(0.0, maxY));
    chart->addAxis(axisY, Qt::AlignLeft);
    barSeries->attachAxis(axisY);

    if ((grid == 86400 || grid == 2678400) && !cats.monthBarLabels.isEmpty()) {
        axisX->setLabelsVisible(false);
        buildAxisOverlay(chart, barSeries, cats.monthBarLabels, cats.yearBarLabels);
    }

    if (grid == 900)
        buildHourSeparators(chart, barSeries, cats.sepCatIndices, cats.sepHourLabels);

    return axisY;
}

// ── Finalize tab ────────────────────────────────────────────────────────────

void EnergyHistoryBuilder::finalizeEnergyHistoryTab(
    QChartView *chartView,
    const QStringList &viewLabels,
    int selectedIdx,
    const QString &totalText,
    int grid)
{
    chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Combo (view selector) — top-left overlay
    QComboBox *combo = new QComboBox();
    for (const QString &label : viewLabels)
        combo->addItem(label);
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

    // Total energy label — top-right overlay
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

    // Install event filter on both layers so QHelpEvent triggers the tooltip
    chartView->viewport()->installEventFilter(&m_owner);
    comboOverlay->installEventFilter(&m_owner);

    m_energyChartView = chartView;
    m_energyResCombo = combo;
    combo->blockSignals(true);
    combo->setCurrentIndex(selectedIdx);
    combo->blockSignals(false);
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     &m_owner, [this](int idx) { onEnergyResolutionChanged(idx); });

    m_activeEnergyGrid = grid;

    int insertAt = (m_energyHistoryTabIndex >= 0
                    && m_energyHistoryTabIndex <= m_owner.m_tabs->count())
                   ? m_energyHistoryTabIndex
                   : m_owner.m_tabs->count();
    m_energyHistoryTabIndex = m_owner.m_tabs->insertTab(
        insertAt, container, i18n("Energy History"));
}

// ── Single-device energy history chart ──────────────────────────────────────

void EnergyHistoryBuilder::buildEnergyHistoryChart(const DeviceBasicStats &stats)
{
    const StatSeries *hourlySeries  = nullptr;
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

        QLabel *noData = new QLabel(reason, &m_owner);
        noData->setAlignment(Qt::AlignCenter);
        noData->setWordWrap(true);
        m_energyHistoryTabIndex = m_owner.m_tabs->insertTab(
            m_energyHistoryTabIndex >= 0 ? m_energyHistoryTabIndex : m_owner.m_tabs->count(),
            noData, i18n("Energy History"));
        m_activeEnergyGrid = 0;
        return;
    }

    struct ViewDef {
        const char       *label;
        int               bars;
        const StatSeries *series;
        int               grid;
    };

    QVector<ViewDef> views;
    views.append({ QT_TR_NOOP("Last 24 hours"), 96, hourlySeries, 900 });
    if (dailySeries)
        views.append({ QT_TR_NOOP("Rolling month"), 31, dailySeries, 86400 });
    if (monthlySeries)
        views.append({ QT_TR_NOOP("Last 2 years"), 24, monthlySeries, 2678400 });

    const int nViews = views.size();
    int selectedIdx = qBound(0, m_energyResSelectedIdx, nViews - 1);
    const ViewDef &view = views[selectedIdx];

    if (!view.series) {
        QStringList labels;
        for (int i = 0; i < nViews; ++i)
            labels << i18n(views[i].label);
        buildEnergyHistoryPlaceholder(labels, selectedIdx);
        return;
    }

    qint64 fetchSecs = stats.fetchTime.toSecsSinceEpoch();
    int maxBars = (view.grid == 900)
                  ? qMin(view.bars, view.series->values.size())
                  : view.bars;
    EnergyCategories cats = buildEnergyCategories(
        view.grid, maxBars, fetchSecs, stats.fetchTime);

    QList<double> barValues;
    barValues.reserve(cats.barIndices.size());
    for (int idx : cats.barIndices) {
        double v = 0.0;
        if (idx < view.series->values.size()) {
            double raw = view.series->values.at(idx);
            if (!std::isnan(raw)) v = raw;
        }
        barValues << v;
    }

    double rawTotal = 0.0;
    for (double v : barValues) rawTotal += v;
    double maxVal = 0.0;
    for (double v : barValues) maxVal = qMax(maxVal, v);

    if (rawTotal == 0.0) {
        QStringList labels;
        for (int i = 0; i < nViews; ++i)
            labels << i18n(views[i].label);
        buildEnergyHistoryPlaceholder(labels, selectedIdx);
        return;
    }

    const bool   useKwh = (view.grid != 900) && (rawTotal >= 1000.0);
    const double scale  = useKwh ? 0.001 : 1.0;

    const int lastBar = cats.categories.size() - 1;

    QBarSet *barSet = new QBarSet(useKwh ? i18n("kWh") : i18n("Wh"));
    barSet->setColor(QColor(0, 100, 200, 200));
    for (double v : barValues)
        *barSet << v * scale;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
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

    setupEnergyHistoryAxes(chart, barSeries, cats, view.grid, useKwh, maxVal * scale);

    QChartView *chartView = makeChartView(chart);

    auto onBarHovered = [this, cats, barValues, useKwh, chartView](bool status, int index) {
        if (!status) {
            m_energyBarTooltip.clear();
            QToolTip::hideText();
            return;
        }
        if (index < 0 || index >= cats.tooltipLabels.size()) return;
        if (cats.tooltipLabels.at(index).isEmpty()) {
            m_energyBarTooltip.clear();
            QToolTip::hideText();
            return;
        }
        const QString unit = useKwh ? QStringLiteral("kWh") : QStringLiteral("Wh");
        const double  val  = barValues.at(index) * (useKwh ? 0.001 : 1.0);
        const QString newTip = QString("%1\n%2 %3")
                               .arg(cats.tooltipLabels.at(index))
                               .arg(val, 0, 'f', useKwh ? 3 : 1)
                               .arg(unit);
        const bool changed = (newTip != m_energyBarTooltip);
        m_energyBarTooltip = newTip;
        if (changed && QToolTip::isVisible()) {
            QToolTip::showText(QCursor::pos(), QString(), chartView);
            QToolTip::showText(QCursor::pos(), newTip,   chartView);
        }
    };
    QObject::connect(barSet, &QBarSet::hovered, &m_owner, onBarHovered);

    QStringList viewLabelStrings;
    for (int i = 0; i < nViews; ++i)
        viewLabelStrings << i18n(views[i].label);

    QString totalText = useKwh
        ? QString("%1 kWh").arg(rawTotal / 1000.0, 0, 'f', 2)
        : QString("%1 Wh").arg(rawTotal, 0, 'f', 1);

    finalizeEnergyHistoryTab(chartView, viewLabelStrings, selectedIdx, totalText, view.grid);
}

// ── Stacked energy history chart (group mode) ───────────────────────────────

void EnergyHistoryBuilder::buildEnergyHistoryChartStacked(
    const QList<QPair<QString, DeviceBasicStats>> &memberStats)
{
    struct MemberSeriesSet {
        const StatSeries *s900     = nullptr;
        const StatSeries *s86400   = nullptr;
        const StatSeries *s2678400 = nullptr;
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

    bool has900 = false, has86400 = false, has2678400 = false;
    for (const MemberSeriesSet &ms : memberSets) {
        if (ms.s900)     has900     = true;
        if (ms.s86400)   has86400   = true;
        if (ms.s2678400) has2678400 = true;
    }

    if (!has900 && !has86400 && !has2678400) {
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

        QLabel *noData = new QLabel(text, &m_owner);
        noData->setAlignment(Qt::AlignCenter);
        noData->setWordWrap(true);
        m_energyHistoryTabIndex = m_owner.m_tabs->insertTab(
            m_energyHistoryTabIndex >= 0 ? m_energyHistoryTabIndex : m_owner.m_tabs->count(),
            noData, i18n("Energy History"));
        m_activeEnergyGrid = 0;
        return;
    }

    struct ViewDef {
        const char *label;
        int         bars;
        int         grid;
        bool        hasData;
    };
    QVector<ViewDef> views;
    views.append({ QT_TR_NOOP("Last 24 hours"), 96,  900, has900 });
    if (has86400)   views.append({ QT_TR_NOOP("Rolling month"),  31, 86400, true });
    if (has2678400) views.append({ QT_TR_NOOP("Last 2 years"),   24, 2678400, true });

    const int nViews     = views.size();
    int selectedIdx      = qBound(0, m_energyResSelectedIdx, nViews - 1);
    const ViewDef &view  = views[selectedIdx];

    if (!view.hasData) {
        QStringList labels;
        for (int i = 0; i < nViews; ++i)
            labels << i18n(views[i].label);
        buildEnergyHistoryPlaceholder(labels, selectedIdx);
        return;
    }

    qint64 fetchSecs = fetchTime.toSecsSinceEpoch();

    int maxBars = view.bars;
    if (view.grid == 900) {
        int maxVals = 0;
        for (const MemberSeriesSet &ms : memberSets)
            if (ms.s900) maxVals = qMax(maxVals, ms.s900->values.size());
        maxBars = qMin(view.bars, maxVals);
    }

    EnergyCategories cats = buildEnergyCategories(
        view.grid, maxBars, fetchSecs, fetchTime);

    const int nMembers = memberStats.size();
    QVector<QList<double>> memberBarValues(nMembers);

    auto slotValue = [&](int memberIdx, int barIndex) -> double {
        const MemberSeriesSet &ms = memberSets[memberIdx];
        const StatSeries *ss = (view.grid == 900)     ? ms.s900
                             : (view.grid == 86400)   ? ms.s86400
                                                      : ms.s2678400;
        if (!ss || barIndex >= ss->values.size()) return 0.0;
        double raw = ss->values.at(barIndex);
        return std::isnan(raw) ? 0.0 : raw;
    };

    for (int i = 0; i < cats.barIndices.size(); ++i) {
        int idx = cats.barIndices[i];
        for (int m = 0; m < nMembers; ++m)
            memberBarValues[m] << slotValue(m, idx);
    }

    double grandTotal = 0.0;
    double maxStack   = 0.0;
    {
        const int nCats = cats.categories.size();
        for (int slot = 0; slot < nCats; ++slot) {
            double slotSum = 0.0;
            for (int m = 0; m < nMembers; ++m)
                slotSum += memberBarValues[m].value(slot, 0.0);
            grandTotal += slotSum;
            maxStack    = qMax(maxStack, slotSum);
        }
    }

    if (grandTotal == 0.0) {
        QStringList labels;
        for (int i = 0; i < nViews; ++i)
            labels << i18n(views[i].label);
        buildEnergyHistoryPlaceholder(labels, selectedIdx);
        return;
    }

    const bool   useKwh = (view.grid != 900) && (grandTotal >= 1000.0);
    const double scale  = useKwh ? 0.001 : 1.0;

    const int lastBar     = cats.categories.size() - 1;

    QStackedBarSeries *barSeries = new QStackedBarSeries();
    barSeries->setBarWidth(0.8);

    QVector<QBarSet *> barSets;
    barSets.reserve(nMembers);
    for (int m = 0; m < nMembers; ++m) {
        const QString &memberName = memberStats.at(m).first;
        QBarSet *bs = new QBarSet(memberName);
        QColor c = kChartPalette[m % kChartPaletteSize];
        c.setAlpha(200);
        bs->setColor(c);

        const QList<double> &vals = memberBarValues[m];
        for (double v : vals)
            *bs << v * scale;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
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

    setupEnergyHistoryAxes(chart, barSeries, cats, view.grid, useKwh, maxStack * scale);

    QChartView *chartView = makeChartView(chart);

    for (int m = 0; m < nMembers; ++m) {
        const QString memberName = memberStats.at(m).first;
        QBarSet *bs              = barSets.at(m);
        const QList<double> &vals = memberBarValues[m];
        auto onBarHovered = [this, cats, vals, memberName, useKwh, chartView]
                            (bool status, int index) {
            if (!status) {
                m_energyBarTooltip.clear();
                QToolTip::hideText();
                return;
            }
            if (index < 0 || index >= cats.tooltipLabels.size()) return;
            if (cats.tooltipLabels.at(index).isEmpty()) {
                m_energyBarTooltip.clear();
                QToolTip::hideText();
                return;
            }
            const QString unit = useKwh ? QStringLiteral("kWh") : QStringLiteral("Wh");
            const double  val  = vals.value(index, 0.0) * (useKwh ? 0.001 : 1.0);
            const QString newTip = QString("%1\n%2: %3 %4")
                                   .arg(cats.tooltipLabels.at(index))
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
        QObject::connect(bs, &QBarSet::hovered, &m_owner, onBarHovered);
    }

    QStringList viewLabelStrings;
    for (int i = 0; i < nViews; ++i)
        viewLabelStrings << i18n(views[i].label);

    QString totalText = useKwh
        ? QString("%1 kWh").arg(grandTotal / 1000.0, 0, 'f', 2)
        : QString("%1 Wh").arg(grandTotal, 0, 'f', 1);

    finalizeEnergyHistoryTab(chartView, viewLabelStrings, selectedIdx, totalText, view.grid);
}
