#include "humiditysensorwidget.h"
#include <QVBoxLayout>
#include <QProgressBar>
#include <QFormLayout>
#include <QGroupBox>
#include "i18n_shim.h"

HumiditySensorWidget::HumiditySensorWidget(FritzApi *api, QWidget *parent)
    : DeviceWidget(api, parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *grp = new QGroupBox(i18n("Humidity Sensor"), this);
    auto *form = new QFormLayout(grp);

    m_humidityLabel = new QLabel("--", grp);
    m_humidityLabel->setStyleSheet("font-size: 18pt; font-weight: bold;");
    m_statusLabel = new QLabel(grp);

    form->addRow(i18n("Relative Humidity:"), m_humidityLabel);
    form->addRow(i18n("Comfort Level:"),     m_statusLabel);

    layout->addWidget(grp);
    layout->addStretch();
}

void HumiditySensorWidget::updateDevice(const FritzDevice &device)
{
    m_device = device;
    const auto &hs = device.humidityStats;

    if (hs.valid && hs.humidity >= 0) {
        m_humidityLabel->setText(QString("%1 %RH").arg(hs.humidity));

        // Provide comfort assessment
        if (hs.humidity < 30) {
            m_statusLabel->setText(i18n("Too Dry"));
            m_statusLabel->setStyleSheet("color: orange;");
        } else if (hs.humidity > 70) {
            m_statusLabel->setText(i18n("Too Humid"));
            m_statusLabel->setStyleSheet("color: orange;");
        } else {
            m_statusLabel->setText(i18n("Comfortable (30-70%)"));
            m_statusLabel->setStyleSheet("color: green;");
        }
    } else {
        m_humidityLabel->setText(i18n("n/a"));
        m_statusLabel->clear();
    }
}
