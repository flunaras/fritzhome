/// \file chartwidget.cpp
/// \brief Implementation of ChartWidget — orchestrates four builder classes
///        to render device statistics charts.
///
/// ChartWidget owns the tab widget, scroll bars, and shared time-window
/// state.  All chart-specific logic (series creation, axis setup, data
/// updates) is delegated to TemperatureChartBuilder, PowerChartBuilder,
/// EnergyGaugeBuilder, and EnergyHistoryBuilder.

#include "chartwidget.h"
#include "chartutils.h"
#include "i18n_shim.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollBar>
#include <QComboBox>
#include <QCheckBox>
#include <QDateTime>
#include <QSettings>
#include <QToolTip>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QCoreApplication>
#include <limits>
#include <functional>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ChartWidget::ChartWidget(QWidget *parent)
    : QWidget(parent)
    , m_tabs(new QTabWidget(this))
    , m_scrollBar(new QScrollBar(Qt::Horizontal, this))
    , m_powerScrollBar(new QScrollBar(Qt::Horizontal, this))
    , m_tempBuilder(*this)
    , m_powerBuilder(*this)
    , m_gaugeBuilder(*this)
    , m_historyBuilder(*this)
{
    // Save active tab whenever the user switches tabs.
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        saveChartState();
        updateSliderVisibility();
    });

    // Scroll bar: allows viewing any part of the collected history.
    m_scrollBar->setRange(0, 0);
    m_scrollBar->setSingleStep(60);
    m_scrollBar->hide();
    connect(m_scrollBar, &QScrollBar::valueChanged, this, &ChartWidget::onScrollBarChanged);

    m_powerScrollBar->setRange(0, 0);
    m_powerScrollBar->setSingleStep(60);
    m_powerScrollBar->hide();
    connect(m_powerScrollBar, &QScrollBar::valueChanged, this, &ChartWidget::onScrollBarChanged);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tabs, /*stretch=*/1);

    // Restore previously saved window-combo index (tab is restored per-device in updateDevice)
    loadChartState();
}

// ---------------------------------------------------------------------------
// Slider visibility helper — no-op; combo is embedded as chart overlay
// ---------------------------------------------------------------------------

