#pragma once

#include "devicewidget.h"
#include <QLabel>

class HumiditySensorWidget : public DeviceWidget
{
    Q_OBJECT
public:
    explicit HumiditySensorWidget(FritzApi *api, QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device) override;

private:
    QLabel *m_humidityLabel;
    QLabel *m_statusLabel;
};
