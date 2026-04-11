/// \file temperaturechartbuilder.cpp
/// \brief Implementation of TemperatureChartBuilder — builds and manages
///        Temperature chart tabs (single-device and group) for ChartWidget.

#include "temperaturechartbuilder.h"
#include "chartwidget.h"
#include "chartutils.h"
#include "i18n_shim.h"

#include <QTabWidget>
#include <QComboBox>
#include <QScrollBar>
#include <QDateTime>
#include <QSettings>
#include <QPen>
#include <limits>

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

// ── Constructor ─────────────────────────────────────────────────────────────

TemperatureChartBuilder::TemperatureChartBuilder(ChartWidget &owner)
    : m_owner(owner)
{
}

// ── Single-device temperature chart ─────────────────────────────────────────

void TemperatureChartBuilder::buildTemperatureChart(const FritzDevice &dev)
{
    QLineSeries *series = new QLineSeries();
    series->setName(i18n("Temperature"));

    {
        QList<QPointF> points;
        points.reserve(dev.temperatureHistory.size());
        for (const auto &point : dev.temperatureHistory)
            points.append(QPointF(point.first.toMSecsSinceEpoch(), point.second));
        series->replace(downsampleMinMax(points));
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
    m_owner.createLockCheckBox(m_tempLockCheckBox, m_tempScaleLocked,
                               m_lockedTempMin, m_lockedTempMax,
                               [this]() -> QValueAxis * { return m_tempAxisY; });

    // Format current temperature for the overlay label
    QString currentText;
    if (dev.temperature > -273.0)
        currentText = QString::number(dev.temperature, 'f', 1) + " °C";

    // Create a per-tab time-window combo and initialize from m_windowComboIndex
    QComboBox *tmpWindowCombo = new QComboBox();
    for (int i = 0; i < 9; ++i) tmpWindowCombo->addItem(i18n("Last %1", kWindowLabels[i]));
    tmpWindowCombo->setCurrentIndex(qBound(0, m_owner.m_windowComboIndex, 8));
    QObject::connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &m_owner,
            [this](int idx){ m_owner.m_windowComboIndex = idx; m_owner.onWindowComboChanged(idx); m_owner.saveChartState(); });
    m_owner.m_tabs->addTab(makeChartTab(chart, currentText, &m_tempValueLabel, m_owner.m_scrollBar, m_tempLockCheckBox, tmpWindowCombo), i18n("Temperature"));
}

// ── Group temperature chart ─────────────────────────────────────────────────

void TemperatureChartBuilder::buildGroupTemperatureChart(const FritzDeviceList &memberDevices)
{
    // Collect temperature-capable members
    FritzDeviceList tempMembers;
    for (const FritzDevice &m : memberDevices) {
        if (m.hasTemperature() || m.hasThermostat())
            tempMembers.append(m);
    }
    if (tempMembers.isEmpty())
        return;

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

        QColor c = kChartPalette[i % kChartPaletteSize];
        QPen pen(c);
        pen.setWidth(2);
        series->setPen(pen);

        {
            QList<QPointF> points;
            points.reserve(mem.temperatureHistory.size() + 1);
            for (const auto &pt : mem.temperatureHistory) {
                points.append(QPointF(pt.first.toMSecsSinceEpoch(), pt.second));
                minAll = qMin(minAll, pt.second);
                maxAll = qMax(maxAll, pt.second);
                anyData = true;
            }
            // Fallback single point if history is empty but current value is known
            if (mem.temperatureHistory.isEmpty() && mem.temperature > -273.0) {
                points.append(QPointF(QDateTime::currentDateTime().toMSecsSinceEpoch(), mem.temperature));
                minAll = qMin(minAll, mem.temperature);
                maxAll = qMax(maxAll, mem.temperature);
                anyData = true;
            }
            series->replace(downsampleMinMax(points));
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
    m_owner.createLockCheckBox(m_tempLockCheckBox, m_tempScaleLocked,
                               m_lockedTempMin, m_lockedTempMax,
                               [this]() -> QValueAxis * {
                                   return m_groupTempAxisY ? m_groupTempAxisY : m_tempAxisY;
                               });

    QComboBox *tmpWindowCombo = new QComboBox();
    for (int i = 0; i < 9; ++i) tmpWindowCombo->addItem(i18n("Last %1", kWindowLabels[i]));
    tmpWindowCombo->setCurrentIndex(qBound(0, m_owner.m_windowComboIndex, 8));
    QObject::connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &m_owner,
            [this](int idx){ m_owner.m_windowComboIndex = idx; m_owner.onWindowComboChanged(idx); m_owner.saveChartState(); });
    m_owner.m_tabs->addTab(makeChartTab(chart, QString(), nullptr, m_owner.m_scrollBar, m_tempLockCheckBox, tmpWindowCombo), i18n("Temperature"));
}

