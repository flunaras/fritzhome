#include "dimmerwidget.h"
#include "fritzapi.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include "i18n_shim.h"

DimmerWidget::DimmerWidget(FritzApi *api, QWidget *parent)
    : DeviceWidget(api, parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *grp = new QGroupBox(i18n("Dimmer Control"), this);
    auto *grpLayout = new QVBoxLayout(grp);

    m_levelLabel = new QLabel(i18n("Level: --"), grp);
    m_levelLabel->setStyleSheet("font-size: 14pt; font-weight: bold;");

    m_slider = new QSlider(Qt::Horizontal, grp);
    m_slider->setRange(0, 100);
    m_slider->setTickInterval(10);
    m_slider->setTickPosition(QSlider::TicksBelow);

    auto *spinLayout = new QHBoxLayout;
    m_spinBox = new QSpinBox(grp);
    m_spinBox->setRange(0, 100);
    m_spinBox->setSuffix("%");
    m_setBtn  = new QPushButton(i18n("Set Level"), grp);
    m_setBtn->setIcon(QIcon::fromTheme("dialog-ok-apply"));

    spinLayout->addWidget(m_spinBox);
    spinLayout->addWidget(m_setBtn);

    auto *btnLayout = new QHBoxLayout;
    m_onBtn  = new QPushButton(i18n("On (100%)"),  grp);
    m_offBtn = new QPushButton(i18n("Off (0%)"), grp);
    m_onBtn->setIcon(QIcon::fromTheme("media-playback-start"));
    m_offBtn->setIcon(QIcon::fromTheme("media-playback-stop"));
    btnLayout->addWidget(m_offBtn);
    btnLayout->addWidget(m_onBtn);

    grpLayout->addWidget(m_levelLabel);
    grpLayout->addWidget(m_slider);
    grpLayout->addLayout(spinLayout);
    grpLayout->addLayout(btnLayout);

    layout->addWidget(grp);
    layout->addStretch();

    connect(m_slider, &QSlider::valueChanged, this, &DimmerWidget::onSliderChanged);
    connect(m_setBtn, &QPushButton::clicked,  this, &DimmerWidget::onSetLevel);
    connect(m_onBtn,  &QPushButton::clicked,  this, [this]() {
        m_api->setLevelPercentage(m_device.ain, 100);
    });
    connect(m_offBtn, &QPushButton::clicked,  this, [this]() {
        m_api->setLevelPercentage(m_device.ain, 0);
    });
}

void DimmerWidget::onSliderChanged(int value)
{
    m_spinBox->blockSignals(true);
    m_spinBox->setValue(value);
    m_spinBox->blockSignals(false);
}

void DimmerWidget::onSetLevel()
{
    m_api->setLevelPercentage(m_device.ain, m_spinBox->value());
}

void DimmerWidget::updateDevice(const FritzDevice &device)
{
    m_device = device;
    const auto &dim = device.dimmerStats;

    if (dim.valid) {
        int pct = dim.levelPercent;
        m_levelLabel->setText(i18n("Level: %1%", pct));
        m_slider->blockSignals(true);
        m_slider->setValue(pct);
        m_slider->blockSignals(false);
        m_spinBox->blockSignals(true);
        m_spinBox->setValue(pct);
        m_spinBox->blockSignals(false);
    }
    setEnabled(device.present);
}
