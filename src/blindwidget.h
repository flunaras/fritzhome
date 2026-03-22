#pragma once

#include "devicewidget.h"
#include <QLabel>
#include <QPushButton>

class BlindWidget : public DeviceWidget
{
    Q_OBJECT
public:
    explicit BlindWidget(FritzApi *api, QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device) override;

private:
    QLabel *m_modeLabel;
    QPushButton *m_openBtn;
    QPushButton *m_closeBtn;
    QPushButton *m_stopBtn;
};
