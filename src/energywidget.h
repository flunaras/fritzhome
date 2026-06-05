#pragma once

#include "devicewidget.h"
#include <QLabel>
#include <QCheckBox>

class EnergyWidget : public DeviceWidget
{
    Q_OBJECT
public:
    explicit EnergyWidget(FritzApi *api, QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device) override;

private:
    QLabel    *m_powerLabel;
    QLabel    *m_energyLabel;
    QLabel    *m_voltageLabel;
    QCheckBox *m_producerCheckBox;  ///< "Power producer" — mutually exclusive with nativeNetPower
    QCheckBox *m_nativeNetCheckBox; ///< "Native net power meter" — mutually exclusive with isProducer
};
