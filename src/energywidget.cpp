#include "energywidget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include "i18n_shim.h"

EnergyWidget::EnergyWidget(FritzApi *api, QWidget *parent)
    : DeviceWidget(api, parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *grp = new QGroupBox(i18n("Energy Meter"), this);
    auto *form = new QFormLayout(grp);

    m_powerLabel   = new QLabel("--", grp);
    m_energyLabel  = new QLabel("--", grp);
    m_voltageLabel = new QLabel("--", grp);

    m_powerLabel->setStyleSheet("font-size: 16pt; font-weight: bold;");

    form->addRow(i18n("Current Power:"), m_powerLabel);
    form->addRow(i18n("Total Energy:"),  m_energyLabel);
    form->addRow(i18n("Voltage:"),       m_voltageLabel);

    layout->addWidget(grp);
    layout->addStretch();
}

void EnergyWidget::updateDevice(const FritzDevice &device)
{
    m_device = device;
    const auto &em = device.energyStats;

    if (em.valid) {
        m_powerLabel->setText(QString("%1 W").arg(em.power, 0, 'f', 1));
        m_energyLabel->setText(QString("%1 Wh  (%2 kWh)").arg(em.energy, 0, 'f', 0)
                                                          .arg(em.energy / 1000.0, 0, 'f', 3));
        m_voltageLabel->setText(QString("%1 V").arg(em.voltage, 0, 'f', 1));
    } else {
        m_powerLabel->setText(i18n("n/a"));
        m_energyLabel->setText(i18n("n/a"));
        m_voltageLabel->setText(i18n("n/a"));
    }
}
