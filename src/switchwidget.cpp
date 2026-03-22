#include "switchwidget.h"
#include "fritzapi.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMenu>
#include <QAction>
#include <QPixmap>
#include <QPainter>
#include "i18n_shim.h"

// ── helpers ──────────────────────────────────────────────────────────────────

static QToolButton *makeActionButton(const QString &label, const QString &iconName, QWidget *parent)
{
    auto *btn = new QToolButton(parent);
    btn->setText(label);
    btn->setIcon(QIcon::fromTheme(iconName));
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setPopupMode(QToolButton::MenuButtonPopup);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return btn;
}

// Returns a 12×12 filled-circle icon: green when on, gray when off.
static QIcon memberStateIcon(bool on)
{
    const QColor normalColor   = on ? QColor(0, 160, 0)   : QColor(160, 160, 160);
    // Muted variant for the Disabled mode: still distinguishable as
    // green/grey but toned down to match the greyed-out menu text.
    const QColor disabledColor = on ? QColor(120, 180, 120) : QColor(190, 190, 190);

    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(normalColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(1, 1, 10, 10);
    p.end();

    QPixmap pmDis(12, 12);
    pmDis.fill(Qt::transparent);
    QPainter pd(&pmDis);
    pd.setRenderHint(QPainter::Antialiasing);
    pd.setBrush(disabledColor);
    pd.setPen(Qt::NoPen);
    pd.drawEllipse(1, 1, 10, 10);
    pd.end();

    QIcon icon;
    icon.addPixmap(pm,    QIcon::Normal);
    icon.addPixmap(pmDis, QIcon::Disabled);
    return icon;
}

// ── constructor ───────────────────────────────────────────────────────────────

SwitchWidget::SwitchWidget(FritzApi *api, QWidget *parent)
    : DeviceWidget(api, parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *grp = new QGroupBox(i18n("Switch Control"), this);
    auto *grpLayout = new QVBoxLayout(grp);

    m_statusLabel = new QLabel(i18n("Status: Unknown"), grp);
    m_statusLabel->setStyleSheet("font-size: 16pt; font-weight: bold;");
    m_lockedLabel = new QLabel(grp);

    auto *btnLayout = new QHBoxLayout;
    m_onBtn     = makeActionButton(i18n("Turn On"),  QStringLiteral("media-playback-start"), grp);
    m_offBtn    = makeActionButton(i18n("Turn Off"), QStringLiteral("media-playback-stop"),  grp);
    m_toggleBtn = makeActionButton(i18n("Toggle"),   QStringLiteral("view-refresh"),         grp);

    btnLayout->addWidget(m_onBtn);
    btnLayout->addWidget(m_offBtn);
    btnLayout->addWidget(m_toggleBtn);

    grpLayout->addWidget(m_statusLabel);
    grpLayout->addWidget(m_lockedLabel);
    grpLayout->addLayout(btnLayout);

    layout->addWidget(grp);
    layout->addStretch();

    // Main-button clicks → act on the group/device AIN
    connect(m_onBtn,     &QToolButton::clicked, this, [this]() {
        m_api->setSwitchOn(m_device.ain);
    });
    connect(m_offBtn,    &QToolButton::clicked, this, [this]() {
        m_api->setSwitchOff(m_device.ain);
    });
    connect(m_toggleBtn, &QToolButton::clicked, this, [this]() {
        // For groups, setSwitchToggle reads the raw "active" field (any-member-on)
        // from the internal device cache, which can diverge from the synthesized
        // state shown in the widget.  Use the widget's own m_device copy instead
        // so that "toggle" reliably inverts whatever state is currently displayed.
        if (m_device.switchStats.on)
            m_api->setSwitchOff(m_device.ain);
        else
            m_api->setSwitchOn(m_device.ain);
    });
}

// ── setMembers ────────────────────────────────────────────────────────────────

void SwitchWidget::setMembers(const FritzDeviceList &members)
{
    m_members.clear();
    for (const FritzDevice &m : members)
        if (m.hasSwitch())
            m_members.append(m);
    rebuildMenus();
}

// ── rebuildMenus ──────────────────────────────────────────────────────────────

void SwitchWidget::rebuildMenus()
{
    // Remove any existing menus from all three buttons
    m_onBtn->setMenu(nullptr);
    m_offBtn->setMenu(nullptr);
    m_toggleBtn->setMenu(nullptr);

    if (m_members.isEmpty())
        return; // single device: no dropdown needed

    // Build one menu per action; each entry targets a single member AIN
    auto *onMenu     = new QMenu(m_onBtn);
    auto *offMenu    = new QMenu(m_offBtn);
    auto *toggleMenu = new QMenu(m_toggleBtn);

    for (const FritzDevice &member : m_members) {
        const QString ain  = member.ain;
        const QString name = member.name;
        const bool memberOffline = !member.present;
        const bool memberLocked  = member.switchStats.locked;
        const bool memberOn      = member.switchStats.on;
        const QIcon stateIcon    = memberStateIcon(memberOn);

        // Turn On menu: disable members that are offline, already on, or locked
        auto *onAct = onMenu->addAction(stateIcon, name);
        onAct->setEnabled(!memberOffline && !memberLocked && !memberOn);
        connect(onAct, &QAction::triggered, this, [this, ain]() {
            m_api->setSwitchOn(ain);
        });

        // Turn Off menu: disable members that are offline, already off, or locked
        auto *offAct = offMenu->addAction(stateIcon, name);
        offAct->setEnabled(!memberOffline && !memberLocked && memberOn);
        connect(offAct, &QAction::triggered, this, [this, ain]() {
            m_api->setSwitchOff(ain);
        });

        auto *toggleAct = toggleMenu->addAction(stateIcon, name);
        toggleAct->setEnabled(!memberOffline && !memberLocked);
        connect(toggleAct, &QAction::triggered, this, [this, ain]() {
            m_api->setSwitchToggle(ain);
        });
    }

    m_onBtn->setMenu(onMenu);
    m_offBtn->setMenu(offMenu);
    m_toggleBtn->setMenu(toggleMenu);
}

// ── updateDevice ──────────────────────────────────────────────────────────────

void SwitchWidget::updateDevice(const FritzDevice &device)
{
    m_device = device;
    const auto &sw = device.switchStats;

    bool on = sw.on;
    if (sw.mixedSwitchState) {
        m_statusLabel->setText(i18n("PARTIAL"));
        m_statusLabel->setStyleSheet("font-size: 16pt; font-weight: bold; color: darkorange;");
    } else {
        m_statusLabel->setText(on ? i18n("ON") : i18n("OFF"));
        m_statusLabel->setStyleSheet(
            on ? "font-size: 16pt; font-weight: bold; color: green;"
               : "font-size: 16pt; font-weight: bold; color: gray;");
    }

    QString lockInfo;
    if (sw.locked)       lockInfo += i18n("Locked via Fritz!Box UI. ");
    if (sw.deviceLocked) lockInfo += i18n("Locked on device.");
    m_lockedLabel->setText(lockInfo);
    m_lockedLabel->setVisible(!lockInfo.isEmpty());

    // deviceLocked is the physical button lock on the plug — it does NOT
    // prevent remote control via the API.  Only sw.locked (the Fritz!Box
    // UI "API lock") blocks remote switching.
    bool canControl = device.present && !sw.locked;
    // For groups, allOn/allOff are synthesized from members by MainWindow.
    // For single devices they are never set, so derive them from sw.on directly.
    const bool effectiveAllOn  = device.isGroup() ? sw.allOn  : sw.on;
    const bool effectiveAllOff = device.isGroup() ? sw.allOff : !sw.on;
    // Disable a button when all controllable members (or the single device) are
    // already in the target state — there is nothing for that action to do.
    m_onBtn->setEnabled(canControl && !effectiveAllOn);
    m_offBtn->setEnabled(canControl && !effectiveAllOff);
    // Use InstantPopup (dropdown-only, no direct-click action) when:
    //  • the group has mixed on/off states (toggle would be ambiguous), OR
    //  • any member is locked alongside a controllable one (a group toggle would
    //    always produce a mixed result since locked members cannot be changed).
    // In all other cases use MenuButtonPopup (split button: click = action, arrow = dropdown).
    const bool forceInstant = sw.mixedSwitchState || sw.hasLockedMembers;
    if (forceInstant) {
        m_toggleBtn->setEnabled(canControl);
        m_toggleBtn->setPopupMode(QToolButton::InstantPopup);
        QFont f = m_toggleBtn->font();
        f.setItalic(true);
        m_toggleBtn->setFont(f);
        m_toggleBtn->setToolTip(i18n("Members have different states — use the menu to toggle individually."));
    } else {
        m_toggleBtn->setEnabled(canControl);
        m_toggleBtn->setPopupMode(QToolButton::MenuButtonPopup);
        QFont f = m_toggleBtn->font();
        f.setItalic(false);
        m_toggleBtn->setFont(f);
        m_toggleBtn->setToolTip(QString());
    }
}
