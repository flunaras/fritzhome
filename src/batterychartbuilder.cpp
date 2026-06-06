/// \file batterychartbuilder.cpp
/// \brief Implementation of BatteryChartBuilder — battery status display with color-coded levels.

#include "batterychartbuilder.h"
#include "fritzdevice.h"
#include "i18n_shim.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QSettings>
#include <QProgressBar>

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
    QFont iconFont = m_batteryIconLabel->font();
    iconFont.setPointSize(48);
    m_batteryIconLabel->setFont(iconFont);
    m_batteryIconLabel->setAlignment(Qt::AlignCenter);
    m_batteryIconLabel->setMinimumWidth(80);
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
    m_batteryIconLabel->setText(batteryIcon(bs.level));
    m_batteryIconLabel->setStyleSheet(
        QString("QLabel { color: %1; }").arg(colorForLevel(bs.level).name())
    );

    // Update status text
    m_statusLabel->setText(statusText(bs.level, bs.low));

    // Update warning label
    if (bs.low) {
        m_warningLabel->setText(i18n("⚠ Battery Low — Consider replacing soon"));
        m_warningLabel->setVisible(true);
    } else {
        m_warningLabel->setVisible(false);
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
    if (level < 0) return QColor("#999999");  // gray for N/A
    if (level < 10) return QColor("#d32f2f"); // red: critical
    if (level < 50) return QColor("#f57c00"); // orange/yellow: low
    return QColor("#388e3c");                 // green: good
}

QString BatteryChartBuilder::batteryIcon(int level) const
{
    // Unicode battery icons from various ranges
    if (level < 0) return "🔋";              // generic battery
    if (level < 20) return "🪫";             // empty battery
    if (level < 40) return "🔋";             // low battery
    if (level < 60) return "🔋";             // medium battery
    if (level < 80) return "🔋";             // high battery
    return "🔋";                             // full battery
}

QString BatteryChartBuilder::statusText(int level, bool lowFlag) const
{
    if (level < 0)
        return i18n("Battery level not available");

    QString status;
    if (level < 10) {
        status = i18n("Critical — Replace immediately");
    } else if (level < 30) {
        status = i18n("Low battery — Replace soon");
    } else if (level < 50) {
        status = i18n("Low — Consider replacing");
    } else if (level < 70) {
        status = i18n("Fair — Monitor level");
    } else if (level < 90) {
        status = i18n("Good");
    } else {
        status = i18n("Excellent");
    }

    if (lowFlag) {
        status += i18n(" (Fritz!Box warning)");
    }

    return status;
}
