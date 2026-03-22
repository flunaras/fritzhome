#include "colorwidget.h"
#include "fritzapi.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QButtonGroup>
#include <QColor>
#include <QPainter>
#include "i18n_shim.h"

ColorWidget::ColorWidget(FritzApi *api, QWidget *parent)
    : DeviceWidget(api, parent)
{
    auto *layout = new QVBoxLayout(this);

    // Color swatch
    m_colorSwatch = new QFrame(this);
    m_colorSwatch->setFixedHeight(50);
    m_colorSwatch->setFrameShape(QFrame::Box);

    // Mode selector
    auto *modeLayout = new QHBoxLayout;
    m_hueMode = new QRadioButton(i18n("Hue / Saturation"), this);
    m_ctMode  = new QRadioButton(i18n("Color Temperature"), this);
    m_hueMode->setChecked(true);
    modeLayout->addWidget(m_hueMode);
    modeLayout->addWidget(m_ctMode);

    // Hue/Sat group
    auto *hsGrp = new QGroupBox(i18n("Hue && Saturation"), this);
    auto *hsForm = new QFormLayout(hsGrp);

    m_hueSlider = new QSlider(Qt::Horizontal, hsGrp);
    m_hueSlider->setRange(0, 359);
    m_hueLabel  = new QLabel("0", hsGrp);

    m_satSlider = new QSlider(Qt::Horizontal, hsGrp);
    m_satSlider->setRange(0, 255);
    m_satLabel  = new QLabel("0", hsGrp);

    m_applyColorBtn = new QPushButton(i18n("Apply Color"), hsGrp);
    m_applyColorBtn->setIcon(QIcon::fromTheme("dialog-ok-apply"));

    hsForm->addRow(i18n("Hue (0-359°):"),      m_hueSlider);
    hsForm->addRow(QString(),                   m_hueLabel);
    hsForm->addRow(i18n("Saturation (0-255):"), m_satSlider);
    hsForm->addRow(QString(),                   m_satLabel);
    hsForm->addRow(QString(),                   m_applyColorBtn);

    // Color temperature group
    auto *ctGrp = new QGroupBox(i18n("Color Temperature"), this);
    auto *ctForm = new QFormLayout(ctGrp);

    m_ctSlider = new QSlider(Qt::Horizontal, ctGrp);
    m_ctSlider->setRange(2700, 6500);  // Kelvin range
    m_ctLabel  = new QLabel("2700 K", ctGrp);

    m_applyCtBtn = new QPushButton(i18n("Apply Color Temperature"), ctGrp);
    m_applyCtBtn->setIcon(QIcon::fromTheme("dialog-ok-apply"));

    ctForm->addRow(i18n("Temperature (K):"), m_ctSlider);
    ctForm->addRow(QString(),                m_ctLabel);
    ctForm->addRow(QString(),                m_applyCtBtn);

    m_currentColorLabel = new QLabel(this);

    layout->addWidget(m_colorSwatch);
    layout->addWidget(m_currentColorLabel);
    layout->addLayout(modeLayout);
    layout->addWidget(hsGrp);
    layout->addWidget(ctGrp);
    layout->addStretch();

    connect(m_hueSlider, &QSlider::valueChanged, this, &ColorWidget::onHueSatChanged);
    connect(m_satSlider, &QSlider::valueChanged, this, &ColorWidget::onHueSatChanged);
    connect(m_ctSlider,  &QSlider::valueChanged, this, [this](int v) {
        m_ctLabel->setText(QString("%1 K").arg(v));
    });
    connect(m_applyColorBtn, &QPushButton::clicked, this, &ColorWidget::onApplyColor);
    connect(m_applyCtBtn,    &QPushButton::clicked, this, &ColorWidget::onApplyColorTemp);

    connect(m_hueMode, &QRadioButton::toggled, hsGrp,  &QGroupBox::setEnabled);
    connect(m_ctMode,  &QRadioButton::toggled, ctGrp,  &QGroupBox::setEnabled);
    ctGrp->setEnabled(false);
}

void ColorWidget::onHueSatChanged()
{
    int hue = m_hueSlider->value();
    int sat = m_satSlider->value();
    m_hueLabel->setText(QString("%1°").arg(hue));
    m_satLabel->setText(QString::number(sat));

    // Update swatch
    QColor c = QColor::fromHsv(hue, sat, 220);
    m_colorSwatch->setStyleSheet(QString("background-color: %1;").arg(c.name()));
}

void ColorWidget::onApplyColor()
{
    m_api->setColor(m_device.ain, m_hueSlider->value(), m_satSlider->value());
}

void ColorWidget::onApplyColorTemp()
{
    m_api->setColorTemperature(m_device.ain, m_ctSlider->value());
}

void ColorWidget::updateDevice(const FritzDevice &device)
{
    m_device = device;
    const auto &cs = device.colorStats;

    if (cs.valid) {
        if (cs.colorMode == "1") {
            // Hue/Sat mode
            m_hueMode->setChecked(true);
            m_hueSlider->blockSignals(true);
            m_satSlider->blockSignals(true);
            m_hueSlider->setValue(cs.hue);
            m_satSlider->setValue(cs.saturation);
            m_hueSlider->blockSignals(false);
            m_satSlider->blockSignals(false);
            m_hueLabel->setText(QString("%1°").arg(cs.hue));
            m_satLabel->setText(QString::number(cs.saturation));

            QColor c = QColor::fromHsv(cs.hue, cs.saturation, 220);
            m_colorSwatch->setStyleSheet(
                QString("background-color: %1;").arg(c.name()));
            m_currentColorLabel->setText(
                i18n("Current: Hue=%1°, Saturation=%2", cs.hue, cs.saturation));
        } else if (cs.colorMode == "4") {
            // Color temperature mode
            m_ctMode->setChecked(true);
            m_ctSlider->blockSignals(true);
            m_ctSlider->setValue(qBound(2700, cs.colorTemperature, 6500));
            m_ctSlider->blockSignals(false);
            m_ctLabel->setText(QString("%1 K").arg(cs.colorTemperature));
            m_currentColorLabel->setText(
                i18n("Current: Color Temperature=%1 K", cs.colorTemperature));

            // Approximate warm-to-cool color for swatch
            int ratio = qBound(0, (cs.colorTemperature - 2700) * 255 / (6500 - 2700), 255);
            QColor warm(255, 200, 100);
            QColor cool(180, 210, 255);
            QColor blended(
                warm.red()   + (cool.red()   - warm.red())   * ratio / 255,
                warm.green() + (cool.green() - warm.green()) * ratio / 255,
                warm.blue()  + (cool.blue()  - warm.blue())  * ratio / 255
            );
            m_colorSwatch->setStyleSheet(
                QString("background-color: %1;").arg(blended.name()));
        }
    }
    setEnabled(device.present);
}
