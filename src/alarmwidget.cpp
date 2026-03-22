#include "alarmwidget.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include "i18n_shim.h"

AlarmWidget::AlarmWidget(FritzApi *api, QWidget *parent)
    : DeviceWidget(api, parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *grp = new QGroupBox(i18n("Alarm Sensor"), this);
    auto *form = new QFormLayout(grp);

    m_stateLabel     = new QLabel("--", grp);
    m_lastAlertLabel = new QLabel("--", grp);

    m_stateLabel->setStyleSheet("font-size: 18pt; font-weight: bold;");

    form->addRow(i18n("Alarm State:"), m_stateLabel);
    form->addRow(i18n("Last Alert:"),  m_lastAlertLabel);

    layout->addWidget(grp);
    layout->addStretch();
}

void AlarmWidget::updateDevice(const FritzDevice &device)
{
    m_device = device;
    const auto &al = device.alarmStats;

    if (al.valid) {
        if (al.triggered) {
            m_stateLabel->setText(i18n("ALARM TRIGGERED"));
            m_stateLabel->setStyleSheet(
                "font-size: 18pt; font-weight: bold; color: red;");
        } else {
            m_stateLabel->setText(i18n("OK - No Alarm"));
            m_stateLabel->setStyleSheet(
                "font-size: 18pt; font-weight: bold; color: green;");
        }
    }
}