// ── Rolling update ──────────────────────────────────────────────────────────

void TemperatureChartBuilder::updateRolling(const FritzDevice &device,
                                            const FritzDeviceList &memberDevices)
{
    // Helper: rebuild a QXYSeries from a history list using bulk replace().
    // replace(QList<QPointF>) emits a single pointsReplaced signal instead of
    // N individual pointAdded signals, which is dramatically faster when the
    // history list grows large (up to ~17,000 points over 24 hours).
    auto reloadSeries = [](QXYSeries *series,
                           const QList<QPair<QDateTime, double>> &history,
                           double fallbackValue,
                           bool hasFallback,
                           qint64 fallbackTs)
    {
        if (!series)
            return;
        QList<QPointF> points;
        points.reserve(history.size() + 1);
        for (const auto &p : history)
            points.append(QPointF(p.first.toMSecsSinceEpoch(), p.second));
        if (history.isEmpty() && hasFallback)
            points.append(QPointF(fallbackTs, fallbackValue));
        series->replace(downsampleMinMax(points));
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
}

// ── Y-axis rescaling ────────────────────────────────────────────────────────

void TemperatureChartBuilder::rescaleYTemp(qint64 minMs, qint64 maxMs)
{
    if (!m_tempAxisY)
        return;

    if (m_tempScaleLocked) {
        m_tempAxisY->setRange(m_lockedTempMin, m_lockedTempMax);
        return;
    }

    double minVal, maxVal;
    if (!scanHistoryRange(m_owner.m_device.temperatureHistory, minMs, maxMs, minVal, maxVal)) {
        m_tempAxisY->setRange(0, 30);
        return;
    }

    double margin = qMax(1.0, (maxVal - minVal) * 0.1);
    applyAxisRange(m_tempAxisY, roundAxisRange(minVal - margin, maxVal + margin));
}

void TemperatureChartBuilder::rescaleYGroupTemp(qint64 minMs, qint64 maxMs)
{
    if (!m_groupTempAxisY || m_groupTempSeries.isEmpty())
        return;

    if (m_tempScaleLocked) {
        m_groupTempAxisY->setRange(m_lockedTempMin, m_lockedTempMax);
        return;
    }

    double minVal, maxVal;
    if (!scanSeriesRange(m_groupTempSeries, minMs, maxMs, minVal, maxVal)) {
        m_groupTempAxisY->setRange(0, 30);
        return;
    }
    double margin = qMax(1.0, (maxVal - minVal) * 0.1);
    applyAxisRange(m_groupTempAxisY, roundAxisRange(minVal - margin, maxVal + margin));
}

// ── Reset / teardown ────────────────────────────────────────────────────────

void TemperatureChartBuilder::reset()
{
    m_tempAxisX  = nullptr;
    m_tempAxisY  = nullptr;
    m_tempSeries = nullptr;
    m_tempValueLabel = nullptr;

    m_groupTempAxisX = nullptr;
    m_groupTempAxisY = nullptr;
    m_groupTempSeries.clear();
}

void TemperatureChartBuilder::nullifyWidgetPointers(QWidget *w)
{
    if (m_tempValueLabel && m_tempValueLabel->window() == w)
        m_tempValueLabel = nullptr;
    if (m_tempLockCheckBox && m_tempLockCheckBox->window() == w)
        m_tempLockCheckBox = nullptr;
}

void TemperatureChartBuilder::resetLock()
{
    m_tempScaleLocked = false;
    m_lockedTempMin   = 0.0;
    m_lockedTempMax   = 30.0;
}

// ── Settings persistence ────────────────────────────────────────────────────

void TemperatureChartBuilder::saveState() const
{
    QSettings s;
    s.setValue(QStringLiteral("chart/tempScaleLocked"), m_tempScaleLocked);
    s.setValue(QStringLiteral("chart/lockedTempMin"),   m_lockedTempMin);
    s.setValue(QStringLiteral("chart/lockedTempMax"),   m_lockedTempMax);
}

void TemperatureChartBuilder::loadState()
{
    QSettings s;
    m_tempScaleLocked = s.value(QStringLiteral("chart/tempScaleLocked"), false).toBool();
    m_lockedTempMin   = s.value(QStringLiteral("chart/lockedTempMin"),   0.0).toDouble();
    m_lockedTempMax   = s.value(QStringLiteral("chart/lockedTempMax"),   30.0).toDouble();
}
