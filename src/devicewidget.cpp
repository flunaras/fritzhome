#include "devicewidget.h"

DeviceWidget::DeviceWidget(FritzApi *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
{}
