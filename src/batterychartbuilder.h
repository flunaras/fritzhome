/// \file batterychartbuilder.h
/// \brief Battery status display builder — renders battery level with color and icons.

#pragma once

#include <QWidget>
#include <QPointer>
#include <QPixmap>

class FritzDevice;

/// Battery Status Chart Builder
/// 
/// Renders a clean battery status display with:
/// - Large battery level indicator with color coding
/// - Battery icon reflecting current level
/// - Status text (e.g., "Good", "Low", "Critical")
/// - Battery low warning if applicable
///
/// Color scheme:
/// - Green: Battery > 50% (good)
/// - Yellow: Battery 10-50% (low)
/// - Red: Battery < 10% (critical)
/// - Gray: Battery low flag set (warning from Fritz!Box)
class BatteryChartBuilder {
public:
    explicit BatteryChartBuilder(QWidget &parentWidget);

    /// Build the battery status chart for a device with battery support.
    /// Returns the widget to add to the tab widget.
    QWidget *buildBatteryChart(const FritzDevice &device);

    /// Update the battery display with new device data.
    void updateBattery(const FritzDevice &device);

    /// Reset the builder state (called when switching devices).
    void reset();

    /// Save/load builder state from QSettings.
    void saveState() const;
    void loadState();

private:
    QWidget &m_parentWidget;
    QPointer<QWidget> m_batteryContainer;
    QPointer<class QLabel> m_batteryLevelLabel;
    QPointer<class QLabel> m_batteryIconLabel;
    QPointer<class QLabel> m_statusLabel;
    QPointer<class QLabel> m_warningLabel;
    QPointer<class QLabel> m_externalPowerLabel;
    QPointer<class QProgressBar> m_progressBar;

    /// Return color for battery level: green > 50%, yellow 10-50%, red < 10%.
    QColor colorForLevel(int level) const;

    /// Render a battery icon pixmap whose fill color reflects normalized level.
    QPixmap batteryIconPixmap(int level) const;

    /// Return descriptive status text for the battery level.
    QString statusText(int level, bool lowFlag) const;
};
