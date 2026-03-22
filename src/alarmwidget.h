#pragma once

#include "devicewidget.h"
#include <QLabel>

class AlarmWidget : public DeviceWidget
{
    Q_OBJECT
public:
    explicit AlarmWidget(FritzApi *api, QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device) override;

private:
    QLabel *m_stateLabel;
    QLabel *m_lastAlertLabel;
};
