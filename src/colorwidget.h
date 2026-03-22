#pragma once

#include "devicewidget.h"
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QPushButton>
#include <QRadioButton>
#include <QFrame>

class ColorWidget : public DeviceWidget
{
    Q_OBJECT
public:
    explicit ColorWidget(FritzApi *api, QWidget *parent = nullptr);
    void updateDevice(const FritzDevice &device) override;

private slots:
    void onApplyColor();
    void onApplyColorTemp();
    void onHueSatChanged();

private:
    // Color swatch
    QFrame *m_colorSwatch;

    // Hue/Sat mode
    QRadioButton *m_hueMode;
    QSlider *m_hueSlider;
    QLabel *m_hueLabel;
    QSlider *m_satSlider;
    QLabel *m_satLabel;
    QPushButton *m_applyColorBtn;

    // Color temperature mode
    QRadioButton *m_ctMode;
    QSlider *m_ctSlider;
    QLabel *m_ctLabel;
    QPushButton *m_applyCtBtn;

    QLabel *m_currentColorLabel;
};
