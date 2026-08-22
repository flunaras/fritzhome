/// \file batterychartbuilder.cpp
/// \brief Implementation of BatteryChartBuilder — battery status display with color-coded levels.

#include "batterychartbuilder.h"
#include "chartutils.h"
#include "fritzdevice.h"
#include "i18n_shim.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QSettings>
#include <QProgressBar>
#include <QPainter>
#include <QPainterPath>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

BatteryChartBuilder::BatteryChartBuilder(QWidget &parentWidget)
    : m_parentWidget(parentWidget)
{
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QWidget *BatteryChartBuilder::buildBatteryChart(const FritzDevice &device)
{
    reset();

    m_batteryContainer = new QWidget(&m_parentWidget);
    QVBoxLayout *layout = new QVBoxLayout(m_batteryContainer);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    // Title
    QLabel *titleLabel = new QLabel(i18n("Battery Status"), m_batteryContainer);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    // Battery level display with icon
    QHBoxLayout *levelLayout = new QHBoxLayout();

    m_batteryIconLabel = new QLabel(m_batteryContainer);
    m_batteryIconLabel->setAlignment(Qt::AlignCenter);
    m_batteryIconLabel->setFixedSize(78, 44);
    levelLayout->addWidget(m_batteryIconLabel);

    QVBoxLayout *infoLayout = new QVBoxLayout();

    m_batteryLevelLabel = new QLabel(m_batteryContainer);
    QFont levelFont = m_batteryLevelLabel->font();
    levelFont.setPointSize(32);
    levelFont.setBold(true);
    m_batteryLevelLabel->setFont(levelFont);
    m_batteryLevelLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    infoLayout->addWidget(m_batteryLevelLabel);

    m_statusLabel = new QLabel(m_batteryContainer);
    QFont statusFont = m_statusLabel->font();
    statusFont.setPointSize(11);
    m_statusLabel->setFont(statusFont);
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    infoLayout->addWidget(m_statusLabel);

    levelLayout->addLayout(infoLayout);
    levelLayout->addStretch();
    layout->addLayout(levelLayout);

    // Battery low warning (if applicable)
    m_warningLabel = new QLabel(m_batteryContainer);
    QFont warningFont = m_warningLabel->font();
    warningFont.setPointSize(10);
    warningFont.setBold(true);
    m_warningLabel->setFont(warningFont);
    m_warningLabel->setAlignment(Qt::AlignLeft);
    m_warningLabel->setStyleSheet("QLabel { color: #d32f2f; padding: 10px; background-color: #ffebee; border-radius: 4px; }");
    m_warningLabel->setVisible(false);
    layout->addWidget(m_warningLabel);

    // External power notice (shown when the Fritz!Box reports the device is
    // currently running on USB/mains power rather than its battery).
    m_externalPowerLabel = new QLabel(m_batteryContainer);
    QFont externalFont = m_externalPowerLabel->font();
    externalFont.setPointSize(10);
    externalFont.setBold(true);
    m_externalPowerLabel->setFont(externalFont);
    m_externalPowerLabel->setAlignment(Qt::AlignLeft);
    m_externalPowerLabel->setStyleSheet("QLabel { color: #2e7d32; padding: 10px; background-color: #e8f5e9; border-radius: 4px; }");
    m_externalPowerLabel->setVisible(false);
    layout->addWidget(m_externalPowerLabel);

    // Progress bar for visual indication
    m_progressBar = new QProgressBar(m_batteryContainer);
    m_progressBar->setAlignment(Qt::AlignCenter);
    m_progressBar->setTextVisible(true);
    layout->addWidget(m_progressBar);

    layout->addStretch();

    // Update with initial data
    updateBattery(device);

    return m_batteryContainer;
}

void BatteryChartBuilder::updateBattery(const FritzDevice &device)
{
    if (!device.hasBattery() || !m_batteryContainer) {
        return;
    }

    const auto &bs = device.batteryStats;

    // Update level label and color
    if (bs.level >= 0) {
        QString levelStr = QString("%1%").arg(bs.level);
        m_batteryLevelLabel->setText(levelStr);
        m_batteryLevelLabel->setStyleSheet(
            QString("QLabel { color: %1; }").arg(colorForLevel(bs.level).name())
        );
    } else {
        m_batteryLevelLabel->setText(i18n("N/A"));
        m_batteryLevelLabel->setStyleSheet("QLabel { color: #999; }");
    }

    // Update icon
    m_batteryIconLabel->setPixmap(batteryIconPixmap(bs.level));

    // Update status text
    m_statusLabel->setText(statusText(bs.level, bs.low));

    // Update warning label — suppressed while running on external power,
    // since the reported battery level is not currently in use.
    if (bs.low && !bs.externallyPowered) {
        m_warningLabel->setText(i18n("⚠ Battery Low — Consider replacing soon"));
        m_warningLabel->setVisible(true);
    } else {
        m_warningLabel->setVisible(false);
    }

    // External power notice
    if (m_externalPowerLabel) {
        if (bs.externallyPowered) {
            m_externalPowerLabel->setText(i18n("🔌 Currently powered via USB/Mains (battery not in use)"));
            m_externalPowerLabel->setVisible(true);
        } else {
            m_externalPowerLabel->setVisible(false);
        }
    }

    // Update progress bar
    if (m_progressBar) {
        if (bs.level >= 0) {
            m_progressBar->setValue(bs.level);
            m_progressBar->setStyleSheet(
                QString("QProgressBar::chunk { background-color: %1; }").arg(colorForLevel(bs.level).name())
            );
        } else {
            m_progressBar->setValue(0);
        }
    }
}

void BatteryChartBuilder::reset()
{
    if (m_batteryContainer) {
        delete m_batteryContainer;
    }
    m_batteryContainer = nullptr;
    m_batteryLevelLabel = nullptr;
    m_batteryIconLabel = nullptr;
    m_statusLabel = nullptr;
    m_warningLabel = nullptr;
    m_progressBar = nullptr;
}

void BatteryChartBuilder::saveState() const
{
    // No state to save for battery display (stateless visualization)
}

void BatteryChartBuilder::loadState()
{
    // No state to restore
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

QColor BatteryChartBuilder::colorForLevel(int level) const
{
    return batteryColorForLevel(level);
}

QPixmap BatteryChartBuilder::batteryIconPixmap(int level) const
{
    // Paint our own icon instead of using emoji glyphs so stylesheet/system
    // emoji-color rendering cannot override the intended battery color.
    // Orientation is flipped 180°: terminal sits on the right, fill grows from the left.
    const int w = 78;
    const int h = 44;
    const int terminalVisible = 5; // visible width of the terminal nub past the body edge
    const int borderW = 2;         // pen width for body outline
    const int bodyW = w - terminalVisible - borderW;
    const int bodyH = h - 8;
    const int x = borderW / 2 + 1; // leave room for the pen stroke on the left
    const int y = (h - bodyH) / 2;

    QPixmap pm(w, h);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor fill = colorForLevel(level);
    QColor border = QColor("#404040");
    if (level < 0) {
        fill = QColor("#999999");
        border = QColor("#666666");
    }

    // 1. Battery body outline first (white-filled rounded rectangle with a dark stroke).
    QRect bodyRect(x, y, bodyW, bodyH);
    p.setPen(QPen(border, borderW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(bodyRect, 3, 3);

    // 2. Inner fill based on level (left-aligned to match flipped terminal).
    const QRect inner = bodyRect.adjusted(3, 3, -3, -3);
    if (level >= 0) {
        const int clamped = qBound(0, level, 100);
        const int fillW = qMax(0, (inner.width() * clamped) / 100);
        if (fillW > 0) {
            QRectF fillRect(inner.x(), inner.y(), fillW, inner.height());
            QPainterPath fillPath;
            fillPath.addRoundedRect(fillRect, 1.5, 1.5);
            p.fillPath(fillPath, fill);
        }
    } else {
        // N/A state: diagonal hatch conveys unknown level.
        p.fillRect(inner, QBrush(fill, Qt::BDiagPattern));
    }

    // 3. Terminal nub drawn on top of the body's right edge: a solid filled
    //    rounded rectangle in the border color. Its left side sits exactly
    //    on the body's right edge so there is no gap; the body stroke at
    //    that point is hidden behind the nub. Result: a single continuous
    //    silhouette that reads as a battery.
    const int terminalH = bodyH / 2;
    const int terminalW = terminalVisible + borderW / 2;
    const int terminalX = bodyRect.right() - borderW / 2 + 1;
    const int terminalY = y + (bodyH - terminalH) / 2;
    QPainterPath termPath;
    termPath.addRoundedRect(QRectF(terminalX, terminalY, terminalW, terminalH), 1.5, 1.5);
    p.fillPath(termPath, border);

    return pm;
}

QString BatteryChartBuilder::statusText(int level, bool lowFlag) const
{
    return batteryStatusTextForLevel(level, lowFlag);
}
