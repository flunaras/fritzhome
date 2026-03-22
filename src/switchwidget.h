#pragma once

#include "devicewidget.h"
#include <QLabel>
#include <QToolButton>
#include <QGroupBox>

class SwitchWidget : public DeviceWidget
{
    Q_OBJECT
public:
    explicit SwitchWidget(FritzApi *api, QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device) override;
    void setMembers(const FritzDeviceList &members) override;

private:
    void rebuildMenus();

    QLabel      *m_statusLabel;
    QLabel      *m_lockedLabel;
    QToolButton *m_onBtn;
    QToolButton *m_offBtn;
    QToolButton *m_toggleBtn;

    FritzDeviceList m_members; // switch-capable group members; empty for single devices
};
