#pragma once

#include "devicewidget.h"
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>

class DimmerWidget : public DeviceWidget
{
    Q_OBJECT
public:
    explicit DimmerWidget(FritzApi *api, QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device) override;

private slots:
    void onSliderChanged(int value);
    void onSetLevel();

private:
    QLabel *m_levelLabel;
    QSlider *m_slider;
    QSpinBox *m_spinBox;
    QPushButton *m_setBtn;
    QPushButton *m_onBtn;
    QPushButton *m_offBtn;
};
