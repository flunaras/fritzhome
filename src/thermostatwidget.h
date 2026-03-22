#pragma once

#include "devicewidget.h"
#include <QLabel>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QPushButton>

class ThermostatWidget : public DeviceWidget
{
    Q_OBJECT
public:
    explicit ThermostatWidget(FritzApi *api, QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device) override;

private slots:
    void onSetTemp();
    void onSetOff();
    void onSetOn();

private:
    // Helper: AHA encoded value -> °C display
    static double ahaToC(int raw);
    // °C display -> AHA encoded value
    static int cToAha(double c);

    QLabel *m_currentTempLabel;
    QLabel *m_targetTempLabel;
    QLabel *m_batteryLabel;
    QLabel *m_windowLabel;
    QDoubleSpinBox *m_targetSpin;
    QPushButton *m_setBtn;
    QPushButton *m_offBtn;
    QPushButton *m_onBtn;
    QLabel *m_comfortLabel;
    QLabel *m_ecoLabel;
};