void ChartWidget::updateSliderVisibility()
{
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
    QScrollBar *any = m_scrollBar ? m_scrollBar : m_powerScrollBar;
    if (!any) return;
    m_scrollAtEnd = (value >= any->maximum());
    applyTimeWindow();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ChartWidget::updateDevice(const FritzDevice &device,
                               const FritzDeviceList &memberDevices)
{
    const bool deviceChanged = (m_device.ain != device.ain);

    // Decide which tab to restore after the rebuild.
    QString activeTabText;
    if (!deviceChanged && m_tabs->currentIndex() >= 0) {
        activeTabText = plainTabText(m_tabs->tabText(m_tabs->currentIndex()));
    } else {
        QSettings s;
        activeTabText = s.value(QStringLiteral("ui/chartTab")).toString();
    }

    resetChartState(deviceChanged);

    m_device = device;
    m_memberDevices = memberDevices;

    teardownTabs();
    buildChartsForDevice(device, memberDevices, deviceChanged);
    restoreTabAndApplyWindow(activeTabText);
}

// ── Chart rebuild helpers ─────────────────────────────────────────────────────

void ChartWidget::resetChartState(bool deviceChanged)
{
    if (deviceChanged) {
        m_historyBuilder.groupHistoryMode() = false;
        m_historyBuilder.lastGroupMemberStats().clear();
        m_historyBuilder.energyError().clear();
        m_historyBuilder.groupEnergyErrors().clear();

        m_powerBuilder.resetLock();
        if (m_powerBuilder.m_powerLockCheckBox) {
            m_powerBuilder.m_powerLockCheckBox->blockSignals(true);
            m_powerBuilder.m_powerLockCheckBox->setChecked(false);
            m_powerBuilder.m_powerLockCheckBox->blockSignals(false);
        }

        m_tempBuilder.resetLock();
        if (m_tempBuilder.m_tempLockCheckBox) {
            m_tempBuilder.m_tempLockCheckBox->blockSignals(true);
            m_tempBuilder.m_tempLockCheckBox->setChecked(false);
            m_tempBuilder.m_tempLockCheckBox->blockSignals(false);
        }
    }

    m_tempBuilder.reset();
    m_powerBuilder.reset();
    m_gaugeBuilder.reset();

    m_historyBuilder.m_energyHistoryTabIndex = -1;
    m_historyBuilder.m_activeEnergyGrid      = 0;
    m_historyBuilder.m_lastAvailableGrids    = 0;
    m_historyBuilder.m_lastEnergyStats       = DeviceBasicStats();
    m_historyBuilder.reset();

    // Lock checkboxes are owned by tab widgets (reparented in makeChartTab).
    // Reset to nullptr so guards don't dereference deleted widgets.
    m_powerBuilder.m_powerLockCheckBox = nullptr;
    m_tempBuilder.m_tempLockCheckBox   = nullptr;
}

void ChartWidget::teardownTabs()
{
    // Reparent scroll bars back to this widget before destroying old tabs.
    if (QPointer<QScrollBar> sb = m_scrollBar) sb->setParent(this);
    if (QPointer<QScrollBar> psb = m_powerScrollBar) psb->setParent(this);

    // Block currentChanged signals during teardown.
    m_tabs->blockSignals(true);

    while (m_tabs->count() > 0) {
        QWidget *w = m_tabs->widget(0);
        m_tabs->removeTab(0);

        // Clean up pie chart event filter if this tab owns the pie view.
        if (m_gaugeBuilder.isPieOwner(w))
            m_gaugeBuilder.cleanupPieFilter();

        // Null out widget pointers owned by this tab in each builder.
        if (w) {
            m_gaugeBuilder.nullifyWidgetPointers(w);
            m_powerBuilder.nullifyWidgetPointers(w);
            m_tempBuilder.nullifyWidgetPointers(w);
        }
        delete w;
    }
}

void ChartWidget::buildChartsForDevice(const FritzDevice &device,
                                        const FritzDeviceList &memberDevices,
                                        bool deviceChanged)
{
    if (device.isGroup()) {
        m_tempBuilder.buildGroupTemperatureChart(memberDevices);
    } else if (device.hasTemperature() || device.hasThermostat()) {
        m_tempBuilder.buildTemperatureChart(device);
    }
    if (device.hasEnergyMeter()) {
        m_powerBuilder.buildPowerChart(device, memberDevices);
        m_gaugeBuilder.buildEnergyGauge(device, memberDevices);

        if (device.basicStats.valid && deviceChanged && !device.isGroup()) {
            m_historyBuilder.m_lastEnergyStats = device.basicStats;
            m_historyBuilder.buildEnergyHistoryChart(device.basicStats);
        } else if (!device.basicStats.valid || (device.isGroup() && deviceChanged)) {
            QLabel *placeholder = new QLabel(i18n("Fetching energy history…"), this);
            placeholder->setAlignment(Qt::AlignCenter);
            m_historyBuilder.m_energyHistoryTabIndex = m_tabs->addTab(placeholder, i18n("Energy History"));
        } else {
            if (m_historyBuilder.m_groupHistoryMode)
                m_historyBuilder.buildEnergyHistoryChartStacked(m_historyBuilder.m_lastGroupMemberStats);
            else
                m_historyBuilder.buildEnergyHistoryChart(m_historyBuilder.m_lastEnergyStats);
        }
    }
    if (device.hasHumidity()) {
        m_powerBuilder.buildHumidityChart(device);
    }

    if (m_tabs->count() == 0) {
        QLabel *placeholder = new QLabel(i18n("No chart data available for this device."), this);
        placeholder->setAlignment(Qt::AlignCenter);
        m_tabs->addTab(placeholder, i18n("Info"));
    }
}

void ChartWidget::restoreTabAndApplyWindow(const QString &activeTabText)
{
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

    m_tabs->blockSignals(false);

    m_scrollAtEnd = true;
    updateScrollBar();
    applyTimeWindow();

    saveChartState();
    updateSliderVisibility();
}

// ---------------------------------------------------------------------------
// updateRollingCharts — delegates to builders
// ---------------------------------------------------------------------------

void ChartWidget::updateRollingCharts(const FritzDevice &device,
                                      const FritzDeviceList &memberDevices)
{
    m_device = device;

    m_tempBuilder.updateRolling(device, memberDevices);
    m_powerBuilder.updateRolling(device, memberDevices);
    m_gaugeBuilder.updateRolling(device);

    updateScrollBar();
    applyTimeWindow();
}

// ---------------------------------------------------------------------------
// Energy stats public API — skip-rebuild logic, then delegate to builder
// ---------------------------------------------------------------------------

// File-scope helpers for skip-rebuild detection.
static QList<double> energyValuesForGrid(const DeviceBasicStats &stats, int grid)
{
    for (const StatSeries &ss : stats.energy)
        if (ss.grid == grid)
            return ss.values;
    return {};
}

static QList<double> groupEnergyValuesForGrid(
    const QList<QPair<QString, DeviceBasicStats>> &memberStats, int grid)
{
    QList<double> combined;
    for (const auto &pair : memberStats)
        combined += energyValuesForGrid(pair.second, grid);
    return combined;
}

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
    m_historyBuilder.energyError().clear();

    int currentAvailableGrids = computeAvailableGrids(stats);

    if (m_historyBuilder.m_activeEnergyGrid > 0
            && energyValuesForGrid(stats, m_historyBuilder.m_activeEnergyGrid)
               == energyValuesForGrid(m_historyBuilder.m_lastEnergyStats, m_historyBuilder.m_activeEnergyGrid)
            && currentAvailableGrids == m_historyBuilder.m_lastAvailableGrids) {
        m_historyBuilder.m_lastEnergyStats = stats;
        return;
    }

    m_historyBuilder.m_lastEnergyStats    = stats;
    m_historyBuilder.m_lastAvailableGrids = currentAvailableGrids;

    if (!m_device.hasEnergyMeter())
        return;

    replaceEnergyHistoryTab([this, &stats]() {
        m_historyBuilder.buildEnergyHistoryChart(stats);
    });
}

