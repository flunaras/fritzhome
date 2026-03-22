#pragma once

#include "devicewidget.h"
#include <QLabel>

class EnergyWidget : public DeviceWidget
{
    Q_OBJECT
public:
    explicit EnergyWidget(FritzApi *api, QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device) override;

private:
    QLabel *m_powerLabel;
    QLabel *m_energyLabel;
    QLabel *m_voltageLabel;
};
