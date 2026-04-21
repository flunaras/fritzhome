/// \file energygaugebuilder.cpp
/// \brief Implementation of EnergyGaugeBuilder — builds and manages the Energy
///        gauge tab and group pie chart for ChartWidget.
///
/// Also contains the PieTooltipFilter QObject class (hover tooltips for pie
/// slices) and the updatePieSliceLabels() static helper.

#include "energygaugebuilder.h"
#include "chartwidget.h"
#include "chartutils.h"
#include "i18n_shim.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QToolTip>
#include <QHelpEvent>
#include <QMouseEvent>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>

#include <cmath>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE
#endif

// ── Forward declaration of static helper ────────────────────────────────────

static void updatePieSliceLabels(QPieSeries *series, QPieSlice *explodedSlice);

// ── PieTooltipFilter ────────────────────────────────────────────────────────
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
            collapseExplodedSlice(series);
            return QObject::eventFilter(watched, event);
        }

        if (event->type() != QEvent::ToolTip && event->type() != QEvent::MouseMove)
            return QObject::eventFilter(watched, event);

        QWidget *w = qobject_cast<QWidget*>(watched);
        QPoint pos = cursorPosFromEvent(event);

        QChartView *cv = qobject_cast<QChartView*>(w->parentWidget());
        QPointF cursorLocal;
        QRectF pa;
        mapCursorToLocal(cv, w, pos, cursorLocal, pa);

        QPointF center = pa.center();
        QPointF delta = cursorLocal - center;
        const double dist = std::hypot(delta.x(), delta.y());

        // Disable exploding near the pie center (ambiguous mapping).
        const double centerRadius = qMin(pa.width(), pa.height()) * 0.25;
        if (dist < centerRadius) {
            collapseExplodedSlice(series);
            return false;
        }

        // Convert cursor angle from atan2 to Qt-pie convention (0°=top, CW).
        double angle = cursorAngle(delta);

        QPieSlice *foundSlice = findSliceAtAngle(series, angle);

        if (!foundSlice) {
            QToolTip::hideText();
            collapseExplodedSlice(series);
            return QObject::eventFilter(watched, event);
        }

        showSliceTooltip(foundSlice, series, w, pos);
        updateExplodeState(foundSlice, series);

        return true;
    }