void ChartWidget::updateGroupEnergyStats(const QList<QPair<QString, DeviceBasicStats>> &memberStats)
{
    m_historyBuilder.groupEnergyErrors().clear();

    int currentAvailableGrids = computeAvailableGridsForGroup(memberStats);

    if (m_historyBuilder.m_activeEnergyGrid > 0
            && groupEnergyValuesForGrid(memberStats, m_historyBuilder.m_activeEnergyGrid)
               == groupEnergyValuesForGrid(m_historyBuilder.m_lastGroupMemberStats, m_historyBuilder.m_activeEnergyGrid)
            && currentAvailableGrids == m_historyBuilder.m_lastAvailableGrids) {
        m_historyBuilder.m_lastGroupMemberStats = memberStats;
        m_historyBuilder.m_groupHistoryMode     = true;
        return;
    }

    m_historyBuilder.m_lastGroupMemberStats = memberStats;
    m_historyBuilder.m_groupHistoryMode     = true;
    m_historyBuilder.m_lastAvailableGrids   = currentAvailableGrids;

    if (!m_device.hasEnergyMeter())
        return;

    replaceEnergyHistoryTab([this, &memberStats]() {
        m_historyBuilder.buildEnergyHistoryChartStacked(memberStats);
    });
}

void ChartWidget::updateEnergyStatsError(const QString &error)
{
    m_historyBuilder.m_energyError = error;
    m_historyBuilder.showEnergyHistoryError(i18n("Error fetching energy history"), QStringList{error});
}

