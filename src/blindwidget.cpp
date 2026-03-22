#include "blindwidget.h"
#include "fritzapi.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include "i18n_shim.h"

BlindWidget::BlindWidget(FritzApi *api, QWidget *parent)
    : DeviceWidget(api, parent)
{
    auto *layout = new QVBoxLayout(this);
    auto *grp = new QGroupBox(i18n("Blind / Roller Shutter"), this);
    auto *grpLayout = new QVBoxLayout(grp);

    m_modeLabel = new QLabel(i18n("Status: --"), grp);
    m_modeLabel->setStyleSheet("font-size: 14pt; font-weight: bold;");

    auto *btnLayout = new QHBoxLayout;
    m_openBtn  = new QPushButton(i18n("Open"),  grp);
    m_closeBtn = new QPushButton(i18n("Close"), grp);
    m_stopBtn  = new QPushButton(i18n("Stop"),  grp);

    m_openBtn->setIcon(QIcon::fromTheme("go-up"));
    m_closeBtn->setIcon(QIcon::fromTheme("go-down"));
    m_stopBtn->setIcon(QIcon::fromTheme("process-stop"));

    btnLayout->addWidget(m_openBtn);
    btnLayout->addWidget(m_stopBtn);
    btnLayout->addWidget(m_closeBtn);

    grpLayout->addWidget(m_modeLabel);
    grpLayout->addLayout(btnLayout);

    layout->addWidget(grp);
    layout->addStretch();

    connect(m_openBtn,  &QPushButton::clicked, this, [this]() {
        m_api->setBlind(m_device.ain, "open");
    });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        m_api->setBlind(m_device.ain, "close");
    });
    connect(m_stopBtn,  &QPushButton::clicked, this, [this]() {
        m_api->setBlind(m_device.ain, "stop");
    });
}

void BlindWidget::updateDevice(const FritzDevice &device)
{
    m_device = device;
    const auto &bl = device.blindStats;

    if (bl.valid) {
        QString mode = bl.mode;
        if (mode == "open")  mode = i18n("Opening");
        else if (mode == "close") mode = i18n("Closing");
        else if (mode == "stop")  mode = i18n("Stopped");
        m_modeLabel->setText(i18n("Status: %1", mode));
    }
    setEnabled(device.present);
}
