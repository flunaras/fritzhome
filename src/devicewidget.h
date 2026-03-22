#pragma once

#include <QWidget>
#include "fritzdevice.h"

class FritzApi;

/**
 * Base class for all device-specific control widgets.
 * Each subclass renders appropriate controls and charts for a device type.
 */
class DeviceWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceWidget(FritzApi *api, QWidget *parent = nullptr);
    virtual void updateDevice(const FritzDevice &device) = 0;
    // Called by MainWindow after updateDevice() when the device is a group.
    // Provides the resolved member device list so widgets can build per-member controls.
    // Default implementation is a no-op; only SwitchWidget overrides this.
    virtual void setMembers(const FritzDeviceList &members) { Q_UNUSED(members) }

protected:
    FritzApi *m_api;
    FritzDevice m_device;
};