void ChartWidget::updateGroupEnergyStatsError(const QString &memberName, const QString &error)
{
    QString errorMsg = memberName.isEmpty()
        ? error
        : i18n("%1: %2", memberName, error);

    if (!m_historyBuilder.m_groupEnergyErrors.contains(errorMsg))
        m_historyBuilder.m_groupEnergyErrors.append(errorMsg);

    m_historyBuilder.showEnergyHistoryError(
        i18n("Error fetching energy history for group"), m_historyBuilder.m_groupEnergyErrors);
}

// ---------------------------------------------------------------------------
// Lock checkbox helper
// ---------------------------------------------------------------------------

void ChartWidget::createLockCheckBox(QPointer<QCheckBox> &checkBoxOut,
                                     bool &scaleLocked,
                                     double &lockedMin,
                                     double &lockedMax,
                                     const std::function<QValueAxis *()> &activeAxis)
{
    checkBoxOut = new QCheckBox(i18n("Lock Y scale"));
    checkBoxOut->setToolTip(i18n("When checked, the vertical axis range is frozen at its\n"
                                 "current scale and will not auto-adjust as new data arrives\n"
                                 "or the time window is scrolled."));
    checkBoxOut->setChecked(scaleLocked);
    connect(checkBoxOut, &QCheckBox::toggled, this,
            [this, &scaleLocked, &lockedMin, &lockedMax, activeAxis](bool locked) {
        scaleLocked = locked;
        if (locked) {
            QValueAxis *axis = activeAxis();
            if (axis) {
                lockedMin = axis->min();
                lockedMax = axis->max();
            }
        } else {
            applyTimeWindow();
        }
        saveChartState();
    });
}

// ---------------------------------------------------------------------------
// Energy history tab replacement helper
// ---------------------------------------------------------------------------

void ChartWidget::replaceEnergyHistoryTab(const std::function<void()> &buildFn)
{
    if (m_historyBuilder.m_energyHistoryTabIndex < 0
        || m_historyBuilder.m_energyHistoryTabIndex >= m_tabs->count())
        return;

    const QString savedTitle = plainTabText(m_tabs->tabText(m_tabs->currentIndex()));

    m_tabs->blockSignals(true);

    QWidget *old = m_tabs->widget(m_historyBuilder.m_energyHistoryTabIndex);
    m_tabs->removeTab(m_historyBuilder.m_energyHistoryTabIndex);
    delete old;
    m_historyBuilder.m_energyResCombo  = nullptr;
    m_historyBuilder.m_energyChartView = nullptr;

    buildFn();

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
    auto applyToBar = [&](QScrollBar *bar, int pageStepSec, int spanSec) {
        if (!bar) return;
        bar->blockSignals(true);
        bar->setPageStep(pageStepSec);
        bar->setRange(pageStepSec, spanSec);
        if (m_scrollAtEnd)
            bar->setValue(bar->maximum());
        bar->blockSignals(false);
    };

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
        for (QScrollBar *bar : {m_scrollBar, m_powerScrollBar}) {
            if (!bar) continue;
            bar->blockSignals(true);
            bar->setRange(0, 0);
            bar->setValue(0);
            bar->blockSignals(false);
        }
        return;
    }

    qint64 spanSec    = (nowMs - dataStartMs) / 1000;
    int pageStepSec   = static_cast<int>(qMin(windowMs() / 1000, spanSec));

    applyToBar(m_scrollBar,      pageStepSec, static_cast<int>(spanSec));
    applyToBar(m_powerScrollBar, pageStepSec, static_cast<int>(spanSec));
}

