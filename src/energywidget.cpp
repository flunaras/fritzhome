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

    // Power-role checkboxes — mutually exclusive (producer vs. native net power meter).
    m_producerCheckBox = new QCheckBox(i18n("Power producer"), grp);
    m_producerCheckBox->setToolTip(i18n("This device is a power producer (negates power/energy values in charts)"));
    m_nativeNetCheckBox = new QCheckBox(i18n("Native net power meter"), grp);
    m_nativeNetCheckBox->setToolTip(i18n("This device natively reports signed net power (positive=consuming, negative=producing)"));

    grpLayout->addLayout(form);
    grpLayout->addWidget(m_producerCheckBox);
    grpLayout->addWidget(m_nativeNetCheckBox);

    layout->addWidget(grp);
    layout->addStretch();

    // Power-role checkboxes are mutually exclusive.
    // When one is turned ON the other is unchecked and its *Changed signal is emitted.
    connect(m_producerCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_nativeNetCheckBox->blockSignals(true);
            m_nativeNetCheckBox->setChecked(false);
            m_nativeNetCheckBox->blockSignals(false);
            emit nativeNetPowerChanged(m_device.ain, false);
        }
        emit producerStatusChanged(m_device.ain, checked);
    });
    connect(m_nativeNetCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_producerCheckBox->blockSignals(true);
            m_producerCheckBox->setChecked(false);
            m_producerCheckBox->blockSignals(false);
            emit producerStatusChanged(m_device.ain, false);
        }
        emit nativeNetPowerChanged(m_device.ain, checked);
    });
}

void EnergyWidget::updateDevice(const FritzDevice &device)
{
    m_device = device;
    const auto &em = device.energyStats;

    if (em.valid) {
        if (em.powerValid)
            m_powerLabel->setText(QString("%1 W").arg(em.power, 0, 'f', 1));
        else
            m_powerLabel->setText(i18n("n/a"));
        m_energyLabel->setText(QString("%1 Wh  (%2 kWh)").arg(em.energy, 0, 'f', 0)
                                                          .arg(em.energy / 1000.0, 0, 'f', 3));
        m_voltageLabel->setText(QString("%1 V").arg(em.voltage, 0, 'f', 1));
    } else {
        m_powerLabel->setText(i18n("n/a"));
        m_energyLabel->setText(i18n("n/a"));
        m_voltageLabel->setText(i18n("n/a"));
    }

    // Update checkbox states without triggering signals.
    // Hide both for group devices — each member has its own per-device flags.
    const bool showPowerConfig = !device.isGroup();
    m_producerCheckBox->setVisible(showPowerConfig);
    m_nativeNetCheckBox->setVisible(showPowerConfig);
    if (showPowerConfig) {
        m_producerCheckBox->blockSignals(true);
        m_nativeNetCheckBox->blockSignals(true);
        m_producerCheckBox->setChecked(device.isProducer);
        m_nativeNetCheckBox->setChecked(device.nativeNetPower);
        m_producerCheckBox->blockSignals(false);
        m_nativeNetCheckBox->blockSignals(false);
    }
}
