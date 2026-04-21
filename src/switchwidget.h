#pragma once

#include "devicewidget.h"
#include <QLabel>
#include <QToolButton>
#include <QGroupBox>
#include <QCheckBox>

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
    QCheckBox   *m_producerCheckBox = nullptr; ///< "This device is a power producer" — only shown for energy-capable devices

    FritzDeviceList m_members; // switch-capable group members; empty for single devices
};
