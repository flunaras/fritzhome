#include "thermostatwidget.h"
#include "fritzapi.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QProgressBar>
#include "i18n_shim.h"

ThermostatWidget::ThermostatWidget(FritzApi *api, QWidget *parent)
    : DeviceWidget(api, parent)
{
    auto *layout = new QVBoxLayout(this);

    // Current readings
    auto *readingsGrp = new QGroupBox(i18n("Current Readings"), this);
    auto *readForm = new QFormLayout(readingsGrp);

    m_currentTempLabel = new QLabel("--", readingsGrp);
    m_currentTempLabel->setStyleSheet("font-size: 18pt; font-weight: bold;");
    m_batteryLabel = new QLabel("--", readingsGrp);
    m_windowLabel  = new QLabel("--", readingsGrp);
    m_comfortLabel = new QLabel("--", readingsGrp);
    m_ecoLabel     = new QLabel("--", readingsGrp);

    readForm->addRow(i18n("Current Temp:"), m_currentTempLabel);
    readForm->addRow(i18n("Battery:"),      m_batteryLabel);
    readForm->addRow(i18n("Window:"),       m_windowLabel);
    readForm->addRow(i18n("Comfort Temp:"), m_comfortLabel);
    readForm->addRow(i18n("Eco Temp:"),     m_ecoLabel);

    // Controls
    auto *ctrlGrp = new QGroupBox(i18n("Set Target Temperature"), this);
    auto *ctrlLayout = new QVBoxLayout(ctrlGrp);

    m_targetTempLabel = new QLabel(i18n("Target: --"), ctrlGrp);
    m_targetTempLabel->setStyleSheet("font-size: 14pt;");

    auto *spinLayout = new QHBoxLayout;
    m_targetSpin = new QDoubleSpinBox(ctrlGrp);
    m_targetSpin->setRange(8.0, 28.0);
    m_targetSpin->setSingleStep(0.5);
    m_targetSpin->setSuffix(" °C");
    m_targetSpin->setDecimals(1);
    m_setBtn = new QPushButton(i18n("Set"), ctrlGrp);
    m_setBtn->setIcon(QIcon::fromTheme("dialog-ok-apply"));

    spinLayout->addWidget(m_targetSpin);
    spinLayout->addWidget(m_setBtn);

    auto *modeLayout = new QHBoxLayout;
    m_offBtn = new QPushButton(i18n("Off"),        ctrlGrp);
    m_onBtn  = new QPushButton(i18n("On (Comfort)"), ctrlGrp);
    m_offBtn->setIcon(QIcon::fromTheme("media-playback-stop"));
    m_onBtn->setIcon(QIcon::fromTheme("media-playback-start"));
    modeLayout->addWidget(m_offBtn);
    modeLayout->addWidget(m_onBtn);

    ctrlLayout->addWidget(m_targetTempLabel);
    ctrlLayout->addLayout(spinLayout);
    ctrlLayout->addLayout(modeLayout);

    layout->addWidget(readingsGrp);
    layout->addWidget(ctrlGrp);
    layout->addStretch();

    connect(m_setBtn, &QPushButton::clicked, this, &ThermostatWidget::onSetTemp);
    connect(m_offBtn, &QPushButton::clicked, this, &ThermostatWidget::onSetOff);
    connect(m_onBtn,  &QPushButton::clicked, this, &ThermostatWidget::onSetOn);
}

double ThermostatWidget::ahaToC(int raw)
{
    // AHA encoding: 16 = 8°C, 56 = 28°C (0.5°C steps)
    // 253 = off, 254 = on (comfort)
    if (raw == 253 || raw == 254) return -1.0;
    return 8.0 + (raw - 16) * 0.5;
}

int ThermostatWidget::cToAha(double c)
{
    // Clamp to valid range
    if (c < 8.0) c = 8.0;
    if (c > 28.0) c = 28.0;
    return qRound(16 + (c - 8.0) * 2);
}

void ThermostatWidget::updateDevice(const FritzDevice &device)
{
    m_device = device;
    const auto &hkr = device.thermostatStats;

    // Current temperature (tist: 0.5°C steps offset from 0°C)
    if (hkr.currentTemp > 0 && hkr.currentTemp < 255) {
        double tC = hkr.currentTemp / 2.0;
        m_currentTempLabel->setText(QString("%1 °C").arg(tC, 0, 'f', 1));
    } else {
        m_currentTempLabel->setText(i18n("n/a"));
    }

    // Target temperature
    int tsoll = hkr.targetTemp;
    if (tsoll == 253) {
        m_targetTempLabel->setText(i18n("Target: Off"));
    } else if (tsoll == 254) {
        m_targetTempLabel->setText(i18n("Target: On (Comfort)"));
    } else if (tsoll >= 16 && tsoll <= 56) {
        double tC = 8.0 + (tsoll - 16) * 0.5;
        m_targetTempLabel->setText(i18n("Target: %1 °C", QString::number(tC, 'f', 1)));
        m_targetSpin->setValue(tC);
    }

    // Battery
    if (hkr.battery >= 0) {
        m_batteryLabel->setText(QString("%1%").arg(hkr.battery));
        if (hkr.batteryLow)
            m_batteryLabel->setStyleSheet("color: red; font-weight: bold;");
        else
            m_batteryLabel->setStyleSheet("");
    }

    // Window
    m_windowLabel->setText(hkr.windowOpen ? i18n("Open (heating paused)") : i18n("Closed"));
    m_windowLabel->setStyleSheet(hkr.windowOpen ? "color: orange;" : "");

    // Comfort/Eco temperatures
    if (hkr.comfortTemp >= 16 && hkr.comfortTemp <= 56) {
        double cc = 8.0 + (hkr.comfortTemp - 16) * 0.5;
        m_comfortLabel->setText(QString("%1 °C").arg(cc, 0, 'f', 1));
    }
    if (hkr.ecotTemp >= 16 && hkr.ecotTemp <= 56) {
        double ec = 8.0 + (hkr.ecotTemp - 16) * 0.5;
        m_ecoLabel->setText(QString("%1 °C").arg(ec, 0, 'f', 1));
    }

    setEnabled(device.present);
}

void ThermostatWidget::onSetTemp()
{
    int val = cToAha(m_targetSpin->value());
    m_api->setThermostatTarget(m_device.ain, val);
}

void ThermostatWidget::onSetOff()
{
    m_api->setThermostatTarget(m_device.ain, 253);
}

void ThermostatWidget::onSetOn()
{
    m_api->setThermostatTarget(m_device.ain, 254);
}