private:
    // ── Helper: collapse any currently exploded slice ────────────────────
    void collapseExplodedSlice(QPieSeries *series)
    {
        if (m_lastSlice) {
            m_lastSlice->setExploded(false);
            m_lastSlice = nullptr;
            updatePieSliceLabels(series, nullptr);
        }
        m_candidateSlice = nullptr;
        m_candidateCount = 0;
    }

    // ── Helper: extract cursor position from event ──────────────────────
    static QPoint cursorPosFromEvent(QEvent *event)
    {
        if (event->type() == QEvent::ToolTip)
            return static_cast<QHelpEvent*>(event)->pos();
        return static_cast<QMouseEvent*>(event)->pos();
    }

    // ── Helper: map viewport cursor to chart-local coordinates ──────────
    // Also returns the plot area rectangle in the same coordinate space.
    static void mapCursorToLocal(QChartView *cv, QWidget *w, const QPoint &pos,
                                 QPointF &outCursorLocal, QRectF &outPlotArea)
    {
        if (cv && cv->chart()) {
            outPlotArea = cv->chart()->plotArea();
            QPointF scenePos = cv->mapToScene(pos);
            outCursorLocal = cv->chart()->mapFromScene(scenePos);
        } else {
            outPlotArea = QRectF(0, 0, w->width(), w->height());
            outCursorLocal = QPointF(pos);
        }
    }

    // ── Helper: convert delta to Qt-pie angle convention ────────────────
    // Screen atan2: 0°=right, +90°=down (Y-down).
    // Qt pie: 0°=top (12 o'clock), clockwise.
    // Conversion: qt_angle = atan2_angle + 90°
    static double cursorAngle(const QPointF &delta)
    {
        double atan2Deg = std::atan2(delta.y(), delta.x()) * 180.0 / M_PI;
        double angle = fmod(atan2Deg + 90.0, 360.0);
        if (angle < 0) angle += 360.0;
        return angle;
    }

    // ── Helper: find the pie slice whose angular range contains \a angle ──
    static QPieSlice *findSliceAtAngle(QPieSeries *series, double angle)
    {
        for (QPieSlice *s : series->slices()) {
            if (s->angleSpan() <= 0.0)
                continue;
            double sliceStart = fmod(s->startAngle(), 360.0);
            if (sliceStart < 0) sliceStart += 360.0;
            double sliceEnd = sliceStart + s->angleSpan();
            if (sliceEnd <= 360.0) {
                if (angle >= sliceStart && angle < sliceEnd)
                    return s;
            } else {
                if (angle >= sliceStart || angle < fmod(sliceEnd, 360.0))
                    return s;
            }
        }
        return nullptr;
    }

    // ── Helper: compose and show tooltip for the given slice ────────────
    static void showSliceTooltip(QPieSlice *slice, QPieSeries *series,
                                 QWidget *w, const QPoint &pos)
    {
        double total = 0.0;
        for (QPieSlice *s : series->slices()) total += s->value();

        double pct = (slice->value() / total) * 100.0;
        QString unit = (total >= 1000.0) ? i18n("kWh") : i18n("Wh");
        double scale = (total >= 1000.0) ? 0.001 : 1.0;
        QString name = slice->property("memberName").toString();
        if (name.isEmpty()) name = slice->label();
        QString tip = QString::fromUtf8("%1:\n%2 %3 (%4 %)")
                      .arg(name)
                      .arg(slice->value() * scale, 0, 'f', (unit == "kWh") ? 3 : 1)
                      .arg(unit)
                      .arg(pct, 0, 'f', 1);
        QToolTip::showText(w->mapToGlobal(pos), tip, w);
    }

    // ── Helper: hysteresis-based explode state update ────────────────────
    // Require two consecutive detections of the same slice before switching
    // the exploded slice, to avoid rapid flip-flopping near boundaries.
    void updateExplodeState(QPieSlice *slice, QPieSeries *series)
    {
        if (m_lastSlice == slice)
            return;                        // already exploded — nothing to do

        if (m_candidateSlice == slice) {
            ++m_candidateCount;
            const int kConfirmCount = 2;
            if (m_candidateCount >= kConfirmCount) {
                if (m_lastSlice) m_lastSlice->setExploded(false);
                slice->setExploded(true);
                m_lastSlice = slice;
                m_candidateSlice = nullptr;
                m_candidateCount = 0;
                updatePieSliceLabels(series, slice);
            }
        } else {
            m_candidateSlice = slice;
            m_candidateCount = 1;
        }
    }

    QPointer<QPieSeries> m_series;
    QPointer<QPieSlice>  m_lastSlice;
    QPointer<QPieSlice>  m_candidateSlice;
    int                  m_candidateCount;
};

// Q_OBJECT macro requires moc; include moc at end of file.
// (placed after all class definitions but before EOF)

// ── updatePieSliceLabels (static helper) ────────────────────────────────────

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

// ── Constructor ─────────────────────────────────────────────────────────────

EnergyGaugeBuilder::EnergyGaugeBuilder(ChartWidget &owner)
    : m_owner(owner)
{
}

// ── Build energy gauge tab ──────────────────────────────────────────────────

