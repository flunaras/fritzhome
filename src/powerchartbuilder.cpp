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

// ── Stacked chart helpers ────────────────────────────────────────────────────

/// Compute the (lowerVal, upperVal) bounds for layer \p i in a stacked chart
/// given the signed per-layer values at one timestamp.
///
/// Consumers (vals[i] >= 0) stack upward from 0 independently of producers.
/// Producers (vals[i] < 0) stack downward from 0 independently of consumers.
/// This prevents mixed consumer/producer groups from producing crossing bands.
static std::pair<double,double> stackedBounds(const QVector<double> &vals, int i)
{
    double consumerBase = 0.0;
    double producerBase = 0.0;
    for (int j = 0; j < i; ++j) {
        if (vals[j] >= 0.0)
            consumerBase += vals[j];
        else
            producerBase += vals[j];
    }
    double v = vals[i];
    if (v >= 0.0)
        return { consumerBase, consumerBase + v };
    else
        return { producerBase + v, producerBase };
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

         // Determine whether members have mixed producer/consumer roles.
         bool hasProducer = false, hasConsumer = false;
         for (const auto &m : energyMembers) {
             if (m.isProducer) hasProducer = true; else hasConsumer = true;
         }
         const bool hasMixedProducers = hasProducer && hasConsumer;

         QMap<qint64, QVector<double>> tsMap;
         for (int i = 0; i < n; ++i) {
             for (const auto &pt : energyMembers.at(i).powerHistory) {
                 qint64 ts = pt.first.toMSecsSinceEpoch();
                 if (!tsMap.contains(ts))
                     tsMap[ts] = QVector<double>(n, 0.0);
                 // Negate power for producer devices
                 double power = energyMembers.at(i).isProducer ? -pt.second : pt.second;
                 tsMap[ts][i] = power;
             }
         }
         QList<qint64> timestamps = tsMap.keys();

         double yMin = 0.0, yMax = 0.0;
         for (int i = 0; i < n; ++i) {
             QColor c = kChartPalette[i % kChartPaletteSize];
             QLineSeries *upper = new QLineSeries();
             QLineSeries *lower = new QLineSeries();

             QList<QPointF> upperPts, lowerPts;
             upperPts.reserve(timestamps.size());
             lowerPts.reserve(timestamps.size());
             for (qint64 ts : timestamps) {
                 const QVector<double> &vals = tsMap[ts];
                 auto [lowerVal, upperVal] = stackedBounds(vals, i);
                 upperPts.append(QPointF(ts, upperVal));
                 lowerPts.append(QPointF(ts, lowerVal));
                 yMax = qMax(yMax, upperVal);
                 yMin = qMin(yMin, lowerVal);
             }
             upper->replace(downsampleMinMax(upperPts));
             lower->replace(downsampleMinMax(lowerPts));

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

     // Net/effective power line: only shown when members have mixed producer/consumer
     // roles — otherwise all areas point the same direction and a net line adds nothing.
     if (hasMixedProducers) {
         QLineSeries *netLine = new QLineSeries();
         netLine->setName(i18n("Net"));
         QPen netPen(QColor(0, 0, 0));
         netPen.setWidth(2);
         netLine->setPen(netPen);

         QList<QPointF> netPts;
         netPts.reserve(timestamps.size());
         for (qint64 ts : timestamps) {
             const QVector<double> &vals = tsMap[ts];
             double net = 0.0;
             for (double v : vals) net += v;
             netPts.append(QPointF(ts, net));
         }
         netLine->replace(downsampleMinMax(netPts));

         chart->addSeries(netLine);
         netLine->attachAxis(axisX);
         netLine->attachAxis(axisY);
         m_powerNetSeries = netLine;
     }

     // Store energy members for rolling update
     m_owner.m_memberDevices = energyMembers;

         if (yMax > yMin || (yMax == 0.0 && yMin == 0.0)) {
             applyAxisRange(axisY, roundAxisRange(yMin, yMax));
         } else {
             axisY->setRange(yMin - 5.0, yMax + 5.0);
         }

        chart->legend()->show();

        // Format current power for the overlay label (group total).
        // For a stacked group, compute the signed sum across all energy members
        // so that producer devices (negative) reduce the displayed total.
        QString currentText;
        if (dev.energyStats.valid) {
            double groupTotal = 0.0;
            for (const FritzDevice &m : energyMembers)
                groupTotal += m.isProducer ? -m.energyStats.power : m.energyStats.power;
            currentText = QString::number(groupTotal, 'f', 1) + " W";
        }

        m_powerAxisX = axisX;
        m_powerAxisY = axisY;
        // m_powerSeries / m_powerLowerSeries stay nullptr in stacked mode
        QComboBox *tmpWindowCombo = new QComboBox();
        for (int i = 0; i < 9; ++i) tmpWindowCombo->addItem(i18n("Last %1", kWindowLabels[i]));
        tmpWindowCombo->setCurrentIndex(qBound(0, m_owner.m_windowComboIndex, 8));
        QObject::connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &m_owner,
                [this](int idx){ m_owner.m_windowComboIndex = idx; m_owner.onWindowComboChanged(idx); m_owner.saveChartState(); });
        QWidget *tab = makeChartTab(chart, currentText, &m_powerValueLabel, m_owner.m_powerScrollBar, m_powerLockCheckBox, tmpWindowCombo);
        if (m_owner.m_powerTabInsertIndex >= 0)
            m_owner.m_tabs->insertTab(m_owner.m_powerTabInsertIndex, tab, i18n("Power"));
        else
            m_owner.m_tabs->addTab(tab, i18n("Power"));

    } else {
         // Single-device (or group with <2 energy members): original area chart
         QLineSeries *series = new QLineSeries();
         series->setName(i18n("Power (W)"));

         {
             QList<QPointF> points;
             points.reserve(dev.powerHistory.size());
             for (const auto &point : dev.powerHistory) {
                 // Negate power for producer devices
                 double power = dev.isProducer ? -point.second : point.second;
                 points.append(QPointF(point.first.toMSecsSinceEpoch(), power));
             }
             series->replace(downsampleMinMax(points));
         }

         if (dev.powerHistory.isEmpty() && dev.energyStats.valid) {
             double power = dev.isProducer ? -dev.energyStats.power : dev.energyStats.power;
             series->append(QDateTime::currentDateTime().toMSecsSinceEpoch(), power);
         }

         // Shaded area under the curve (baseline is zero, or extends below if producer)
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
             double minP = 0.0, maxP = 0.0;
             for (const auto &p : dev.powerHistory) {
                 double power = dev.isProducer ? -p.second : p.second;
                 minP = qMin(minP, power);
                 maxP = qMax(maxP, power);
             }
             applyAxisRange(axisY, roundAxisRange(minP, maxP));
         }

        // Store axis/series pointers for in-place updates
        m_powerAxisX       = axisX;
        m_powerAxisY       = axisY;
        m_powerSeries      = series;
        m_powerLowerSeries = lower;

        // Format current power for the overlay label
        QString currentText;
        if (dev.energyStats.valid) {
            double power = dev.isProducer ? -dev.energyStats.power : dev.energyStats.power;
            currentText = QString::number(power, 'f', 1) + " W";
        }

        QComboBox *tmpWindowCombo = new QComboBox();
        for (int i = 0; i < 9; ++i) tmpWindowCombo->addItem(i18n("Last %1", kWindowLabels[i]));
        tmpWindowCombo->setCurrentIndex(qBound(0, m_owner.m_windowComboIndex, 8));
        QObject::connect(tmpWindowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &m_owner,
                [this](int idx){ m_owner.m_windowComboIndex = idx; m_owner.onWindowComboChanged(idx); m_owner.saveChartState(); });
        QWidget *tab = makeChartTab(chart, currentText, &m_powerValueLabel, m_owner.m_powerScrollBar, m_powerLockCheckBox, tmpWindowCombo);
        if (m_owner.m_powerTabInsertIndex >= 0)
            m_owner.m_tabs->insertTab(m_owner.m_powerTabInsertIndex, tab, i18n("Power"));
        else
            m_owner.m_tabs->addTab(tab, i18n("Power"));
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

    {
        QList<QPointF> points;
        points.reserve(dev.humidityHistory.size());
        for (const auto &point : dev.humidityHistory)
            points.append(QPointF(point.first.toMSecsSinceEpoch(), point.second));
        series->replace(downsampleMinMax(points));
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
    // Helper: rebuild a QXYSeries from a history list using bulk replace().
    // replace(QList<QPointF>) emits a single pointsReplaced signal instead of
    // N individual pointAdded signals, which is dramatically faster when the
    // history list grows large (up to ~17,000 points over 24 hours).
    // The result is downsampled via min/max-per-bucket envelope decimation so
    // that Qt Charts never renders more than kMaxSeriesPoints, keeping the
    // rendering cost bounded regardless of time window size.
    auto reloadSeries = [](QXYSeries *series,
                           const QList<QPair<QDateTime, double>> &history,
                           double fallbackValue,
                           bool hasFallback,
                           qint64 fallbackTs,
                           bool isProducer)
    {
        if (!series)
            return;
        QList<QPointF> points;
        points.reserve(history.size() + 1);
        for (const auto &p : history) {
            double power = isProducer ? -p.second : p.second;
            points.append(QPointF(p.first.toMSecsSinceEpoch(), power));
        }
        if (history.isEmpty() && hasFallback) {
            double power = isProducer ? -fallbackValue : fallbackValue;
            points.append(QPointF(fallbackTs, power));
        }
        series->replace(downsampleMinMax(points));
    };

    // --- Power (single-device or group fallback) ---
    {
        qint64 fallbackTs = QDateTime::currentMSecsSinceEpoch();
        bool hasFallback = device.energyStats.valid;
        reloadSeries(m_powerSeries, device.powerHistory,
                     device.energyStats.power, hasFallback, fallbackTs, device.isProducer);
        // Keep the lower (zero baseline) series in sync with the upper series
        // time range so QAreaSeries renders the fill correctly for all points.
        if (m_powerLowerSeries) {
            QList<QPointF> lowerPts;
            if (!device.powerHistory.isEmpty()) {
                lowerPts.append(QPointF(
                    device.powerHistory.first().first.toMSecsSinceEpoch(), 0));
                lowerPts.append(QPointF(
                    device.powerHistory.last().first.toMSecsSinceEpoch(), 0));
            } else if (hasFallback) {
                lowerPts.append(QPointF(fallbackTs - 3600000LL, 0));
                lowerPts.append(QPointF(fallbackTs, 0));
            }
            m_powerLowerSeries->replace(lowerPts);
        }
        if (m_powerSeries && m_powerValueLabel && device.energyStats.valid) {
            double power = device.isProducer ? -device.energyStats.power : device.energyStats.power;
            m_powerValueLabel->setText(
                QString::number(power, 'f', 1) + " W");
        }
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
            if (m_powerValueLabel && device.energyStats.valid) {
                double groupTotal = 0.0;
                for (const FritzDevice &m : energyMembers)
                    groupTotal += m.isProducer ? -m.energyStats.power : m.energyStats.power;
                m_powerValueLabel->setText(QString::number(groupTotal, 'f', 1) + " W");
            }
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
                     // Negate power for producer devices
                     double power = m_owner.m_memberDevices.at(i).isProducer ? -pt.second : pt.second;
                     tsMap[ts][i] = power;
                 }
             }

            // Rebuild each stacked layer in-place using bulk replace()
            const QList<qint64> timestamps = tsMap.keys();
            for (int i = 0; i < n; ++i) {
                QXYSeries *upper = m_powerStackedUpper.at(i);
                QXYSeries *lower = m_powerStackedLower.at(i);
                if (!upper || !lower)
                    continue;
                QList<QPointF> upperPts, lowerPts;
                upperPts.reserve(timestamps.size());
                lowerPts.reserve(timestamps.size());
                 for (qint64 ts : timestamps) {
                     const QVector<double> &vals = tsMap[ts];
                     auto [lowerVal, upperVal] = stackedBounds(vals, i);
                     upperPts.append(QPointF(ts, upperVal));
                     lowerPts.append(QPointF(ts, lowerVal));
                 }
                upper->replace(downsampleMinMax(upperPts));
                lower->replace(downsampleMinMax(lowerPts));
            }

            // Update net line
            if (m_powerNetSeries) {
                QList<QPointF> netPts;
                netPts.reserve(timestamps.size());
                for (qint64 ts : timestamps) {
                    const QVector<double> &vals = tsMap[ts];
                    double net = 0.0;
                    for (double v : vals) net += v;
                    netPts.append(QPointF(ts, net));
                }
                m_powerNetSeries->replace(downsampleMinMax(netPts));
            }

            // Update power label to show signed group total (producers negated)
            if (m_powerValueLabel && device.energyStats.valid) {
                double groupTotal = 0.0;
                for (const FritzDevice &m : m_owner.m_memberDevices)
                    groupTotal += m.isProducer ? -m.energyStats.power : m.energyStats.power;
                m_powerValueLabel->setText(QString::number(groupTotal, 'f', 1) + " W");
            }
         }
      }

      // --- Humidity ─────────────────────────────────────────────────────────
      {
          qint64 fallbackTs = QDateTime::currentMSecsSinceEpoch();
          bool hasFallback = device.humidityStats.valid;
          reloadSeries(m_humiditySeries, device.humidityHistory,
                       device.humidityStats.humidity, hasFallback, fallbackTs, false);  // isProducer=false (humidity is never negated)
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

    // Stacked mode: compute min/max from all stacked values within the window
    if (!m_powerStackedUpper.isEmpty() && !m_owner.m_memberDevices.isEmpty()) {
        double yMin = 0.0, yMax = 0.0;
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
                    // Negate power for producer devices
                    double power = m_owner.m_memberDevices.at(i).isProducer ? -pt.second : pt.second;
                    tsMap[ts][i] = power;
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
                    // Negate power for producer devices
                    double power = m_owner.m_memberDevices.at(i).isProducer ? -pt.second : pt.second;
                    tsMap[ts][i] = power;
                }
            }
        }
        // Compute min/max using the same split consumer/producer stacking logic
        for (auto it = tsMap.constBegin(); it != tsMap.constEnd(); ++it) {
            const QVector<double> &vals = it.value();
            for (int i = 0; i < n; ++i) {
                auto [lo, hi] = stackedBounds(vals, i);
                yMin = qMin(yMin, lo);
                yMax = qMax(yMax, hi);
            }
        }
        applyAxisRange(m_powerAxisY, roundAxisRange(yMin, yMax));
        return;
    }

    // Single-device mode
    double minVal, maxVal;
    if (!m_owner.m_device.powerHistory.isEmpty()) {
        minVal = 0.0;
        maxVal = 0.0;
        for (const auto &p : m_owner.m_device.powerHistory) {
            if (p.first.toMSecsSinceEpoch() >= minMs && p.first.toMSecsSinceEpoch() <= maxMs) {
                double power = m_owner.m_device.isProducer ? -p.second : p.second;
                minVal = qMin(minVal, power);
                maxVal = qMax(maxVal, power);
            }
        }
    } else {
        m_powerAxisY->setRange(-5, 10);
        return;
    }

    applyAxisRange(m_powerAxisY, roundAxisRange(minVal, maxVal));
}

// ── Reset / teardown ────────────────────────────────────────────────────────

void PowerChartBuilder::reset()
{
    m_powerAxisX       = nullptr;
    m_powerAxisY       = nullptr;
    m_powerSeries      = nullptr;
    m_powerLowerSeries = nullptr;
    m_powerValueLabel  = nullptr;
    m_powerLockCheckBox = nullptr;

    m_powerStackedUpper.clear();
    m_powerStackedLower.clear();
    m_powerNetSeries   = nullptr;

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
