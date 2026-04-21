#include "energywidget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QCheckBox>
#include "i18n_shim.h"

EnergyWidget::EnergyWidget(FritzApi *api, QWidget *parent)
    : DeviceWidget(api, parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *grp = new QGroupBox(i18n("Energy Meter"), this);
    auto *grpLayout = new QVBoxLayout(grp);
    auto *form = new QFormLayout();

    m_powerLabel   = new QLabel("--", grp);
    m_energyLabel  = new QLabel("--", grp);
    m_voltageLabel = new QLabel("--", grp);

    m_powerLabel->setStyleSheet("font-size: 16pt; font-weight: bold;");

    form->addRow(i18n("Current Power:"), m_powerLabel);
    form->addRow(i18n("Total Energy:"),  m_energyLabel);
    form->addRow(i18n("Voltage:"),       m_voltageLabel);

    // Producer checkbox — always shown for EnergyWidget (energy-only devices always have a meter)
    m_producerCheckBox = new QCheckBox(i18n("This device is a power producer (negate power chart)"), grp);

    grpLayout->addLayout(form);
    grpLayout->addWidget(m_producerCheckBox);

    layout->addWidget(grp);
    layout->addStretch();

    // Producer checkbox: emit signal so MainWindow can persist and rebuild charts
    connect(m_producerCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        emit producerStatusChanged(m_device.ain, checked);
    });
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

    // Update checkbox state without triggering producerStatusChanged signal.
    // Hide the checkbox for group devices — groups have no single producer flag;
    // the per-member flag on each native device controls chart sign instead.
    m_producerCheckBox->setVisible(!device.isGroup());
    m_producerCheckBox->blockSignals(true);
    m_producerCheckBox->setChecked(device.isProducer);
    m_producerCheckBox->blockSignals(false);
}
