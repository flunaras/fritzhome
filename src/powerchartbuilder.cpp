/// \file powerchartbuilder.cpp
/// \brief Implementation of PowerChartBuilder — builds and manages
///        Power and Humidity chart tabs for ChartWidget.

#include "powerchartbuilder.h"
#include "chartwidget.h"
#include "chartutils.h"
#include "i18n_shim.h"

#include <QTabWidget>
#include <QComboBox>
#include <QScrollBar>
#include <QDateTime>
#include <QSettings>
#include <QPen>
#include <QMap>
#include <QVector>
#include <limits>

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

// ── Constructor ─────────────────────────────────────────────────────────────

PowerChartBuilder::PowerChartBuilder(ChartWidget &owner)
    : m_owner(owner)
{
}

// ── Power chart ─────────────────────────────────────────────────────────────

void PowerChartBuilder::buildPowerChart(const FritzDevice &dev,
                                        const FritzDeviceList &memberDevices)
{
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
    m_owner.createLockCheckBox(m_powerLockCheckBox, m_powerScaleLocked,
                               m_lockedPowerMin, m_lockedPowerMax,
                               [this]() -> QValueAxis * { return m_powerAxisY; });

    if (stackedMode) {

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
            QColor c = kChartPalette[i % kChartPaletteSize];
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
        m_owner.m_memberDevices = energyMembers;

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
        tmpWindowCombo->setCurrentIndex(qBound(0, m_owner.m_windowComboIndex, 8));
        QObject::connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &m_owner,
                [this](int idx){ m_owner.m_windowComboIndex = idx; m_owner.onWindowComboChanged(idx); m_owner.saveChartState(); });
        m_owner.m_tabs->addTab(makeChartTab(chart, currentText, &m_powerValueLabel, m_owner.m_powerScrollBar, m_powerLockCheckBox, tmpWindowCombo), i18n("Power"));

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
        tmpWindowCombo->setCurrentIndex(qBound(0, m_owner.m_windowComboIndex, 8));
        QObject::connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &m_owner,
                [this](int idx){ m_owner.m_windowComboIndex = idx; m_owner.onWindowComboChanged(idx); m_owner.saveChartState(); });
        m_owner.m_tabs->addTab(makeChartTab(chart, currentText, &m_powerValueLabel, m_owner.m_powerScrollBar, m_powerLockCheckBox, tmpWindowCombo), i18n("Power"));
    }
}

// ── Humidity chart ──────────────────────────────────────────────────────────

void PowerChartBuilder::buildHumidityChart(const FritzDevice &dev)
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

    m_owner.m_tabs->addTab(makeChartView(chart), i18n("Humidity"));
}

// ── Rolling update ──────────────────────────────────────────────────────────

void PowerChartBuilder::updateRolling(const FritzDevice &device,
                                      const FritzDeviceList &memberDevices)
{
    // Helper: rebuild a QXYSeries from a history list using clear+append.
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

        const int n = m_powerStackedUpper.size();

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
            m_owner.m_memberDevices = energyMembers;

            // Build timestamp union across all members
            QMap<qint64, QVector<double>> tsMap;
            for (int i = 0; i < n; ++i) {
                for (const auto &pt : m_owner.m_memberDevices.at(i).powerHistory) {
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
}

// ── Y-axis rescaling ────────────────────────────────────────────────────────

void PowerChartBuilder::rescaleYPower(qint64 minMs, qint64 maxMs)
{
    if (!m_powerAxisY)
        return;

    // When the scale is locked, restore the captured range and do nothing else.
    if (m_powerScaleLocked) {
        m_powerAxisY->setRange(m_lockedPowerMin, m_lockedPowerMax);
        return;
    }

    // Stacked mode: Y-max = sum of all member maxima within the window
    if (!m_powerStackedUpper.isEmpty() && !m_owner.m_memberDevices.isEmpty()) {
        // At each timestamp within the window, compute the cumulative sum.
        // Simplification: sum up the per-device max values within the window.
        double yMax = 0.0;
        bool found = false;

        // Build timestamp union again for the rescale window
        const int n = m_owner.m_memberDevices.size();
        QMap<qint64, QVector<double>> tsMap;
        for (int i = 0; i < n; ++i) {
            for (const auto &pt : m_owner.m_memberDevices.at(i).powerHistory) {
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
                for (const auto &pt : m_owner.m_memberDevices.at(i).powerHistory) {
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
    double minVal, maxVal;
    if (!scanHistoryRange(m_owner.m_device.powerHistory, minMs, maxMs, minVal, maxVal)) {
        m_powerAxisY->setRange(0, 10);
        return;
    }

    m_powerAxisY->setMin(0);
    applyAxisRange(m_powerAxisY, roundAxisRange(0.0, maxVal));
}

// ── Reset / teardown ────────────────────────────────────────────────────────

void PowerChartBuilder::reset()
{
    m_powerAxisX       = nullptr;
    m_powerAxisY       = nullptr;
    m_powerSeries      = nullptr;
    m_powerLowerSeries = nullptr;
    m_powerValueLabel  = nullptr;

    m_powerStackedUpper.clear();
    m_powerStackedLower.clear();

    m_humiditySeries = nullptr;
}

void PowerChartBuilder::nullifyWidgetPointers(QWidget *w)
{
    if (m_powerValueLabel && m_powerValueLabel->window() == w)
        m_powerValueLabel = nullptr;
    if (m_powerLockCheckBox && m_powerLockCheckBox->window() == w)
        m_powerLockCheckBox = nullptr;
}

void PowerChartBuilder::resetLock()
{
    m_powerScaleLocked = false;
    m_lockedPowerMin   = 0.0;
    m_lockedPowerMax   = 10.0;
}

// ── Settings persistence ────────────────────────────────────────────────────

void PowerChartBuilder::saveState() const
{
    QSettings s;
    s.setValue(QStringLiteral("chart/powerScaleLocked"), m_powerScaleLocked);
    s.setValue(QStringLiteral("chart/lockedPowerMin"),   m_lockedPowerMin);
    s.setValue(QStringLiteral("chart/lockedPowerMax"),   m_lockedPowerMax);
}

void PowerChartBuilder::loadState()
{
    QSettings s;
    m_powerScaleLocked = s.value(QStringLiteral("chart/powerScaleLocked"), false).toBool();
    m_lockedPowerMin   = s.value(QStringLiteral("chart/lockedPowerMin"),   0.0).toDouble();
    m_lockedPowerMax   = s.value(QStringLiteral("chart/lockedPowerMax"),   10.0).toDouble();
}