void ChartWidget::applyTimeWindow()
{
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    qint64 winMs = windowMs();

    QScrollBar *anyBar = m_scrollBar ? m_scrollBar : m_powerScrollBar;
    qint64 maxMs;
    if (m_scrollAtEnd || !anyBar || anyBar->maximum() == 0) {
        maxMs = nowMs;
    } else {
        qint64 spanSec     = anyBar->maximum();
        qint64 dataStartMs = nowMs - spanSec * 1000LL;
        maxMs = dataStartMs + (qint64)anyBar->value() * 1000LL;
        if (maxMs > nowMs) maxMs = nowMs;
    }
    qint64 minMs = maxMs - winMs;

    QDateTime minDt = QDateTime::fromMSecsSinceEpoch(minMs);
    QDateTime maxDt = QDateTime::fromMSecsSinceEpoch(maxMs);

    if (m_tempBuilder.m_tempAxisX) {
        m_tempBuilder.m_tempAxisX->setRange(minDt, maxDt);
        applyTimeAxisTicks(m_tempBuilder.m_tempAxisX, winMs);
        m_tempBuilder.rescaleYTemp(minMs, maxMs);
    }
    if (m_tempBuilder.m_groupTempAxisX) {
        m_tempBuilder.m_groupTempAxisX->setRange(minDt, maxDt);
        applyTimeAxisTicks(m_tempBuilder.m_groupTempAxisX, winMs);
        m_tempBuilder.rescaleYGroupTemp(minMs, maxMs);
    }
    if (m_powerBuilder.m_powerAxisX) {
        m_powerBuilder.m_powerAxisX->setRange(minDt, maxDt);
        applyTimeAxisTicks(m_powerBuilder.m_powerAxisX, winMs);
        m_powerBuilder.rescaleYPower(minMs, maxMs);
    }
}

// ---------------------------------------------------------------------------
// Event filter
// ---------------------------------------------------------------------------

bool ChartWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        QHelpEvent *he = static_cast<QHelpEvent *>(event);
        if (!m_historyBuilder.m_energyBarTooltip.isEmpty())
            QToolTip::showText(he->globalPos(), m_historyBuilder.m_energyBarTooltip);
        else
            QToolTip::hideText();
        return true;
    }

    // Forward mouse-move events from the comboOverlay to the chart viewport so
    // that Qt Charts' internal hover tracking can fire QBarSet::hovered signals.
    if (m_historyBuilder.m_energyChartView
        && watched != m_historyBuilder.m_energyChartView->viewport()
        && event->type() == QEvent::MouseMove) {
        QWidget *viewport = m_historyBuilder.m_energyChartView->viewport();
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
        QWidget *srcWidget = qobject_cast<QWidget *>(watched);
        QPoint viewportPos = srcWidget
            ? viewport->mapFromGlobal(srcWidget->mapToGlobal(me->pos()))
            : me->pos();
        QMouseEvent forwarded(QEvent::MouseMove, viewportPos,
                              viewport->mapToGlobal(viewportPos),
                              me->button(), me->buttons(), me->modifiers());
        QCoreApplication::sendEvent(viewport, &forwarded);
    }

    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Settings persistence — delegates builder-specific values
// ---------------------------------------------------------------------------

void ChartWidget::saveChartState() const
{
    QSettings s;
    s.setValue(QStringLiteral("ui/chartSlider"), m_windowComboIndex);
    if (m_tabs->currentIndex() >= 0) {
        const QString tabName = plainTabText(m_tabs->tabText(m_tabs->currentIndex()));
        s.setValue(QStringLiteral("ui/chartTab"), tabName);
    }

    m_tempBuilder.saveState();
    m_powerBuilder.saveState();
    m_historyBuilder.saveState();
}

void ChartWidget::loadChartState()
{
    QSettings s;
    if (s.contains(QStringLiteral("ui/chartSlider"))) {
        m_windowComboIndex = qBound(0, s.value(QStringLiteral("ui/chartSlider")).toInt(), 8);
    }

    m_tempBuilder.loadState();
    m_powerBuilder.loadState();
    m_historyBuilder.loadState();
}