void EnergyGaugeBuilder::buildEnergyGauge(const FritzDevice &dev,
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
        // For a group, compute the net signed energy and power from members
        // (producers subtract, consumers add).  For a single device, negate
        // the raw value when the device is marked as a producer.
        double kwh;
        double power;
        QString energyHeading;
        if (dev.isGroup()) {
            double netEnergy = 0.0;
            double netPower  = 0.0;
            for (const FritzDevice &mem : memberDevices) {
                if (mem.hasEnergyMeter() && mem.energyStats.valid) {
                    const double sign = mem.isProducer ? -1.0 : 1.0;
                    netEnergy += sign * mem.energyStats.energy;
                    netPower  += sign * mem.energyStats.power;
                }
            }
            kwh          = netEnergy / 1000.0;
            power        = netPower;
            energyHeading = i18n("Net Energy (Group)");
        } else {
            const double sign = dev.isProducer ? -1.0 : 1.0;
            // The heading already communicates direction ("Produced" / "Consumed"),
            // so display the absolute kWh value to avoid a double-negative.
            kwh          = qAbs(sign * dev.energyStats.energy / 1000.0);
            power        = sign * dev.energyStats.power;
            energyHeading = dev.isProducer ? i18n("Total Energy Produced") : i18n("Total Energy Consumed");
        }
        vl->addWidget(makeLabel(energyHeading, 11, false));
        m_gaugeKwhLabel = makeLabel(QString::number(kwh, 'f', 3) + " kWh", 28, true);
        vl->addWidget(m_gaugeKwhLabel);
        vl->addSpacing(16);

        vl->addWidget(makeLabel(i18n("Current Power"), 11, false));
        m_gaugePowerLabel = makeLabel(QString::number(power, 'f', 1) + " W", 22, true);
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
                // Store signed energy: negative for producers, positive for consumers.
                // The pie slice will use the absolute magnitude for sizing, but
                // labels/tooltips will reflect the sign.
                const double sign = mem.isProducer ? -1.0 : 1.0;
                memberEnergies.append({ label, sign * mem.energyStats.energy });
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

            int idx = 0;
            double total = 0.0;
            for (const auto &p : memberEnergies) total += std::abs(p.second);
            for (const auto &p : memberEnergies) {
                // Use absolute value for slice size (QPieSeries requires positive values).
                // Store the signed value as a property so labels can show negatives.
                QPieSlice *slice = pie->append(p.first, std::abs(p.second));
                QColor c = kChartPalette[idx % kChartPaletteSize];
                c.setAlpha(200);
                slice->setColor(c);
                // Store raw member name and signed energy for label/tooltip composition.
                slice->setProperty("memberName", p.first);
                slice->setProperty("signedEnergy", p.second);
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

    m_owner.m_tabs->addTab(panel, i18n("Energy"));
}

// ── Update group pie labels ─────────────────────────────────────────────────

void EnergyGaugeBuilder::updateGroupPieLabels()
{
    if (!m_groupEnergyPie)
        return;

    // Compute total absolute Wh to decide on unit scaling (Wh vs kWh)
    // and to compute percentages.
    double totalWh = 0.0;
    for (QPieSlice *s : m_groupEnergyPie->slices()) {
        totalWh += s->value();  // s->value() is always the absolute magnitude
    }
    // Use kWh when typical absolute values exceed 1000 Wh
    const bool useKwh = (totalWh >= 1000.0);
    const double scale = useKwh ? 0.001 : 1.0;
    const QString unit = useKwh ? i18n("kWh") : i18n("Wh");

    // Update each slice label to include both signed value and percent.
    for (QPieSlice *s : m_groupEnergyPie->slices()) {
        // Use signedEnergy property for display (negative for producers).
        // Fall back to s->value() if property not set (should not happen).
        const double signedWh = s->property("signedEnergy").isValid()
                                ? s->property("signedEnergy").toDouble()
                                : s->value();
        const double absWh = s->value();  // absolute magnitude for percentage
        double pct = (totalWh > 0.0) ? (absWh / totalWh * 100.0) : 0.0;
        const double displayVal = signedWh * scale;
        QString name = s->property("memberName").toString();
        if (name.isEmpty()) name = s->label();
        // Example: "Living Room: 1.234 kWh (12.3 %)" or "-0.567 kWh (8.4 %)" for producer
        s->setLabel(QString::fromUtf8("%1: %2 %3 (%4 %)")
                    .arg(name)
                    .arg(displayVal, 0, 'f', useKwh ? 3 : 1)
                    .arg(unit)
                    .arg(pct, 0, 'f', 1));
    }

    // Delegate label visibility to the shared helper: no slice is exploded
    // at build time, so this applies the idle-mode overlap logic.
    updatePieSliceLabels(m_groupEnergyPie, nullptr);
}

// ── Rolling update ──────────────────────────────────────────────────────────

void EnergyGaugeBuilder::updateRolling(const FritzDevice &device,
                                        const FritzDeviceList &memberDevices)
{
    if (device.energyStats.valid) {
        double energy = device.energyStats.energy;
        double power  = device.energyStats.power;
        if (device.isGroup()) {
            // Compute net signed energy and power from members.
            double netEnergy = 0.0;
            double netPower  = 0.0;
            for (const FritzDevice &mem : memberDevices) {
                if (mem.hasEnergyMeter() && mem.energyStats.valid) {
                    const double sign = mem.isProducer ? -1.0 : 1.0;
                    netEnergy += sign * mem.energyStats.energy;
                    netPower  += sign * mem.energyStats.power;
                }
            }
            energy = netEnergy;
            power  = netPower;
        } else {
            const double sign = device.isProducer ? -1.0 : 1.0;
            energy = sign * device.energyStats.energy;
            power  = sign * device.energyStats.power;
        }
        if (m_gaugeKwhLabel)
            m_gaugeKwhLabel->setText(
                QString::number(energy / 1000.0, 'f', 3) + " kWh");
        if (m_gaugePowerLabel)
            m_gaugePowerLabel->setText(
                QString::number(power, 'f', 1) + " W");
        if (m_gaugeVoltageLabel && device.energyStats.voltage > 0)
            m_gaugeVoltageLabel->setText(
                QString::number(device.energyStats.voltage, 'f', 1) + " V");
    }
}

// ── Reset / teardown ────────────────────────────────────────────────────────

void EnergyGaugeBuilder::reset()
{
    m_gaugeKwhLabel     = nullptr;
    m_gaugePowerLabel   = nullptr;
    m_gaugeVoltageLabel = nullptr;

    m_groupEnergyPie        = nullptr;
    m_groupEnergyPieView    = nullptr;
    m_groupPieTooltipFilter = nullptr;
}

void EnergyGaugeBuilder::nullifyWidgetPointers(QWidget *w)
{
    if (m_gaugeKwhLabel && m_gaugeKwhLabel->window() == w)
        m_gaugeKwhLabel = nullptr;
    if (m_gaugePowerLabel && m_gaugePowerLabel->window() == w)
        m_gaugePowerLabel = nullptr;
    if (m_gaugeVoltageLabel && m_gaugeVoltageLabel->window() == w)
        m_gaugeVoltageLabel = nullptr;
    if (m_groupEnergyPieView && m_groupEnergyPieView->window() == w) {
        m_groupEnergyPieView    = nullptr;
        m_groupPieTooltipFilter = nullptr;
        m_groupEnergyPie        = nullptr;
    }
}

void EnergyGaugeBuilder::cleanupPieFilter()
{
    if (m_groupPieTooltipFilter) {
        if (m_groupEnergyPieView)
            m_groupEnergyPieView->viewport()->removeEventFilter(
                m_groupPieTooltipFilter);
        m_groupPieTooltipFilter = nullptr;
    }
    m_groupEnergyPie     = nullptr;
    m_groupEnergyPieView = nullptr;
}

bool EnergyGaugeBuilder::isPieOwner(QWidget *w) const
{
    return m_groupEnergyPieView && w
           && (w == m_groupEnergyPieView
               || w->isAncestorOf(m_groupEnergyPieView));
}

// ── moc include ─────────────────────────────────────────────────────────────
// PieTooltipFilter uses Q_OBJECT; moc output must be included.
#include "energygaugebuilder.moc"
