#include "mainwindow.h"
#include "i18n_shim.h"

#if HAVE_KF
#  include <KXmlGuiWindow>
#  include <KActionCollection>
#  include <KStandardAction>
#  include <KMessageBox>
#  include <KToolBar>
#else
#  include <QMessageBox>
#endif

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QTreeView>
#include <QStackedWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QIcon>
#include <QPixmap>
#include <QScrollArea>
#include <QDateTime>
#include <QTimer>
#include <QSettings>
#include <QSet>
#include <QCloseEvent>
#include <QShowEvent>
#include <QFontMetrics>
#include <QSpinBox>

#include "fritzapi.h"
#include "devicemodel.h"
#include "loginwindow.h"
#include "chartwidget.h"
#include "secretstore.h"
#include "localgroupmanager.h"
#include "localgroupdialog.h"

// Device-specific widgets
#include "switchwidget.h"
#include "thermostatwidget.h"
#include "energywidget.h"
#include "dimmerwidget.h"
#include "blindwidget.h"
#include "colorwidget.h"
#include "humiditysensorwidget.h"
#include "alarmwidget.h"

// QSettings key for the per-device producer flag (must match devicemodel.cpp).
// Full path written: "devices/<ain>/isProducer"
static const char *kSettingsKeyIsProducer = "isProducer";

// Indices into m_controlStack
enum PanelIndex {
    PanelEmpty       = 0,
    PanelSwitch      = 1,
    PanelThermostat  = 2,
    PanelEnergy      = 3,
    PanelDimmer      = 4,
    PanelBlind       = 5,
    PanelColor       = 6,
    PanelHumidity    = 7,
    PanelAlarm       = 8,
};

// ─────────────────────────────────────────────────────────────────────────────
// Returns the appropriate 32x32 icon pixmap for the heading of the details panel.
static QPixmap deviceHeadingPixmap(const FritzDevice &dev)
{
    return QIcon(dev.iconPath()).pixmap(32, 32);
}

// ─────────────────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
#if HAVE_KF
    : KXmlGuiWindow(parent)
#else
    : QMainWindow(parent)
#endif
    , m_api(new FritzApi(this))
    , m_model(new DeviceModel(this))
    , m_localGroupManager(new LocalGroupManager(this))
{
    setWindowTitle(i18n("Fritz!Box Smart Home"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("fritzhome"),
                                   QIcon(QStringLiteral(":/icons/fritzhome.svg"))));
    resize(1100, 680);

    // Allow dock widgets to be split both horizontally and vertically by
    // dragging them beside each other (not just stacking them).
    setDockNestingEnabled(true);

    // ── Dock widgets ──────────────────────────────────────────────────────────
    // Create all three docks first (each addDockWidget call places them), then
    // use splitDockWidget() to establish the default side-by-side layout:
    //   [ Devices | Device Control | Device Charts ]
    // This only applies on first launch; saveState()/restoreState() takes over
    // for subsequent sessions.
    setupDeviceTree();    // m_deviceDock  — placed in LeftDockWidgetArea
    setupControlPanel();  // m_controlDock — initially also Left, then split right
                          // m_chartDock   — initially also Left, then split right of control

    // Default layout: split control panel to the right of the device list,
    // then split chart dock to the right of the control panel.
    splitDockWidget(m_deviceDock,  m_controlDock, Qt::Horizontal);
    splitDockWidget(m_controlDock, m_chartDock,   Qt::Horizontal);

    setupStatusBar();
    setupActions();
    wireSignals();
    restoreSettings();

    // Show local groups immediately even before Fritz!Box connection
    if (!m_localGroupManager->groups().isEmpty())
        m_model->setLocalGroups(m_localGroupManager->groups(), {});
}

MainWindow::~MainWindow() = default;

// ── Constructor helpers ───────────────────────────────────────────────────────

void MainWindow::setupDeviceTree()
{
    QWidget *treeContainer = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(treeContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    m_deviceTree = new QTreeView(treeContainer);
    m_deviceTree->setModel(m_model);
    m_deviceTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTree->setAlternatingRowColors(true);
    m_deviceTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTree->setRootIsDecorated(true);
    m_deviceTree->setItemsExpandable(true);
    m_deviceTree->setUniformRowHeights(false);
    m_deviceTree->setSortingEnabled(false);  // no proxy model, keep insertion order
     m_deviceTree->setMinimumWidth(220);
     m_deviceTree->header()->setSectionResizeMode(QHeaderView::Interactive);
     m_deviceTree->header()->setMinimumSectionSize(50);
     leftLayout->addWidget(m_deviceTree, 1);

    // Polling interval row below the tree
    QHBoxLayout *intervalRow = new QHBoxLayout();
    intervalRow->setContentsMargins(4, 2, 4, 2);
    QLabel *intervalLabel = new QLabel(i18n("Refresh interval:"), treeContainer);
    m_intervalSpin = new QSpinBox(treeContainer);
    m_intervalSpin->setRange(2, 300);
    m_intervalSpin->setValue(m_pollingInterval);
    m_intervalSpin->setSuffix(i18n(" s"));
    m_intervalSpin->setToolTip(i18n("How often to refresh device states (2–300 seconds)"));
    intervalRow->addWidget(intervalLabel);
    intervalRow->addWidget(m_intervalSpin, 1);
    leftLayout->addLayout(intervalRow);

    // Wrap the tree panel in a dock widget so the user can float, move, or hide it.
    m_deviceDock = new QDockWidget(i18n("Devices"), this);
    m_deviceDock->setObjectName(QStringLiteral("DeviceListDock")); // required for saveState()
    m_deviceDock->setWidget(treeContainer);
    m_deviceDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                                  | Qt::TopDockWidgetArea);
    m_deviceDock->setFeatures(QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetFloatable
                              | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, m_deviceDock);
}

void MainWindow::setupControlPanel()
{
    // ── Device control dock ───────────────────────────────────────────────────
    // The heading (icon + device name) and the stacked device-type panels
    // are hosted in a dockable widget so the user can float or hide them.
    QWidget *controlContainer = new QWidget(this);
    QVBoxLayout *controlLayout = new QVBoxLayout(controlContainer);
    controlLayout->setContentsMargins(4, 4, 4, 4);
    controlLayout->setSpacing(6);

    // Device name heading (icon + text in a horizontal row)
    m_deviceIconLabel = new QLabel(controlContainer);
    m_deviceIconLabel->setFixedSize(32, 32);
    m_deviceIconLabel->setAlignment(Qt::AlignCenter);
    m_deviceIconLabel->hide();

    m_deviceNameLabel = new QLabel(controlContainer);
    m_deviceNameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    {
        QFont f = m_deviceNameLabel->font();
        f.setPointSize(f.pointSize() + 3);
        f.setBold(true);
        m_deviceNameLabel->setFont(f);
    }
    m_deviceNameLabel->setContentsMargins(4, 2, 4, 2);
    m_deviceNameLabel->hide();   // hidden until a device is selected

    QHBoxLayout *nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    nameRow->setSpacing(6);
    nameRow->addWidget(m_deviceIconLabel);
    nameRow->addWidget(m_deviceNameLabel, 1);
    controlLayout->addLayout(nameRow);

    // Control stack
    m_controlStack = new QStackedWidget(controlContainer);

    // 0: empty placeholder
    QLabel *emptyLabel = new QLabel(i18n("Select a device from the list."), m_controlStack);
    emptyLabel->setAlignment(Qt::AlignCenter);
    QFont placeholderFont = emptyLabel->font();
    placeholderFont.setItalic(true);
    emptyLabel->setFont(placeholderFont);
    m_controlStack->addWidget(emptyLabel);                              // 0

    // 1..N: device-specific panels (wrapped in scroll areas for safety)
    auto addScrolled = [&](QWidget *w) {
        QScrollArea *sa = new QScrollArea(m_controlStack);
        sa->setWidget(w);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        m_controlStack->addWidget(sa);
    };

    addScrolled(new SwitchWidget(m_api, m_controlStack));               // 1
    addScrolled(new ThermostatWidget(m_api, m_controlStack));           // 2
    addScrolled(new EnergyWidget(m_api, m_controlStack));               // 3
    addScrolled(new DimmerWidget(m_api, m_controlStack));               // 4
    addScrolled(new BlindWidget(m_api, m_controlStack));                // 5
    addScrolled(new ColorWidget(m_api, m_controlStack));                // 6
    addScrolled(new HumiditySensorWidget(m_api, m_controlStack));       // 7
    addScrolled(new AlarmWidget(m_api, m_controlStack));                // 8

    m_controlStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    // No fixed-height pinning needed — the dock widget manages vertical sizing.
    // (The old QSizePolicy::Fixed + setFixedHeight() approach was for a splitter
    // layout where we wanted the control panel to shrink-wrap; with docks the
    // user resizes the dock itself.)
    controlLayout->addWidget(m_controlStack);
    // Fill any remaining vertical space so the dock looks clean when floating.
    controlLayout->addStretch(1);

    m_controlDock = new QDockWidget(i18n("Device Control"), this);
    m_controlDock->setObjectName(QStringLiteral("DeviceControlDock")); // required for saveState()
    m_controlDock->setWidget(controlContainer);
    m_controlDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                                   | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    m_controlDock->setFeatures(QDockWidget::DockWidgetMovable
                               | QDockWidget::DockWidgetFloatable
                               | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, m_controlDock);

    // ── Charts dock ───────────────────────────────────────────────────────────
    // The chart area is also dockable so the user can float, resize, or hide it.
    // It uses the same icon + name header pattern as the control dock so the
    // selected device is always identified in both panels.
    QWidget     *chartContainer = new QWidget(this);
    QVBoxLayout *chartLayout    = new QVBoxLayout(chartContainer);
    chartLayout->setContentsMargins(4, 4, 4, 4);
    chartLayout->setSpacing(6);

    m_chartIconLabel = new QLabel(chartContainer);
    m_chartIconLabel->setFixedSize(32, 32);
    m_chartIconLabel->setAlignment(Qt::AlignCenter);
    m_chartIconLabel->hide();

    m_chartNameLabel = new QLabel(chartContainer);
    m_chartNameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    {
        QFont f = m_chartNameLabel->font();
        f.setPointSize(f.pointSize() + 3);
        f.setBold(true);
        m_chartNameLabel->setFont(f);
    }
    m_chartNameLabel->setContentsMargins(4, 2, 4, 2);
    m_chartNameLabel->hide();

    QHBoxLayout *chartNameRow = new QHBoxLayout();
    chartNameRow->setContentsMargins(0, 0, 0, 0);
    chartNameRow->setSpacing(6);
    chartNameRow->addWidget(m_chartIconLabel);
    chartNameRow->addWidget(m_chartNameLabel, 1);
    chartLayout->addLayout(chartNameRow);

    m_chartWidget = new ChartWidget(chartContainer);
    m_chartWidget->setMinimumHeight(200);
    m_chartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chartLayout->addWidget(m_chartWidget);

    m_chartDock = new QDockWidget(i18n("Device Charts"), this);
    m_chartDock->setObjectName(QStringLiteral("ChartsDock")); // required for saveState()
    m_chartDock->setWidget(chartContainer);
    m_chartDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_chartDock->setFeatures(QDockWidget::DockWidgetMovable
                             | QDockWidget::DockWidgetFloatable
                             | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, m_chartDock);

    // The central widget is a plain empty placeholder — all content lives in
    // docks.  Setting it prevents Qt from using an internal QWidget that would
    // waste space between the dock areas.
    QWidget *placeholder = new QWidget(this);
    placeholder->setMaximumSize(0, 0); // collapse to nothing
    setCentralWidget(placeholder);
}

void MainWindow::wireSignals()
{
    // API signals
    connect(m_api, &FritzApi::loginSuccess,
            this,  &MainWindow::onLoginSuccess);
    connect(m_api, &FritzApi::loginFailed,
            this,  &MainWindow::onLoginFailed);
    connect(m_api, &FritzApi::sslError,
            this,  &MainWindow::onSslError);
    connect(m_api, &FritzApi::sessionExpired,
            this,  &MainWindow::onSessionExpired);
    connect(m_api, &FritzApi::deviceListUpdated,
            this,  &MainWindow::onDeviceListUpdated);
    connect(m_api, &FritzApi::deviceStatsUpdated,
             this,  [this](const QString &ain, const DeviceBasicStats &stats) {
                 // Single-device case: AIN matches the selected device directly.
                 if (ain == m_selectedAin) {
                     m_chartWidget->updateEnergyStats(stats);
                     return;
                 }
                 // Group case: AIN is one of the members we are collecting,
                 // and the group is still the selected device (guard against
                 // stale replies from a previously selected group).
                 if (m_groupStatsPending > 0
                     && m_selectedAin == m_groupAin
                     && m_groupMemberStats.contains(ain)) {
                     m_groupMemberStats[ain] = stats;
                     --m_groupStatsPending;
                     if (m_groupStatsPending == 0) {
                         // All member stats arrived — build the stacked history chart
                         // in device-list order (m_groupMemberOrder), not QMap alphabetical.
                         QList<MemberHistoryEntry> memberStats;
                         for (const QString &memberAin : m_groupMemberOrder) {
                             auto it = m_groupMemberStats.constFind(memberAin);
                             if (it != m_groupMemberStats.constEnd()) {
                                 FritzDevice memberDev = m_model->deviceByAin(memberAin);
                                 MemberHistoryEntry entry;
                                 entry.name       = memberDev.name.isEmpty() ? memberAin : memberDev.name;
                                 entry.stats      = it.value();
                                 entry.isProducer = memberDev.isProducer;
                                 memberStats.append(entry);
                             }
                         }
                         m_chartWidget->updateGroupEnergyStats(memberStats);
                     }
                 }
             });
    connect(m_api, &FritzApi::deviceStatsError,
            this, [this](const QString &ain, const QString &error) {
                 // Single-device case: AIN matches the selected device directly.
                 if (ain == m_selectedAin) {
                     m_chartWidget->updateEnergyStatsError(error);
                     return;
                 }
                 // Group case: AIN is one of the members we are collecting.
                 // Do NOT replace the chart tab with an error display here —
                 // the final updateGroupEnergyStats() call below rebuilds the
                 // stacked chart with a "missing data" warning banner that
                 // names the failed members.  Showing a full error tab now
                 // would (a) clobber the chart for the whole group when only
                 // one member failed, and (b) persist across periodic refresh
                 // ticks where the skip-rebuild guard in updateGroupEnergyStats
                 // returns early without rebuilding the tab.
                 if (m_groupStatsPending > 0
                     && m_selectedAin == m_groupAin
                     && m_groupMemberStats.contains(ain)) {
                     // Decrement pending counter so the chart is still built
                     // (with a warning banner naming the failed member) even
                     // if one member's stats fetch fails.
                     --m_groupStatsPending;
                     if (m_groupStatsPending == 0) {
                         QList<MemberHistoryEntry> memberStats;
                         for (const QString &memberAin : m_groupMemberOrder) {
                             auto it = m_groupMemberStats.constFind(memberAin);
                             if (it != m_groupMemberStats.constEnd()) {
                                 FritzDevice memberDev2 = m_model->deviceByAin(memberAin);
                                 MemberHistoryEntry entry;
                                 entry.name       = memberDev2.name.isEmpty() ? memberAin : memberDev2.name;
                                 entry.stats      = it.value();
                                 entry.isProducer = memberDev2.isProducer;
                                 memberStats.append(entry);
                             }
                         }
                         m_chartWidget->updateGroupEnergyStats(memberStats);
                     }
                 }
            });
    connect(m_api, &FritzApi::networkError,
            this,  &MainWindow::onNetworkError);
    connect(m_api, &FritzApi::commandSuccess,
            this,  &MainWindow::onCommandSuccess);
    connect(m_api, &FritzApi::commandFailed,
            this,  &MainWindow::onCommandFailed);

    // Tree selection
    connect(m_deviceTree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onDeviceSelected);

    // Producer checkbox signal from device panels — persist setting and rebuild charts
    // Walk all scroll-area-wrapped DeviceWidgets in the control stack
    for (int i = 1; i < m_controlStack->count(); ++i) {
        QScrollArea *sa = qobject_cast<QScrollArea *>(m_controlStack->widget(i));
        if (!sa) continue;
        DeviceWidget *dw = qobject_cast<DeviceWidget *>(sa->widget());
        if (!dw) continue;
        connect(dw, &DeviceWidget::producerStatusChanged,
                this, &MainWindow::setDeviceProducerStatus);
    }

     // Polling interval spinbox
     connect(m_intervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
             this, [this](int seconds) {
                 m_pollingInterval = seconds;
                 QSettings s;
                 s.setValue(QStringLiteral("connection/interval"), seconds);
                 if (m_api->isLoggedIn())
                     m_api->startPolling(seconds * 1000);
             });

    // Local group manager: rebuild model whenever groups change
    connect(m_localGroupManager, &LocalGroupManager::groupsChanged,
            this, &MainWindow::onLocalGroupsChanged);
}

void MainWindow::restoreSettings()
{
    QSettings s;
    // Geometry and dock state are deferred to the first showEvent so that
    // restoreState() sees the real window size and can scale dock widths
    // correctly.  Only non-layout settings are loaded here.

    // Restore saved polling interval into spinbox (block signal so we don't
    // call startPolling before login completes).
    const int savedInterval = s.value(QStringLiteral("connection/interval"), 10).toInt();
    m_pollingInterval = savedInterval;
    m_intervalSpin->blockSignals(true);
    m_intervalSpin->setValue(savedInterval);
    m_intervalSpin->blockSignals(false);
    // Header state is restored on first data arrival (onDeviceListUpdated),
    // because the model must be populated before restoreState is reliable.
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Save all UI layout state before closing
    QSettings s;
    s.setValue(QStringLiteral("ui/geometry"),    saveGeometry());
    s.setValue(QStringLiteral("ui/windowState"), saveState());
    // Only persist the tree header state if the columns were actually sized
    // for real device data this session.  If the app started offline (no
    // device list ever arrived), initColumnSizes() never ran, the tree still
    // has Qt-default column widths, and saving them now would overwrite the
    // previously-good header state from a successful prior session.
    if (m_initialColumnSizeDone)
        s.setValue(QStringLiteral("ui/headerState"), m_deviceTree->header()->saveState());

    // Persist per-connection tree state (expanded groups + selected device).
    // Only writes if a successful login happened this session — guarded inside.
    saveConnectionTreeState();

    // Stop polling and abort any in-flight network requests before the
    // application tears down.  Calling qApp->quit() synchronously here would
    // destroy QNetworkAccessManager (and its pending replies) while their
    // finished() callbacks still hold a pointer to this MainWindow, causing a
    // use-after-free crash.  A deferred quit via QMetaObject::invokeMethod(
    // Qt::QueuedConnection) gives pending objects one last event-loop iteration
    // to clean up.
    m_api->stopPolling();
    m_api->abortPendingRequests();
    event->accept();
    QMetaObject::invokeMethod(qApp, &QApplication::quit, Qt::QueuedConnection);
}

void MainWindow::showEvent(QShowEvent *event)
{
#if HAVE_KF
    // Do NOT call KXmlGuiWindow::showEvent() — that triggers
    // applyMainWindowSettings() which reads dock state from KConfig,
    // overwriting our QSettings-based restore.
    QMainWindow::showEvent(event);
#else
    QMainWindow::showEvent(event);
#endif

    // Restore geometry + dock state on the first show only.
    // restoreGeometry() runs immediately; restoreState() is deferred via
    // singleShot(0) so it runs after Qt has finished the initial layout pass
    // triggered by restoreGeometry().  Without the deferral the layout pass
    // that follows restoreGeometry() resets dock widths to their default
    // proportions, overwriting what restoreState() just set.
    if (!m_windowStateRestored) {
        m_windowStateRestored = true;
        QSettings s;
        if (s.contains(QStringLiteral("ui/geometry")))
            restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray());
        if (s.contains(QStringLiteral("ui/windowState"))) {
            const QByteArray state = s.value(QStringLiteral("ui/windowState")).toByteArray();
            QTimer::singleShot(0, this, [this, state]() {
                restoreState(state);
            });
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::configure(const QString &host,
                           const QString &username,
                           const QString &password,
                           int pollingIntervalSeconds,
                           bool ignoreSsl)
{
    // If we were previously logged in to a different connection, persist its
    // tree state now (before m_api gets reconfigured) so it isn't lost.
    if (m_loginSucceeded
        && (m_api->host() != host || m_api->username() != username)) {
        saveConnectionTreeState();
    }
    // New connection attempt: previous success no longer applies.
    m_loginSucceeded     = false;
    m_pendingTreeRestore = false;
    m_pendingExpandedGroups.clear();
    m_pendingSelectedAin.clear();

    m_pollingInterval = pollingIntervalSeconds;
    m_api->setHost(host);
    m_api->setCredentials(username, password);
    m_api->setIgnoreSsl(ignoreSsl);

    // Persist host, username, interval and ignoreSsl flag — never the password
    QSettings s;
    s.setValue(QStringLiteral("connection/host"),      host);
    s.setValue(QStringLiteral("connection/username"),  username);
    s.setValue(QStringLiteral("connection/interval"),  pollingIntervalSeconds);
    s.setValue(QStringLiteral("connection/ignoreSsl"), ignoreSsl);

    // Persist password via SecretStore (KWallet when available, QSettings fallback)
    SecretStore::savePassword(host, username, password);

    setStatusMessage(i18n("Connecting to %1…", host));
    m_api->login();
}

void MainWindow::showLoginDialog()
{
    LoginWindow dlg(this);

    // Prefer live values already in FritzApi; fall back to saved settings on
    // first launch (when m_api->host() is still empty).
    QSettings s;
    if (m_api->host().isEmpty()) {
        const QString savedHost = s.value(QStringLiteral("connection/host"),
                                          QStringLiteral("fritz.box")).toString();
        const QString savedUser = s.value(QStringLiteral("connection/username"),
                                          QString()).toString();
        dlg.setHost(savedHost);
        dlg.setUsername(savedUser);
        dlg.setPassword(SecretStore::loadPassword(savedHost, savedUser));
    } else {
        dlg.setHost(m_api->host());
        dlg.setUsername(m_api->username());
        dlg.setPassword(SecretStore::loadPassword(m_api->host(), m_api->username()));
    }
    dlg.setAutoLogin(s.value(QStringLiteral("connection/autoLogin"), false).toBool());
    dlg.setIgnoreSsl(s.value(QStringLiteral("connection/ignoreSsl"), false).toBool());

    if (dlg.exec() != QDialog::Accepted)
        return;

    s.setValue(QStringLiteral("connection/autoLogin"), dlg.autoLogin());
    m_api->stopPolling();
    configure(dlg.host(), dlg.username(), dlg.password(), m_pollingInterval, dlg.ignoreSsl());
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup helpers
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::setupActions()
{
    // File > Connect (= open login dialog)
    QAction *connectAction = new QAction(
        QIcon::fromTheme(QStringLiteral("network-connect")),
        i18n("&Connect…"), this);
    // On the KF path shortcuts are registered via KActionCollection::setDefaultShortcut()
    // below; calling QAction::setShortcut() directly would trigger a kf.xmlgui warning.
#if !HAVE_KF
    connectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
#endif
    connect(connectAction, &QAction::triggered, this, &MainWindow::actionConnect);

    // File > Refresh
    QAction *refreshAction = new QAction(
        QIcon::fromTheme(QStringLiteral("view-refresh")),
        i18n("&Refresh"), this);
#if !HAVE_KF
    refreshAction->setShortcut(QKeySequence::Refresh);
#endif
    connect(refreshAction, &QAction::triggered, this, &MainWindow::actionRefresh);

    // Quit action — wire to close() so closeEvent() saves state and aborts
    // pending network requests before the event loop exits.
    QAction *quitAction = new QAction(
        QIcon::fromTheme(QStringLiteral("application-exit")),
        i18n("&Quit"), this);
#if !HAVE_KF
    quitAction->setShortcut(QKeySequence::Quit);
#endif
    connect(quitAction, &QAction::triggered, this, &MainWindow::close);

    // Tools > Manage Local Groups
    QAction *localGroupsAction = new QAction(
        QIcon::fromTheme(QStringLiteral("folder-new")),
        i18n("Manage &Local Groups…"), this);
    connect(localGroupsAction, &QAction::triggered, this, &MainWindow::actionManageLocalGroups);

    // View > Show Device List  (toggle dock visibility)
    QAction *showDeviceDockAction = m_deviceDock->toggleViewAction();
    showDeviceDockAction->setText(i18n("Show &Device List"));
    showDeviceDockAction->setIcon(QIcon::fromTheme(QStringLiteral("view-list-tree")));
#if !HAVE_KF
    showDeviceDockAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
#endif

    // View > Show Device Control  (toggle dock visibility)
    QAction *showControlDockAction = m_controlDock->toggleViewAction();
    showControlDockAction->setText(i18n("Show Device &Control"));
    showControlDockAction->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
#if !HAVE_KF
    showControlDockAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
#endif

    // View > Show Device Charts  (toggle dock visibility)
    QAction *showChartDockAction = m_chartDock->toggleViewAction();
    showChartDockAction->setText(i18n("Show &Device Charts"));
    showChartDockAction->setIcon(QIcon::fromTheme(QStringLiteral("office-chart-line")));
#if !HAVE_KF
    showChartDockAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
#endif

    // The QMainWindow right-click context menu is suppressed by the
    // createPopupMenu() override in mainwindow.h; no setContextMenuPolicy
    // calls needed here.

#if HAVE_KF
    actionCollection()->addAction(QStringLiteral("file_connect"),       connectAction);
    actionCollection()->addAction(QStringLiteral("file_refresh"),       refreshAction);
    actionCollection()->addAction(QStringLiteral("tools_localgroups"),  localGroupsAction);
    actionCollection()->addAction(QStringLiteral("view_devicelist"),    showDeviceDockAction);
    actionCollection()->addAction(QStringLiteral("view_devicecontrol"), showControlDockAction);
    actionCollection()->addAction(QStringLiteral("view_charts"),        showChartDockAction);
    actionCollection()->setDefaultShortcut(connectAction,          QKeySequence(Qt::CTRL | Qt::Key_L));
    actionCollection()->setDefaultShortcut(refreshAction,          QKeySequence::Refresh);
    actionCollection()->setDefaultShortcut(showDeviceDockAction,   QKeySequence(Qt::CTRL | Qt::Key_D));
    actionCollection()->setDefaultShortcut(showControlDockAction,  QKeySequence(Qt::CTRL | Qt::Key_P));
    actionCollection()->setDefaultShortcut(showChartDockAction,    QKeySequence(Qt::CTRL | Qt::Key_H));
    KStandardAction::quit(this, &MainWindow::close, actionCollection());

    // Omit the Save flag: dock/toolbar layout is persisted via our own
    // QSettings-based saveState()/restoreState() in closeEvent/showEvent.
    setupGUI(Keys | StatusBar | Create);

    // resetAutoSaveSettings() disables KMainWindow's automatic save/restore of
    // window geometry and dock state via KConfig (fritzhomestaterc).  Without
    // this, KMainWindow::closeEvent() still saves state to KConfig, and
    // applyMainWindowSettings() (triggered internally by KMainWindow) restores
    // it on next launch — overwriting the dock positions we restore from
    // QSettings.  We own the full save/restore cycle via QSettings exclusively.
    resetAutoSaveSettings();

    const auto toolbarList = toolBars();
    for (KToolBar *tb : toolbarList) {
        removeToolBar(tb);
        delete tb;
    }
#else
    // Plain Qt: build menu manually (no toolbar)
    QMenu *fileMenu = menuBar()->addMenu(i18n("&File"));
    fileMenu->addAction(connectAction);
    fileMenu->addAction(refreshAction);
    fileMenu->addSeparator();
    fileMenu->addAction(quitAction);

    QMenu *viewMenu = menuBar()->addMenu(i18n("&View"));
    viewMenu->addAction(showDeviceDockAction);
    viewMenu->addAction(showControlDockAction);
    viewMenu->addAction(showChartDockAction);

    QMenu *toolsMenu = menuBar()->addMenu(i18n("&Tools"));
    toolsMenu->addAction(localGroupsAction);
#endif
}

void MainWindow::setupStatusBar()
{
    m_statusLabel = new QLabel(i18n("Not connected"), this);
    statusBar()->addWidget(m_statusLabel, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots — API events
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::onLoginSuccess()
{
    setStatusMessage(i18n("Connected to %1", m_api->host()));
    // Mark this session as having reached an authenticated state. Tree state
    // is only persisted on close once this flag is set, so a failed login
    // can never wipe out the previous session's snapshot.
    m_loginSucceeded = true;
    // Load saved tree state for this (host, user) pair; it will be applied
    // when the next device-list update arrives (onDeviceListUpdated).
    loadConnectionTreeState();
    m_api->startPolling(m_pollingInterval * 1000);
    // Fetch immediately so the device list appears without waiting for the
    // first interval tick
    m_api->fetchDeviceList();
}

void MainWindow::onLoginFailed(const QString &error)
{
    setStatusMessage(i18n("Login failed: %1", error));
#if HAVE_KF
    KMessageBox::error(this,
        i18n("Could not log in to Fritz!Box:\n%1\n\nPlease check your credentials.", error),
        i18n("Login Failed"));
#else
    QMessageBox::critical(this,
        i18n("Login Failed"),
        i18n("Could not log in to Fritz!Box:\n%1\n\nPlease check your credentials.", error));
#endif
    showLoginDialog();
}

void MainWindow::onSslError(const QString &details)
{
    // TLS certificate errors are reported before the request is aborted.
    // Show a dedicated, actionable message so the user knows exactly what went
    // wrong and how to fix it — either fix the certificate or enable the
    // "Ignore TLS certificate warnings" option in the login dialog.
    const QString hint = i18n(
        "If your Fritz!Box uses a self-signed certificate you can enable\n"
        "\"Ignore TLS certificate warnings\" in the connection dialog.");
    const QString msg  = i18n(
        "A TLS/SSL certificate error occurred while connecting to %1:\n\n"
        "%2\n\n%3",
        m_api->host(), details, hint);

    setStatusMessage(i18n("TLS error — see dialog for details"));
#if HAVE_KF
    KMessageBox::error(this, msg, i18n("TLS Certificate Error"));
#else
    QMessageBox::critical(this, i18n("TLS Certificate Error"), msg);
#endif
    // After dismissing the message, re-open the connection dialog so the user
    // can enable "Ignore TLS certificate warnings" without having to navigate
    // there manually.
    showLoginDialog();
}

void MainWindow::onSessionExpired()
{
    // The Fritz!Box session has expired mid-run.  FritzApi has already
    // invalidated the local SID and is attempting an automatic re-login.
    // Update status to indicate the reconnecting state — do NOT open the login
    // dialog; that only happens if the re-login itself fails (loginFailed will
    // be emitted in that case).
    setStatusMessage(i18n("Session expired — reconnecting to %1…", m_api->host()));
}

// ── Tree state save/restore helpers ───────────────────────────────────────────

QSet<QString> MainWindow::saveTreeState() const
{
    QSet<QString> expandedGroups;
    const int groupCount = m_model->rowCount();
    for (int g = 0; g < groupCount; ++g) {
        const QModelIndex gi = m_model->index(g, 0);
        if (m_deviceTree->isExpanded(gi))
            expandedGroups.insert(m_model->data(gi, Qt::UserRole).toString());
    }
    return expandedGroups;
}

void MainWindow::restoreTreeState(const QSet<QString> &expandedGroups, bool expandAll)
{
    const int groupCount = m_model->rowCount();
    for (int g = 0; g < groupCount; ++g) {
        const QModelIndex gi = m_model->index(g, 0);
        const QString label  = m_model->data(gi, Qt::UserRole).toString();
        m_deviceTree->setExpanded(gi, expandAll || expandedGroups.contains(label));
    }
}

// ── Per-connection tree state persistence ──────────────────────────────────
//
// Tree state (expanded groups + selected device) is stored per-connection so
// users who switch between multiple Fritz!Boxes get the correct UI restored
// for each one. The key prefix is derived from host + username; QSettings'
// group separator '/' is replaced in the components so it cannot be confused
// with the path structure.

QString MainWindow::connectionStateKey() const
{
    const QString host = m_api ? m_api->host() : QString();
    const QString user = m_api ? m_api->username() : QString();
    if (host.isEmpty() || user.isEmpty())
        return QString();

    // Sanitize: QSettings uses '/' as a separator, and '\\' has special meaning
    // on the Windows registry backend. Replace both to keep the key flat.
    auto sanitize = [](QString s) {
        s.replace(QLatin1Char('/'),  QLatin1Char('_'));
        s.replace(QLatin1Char('\\'), QLatin1Char('_'));
        return s;
    };
    return QStringLiteral("connections/%1@%2")
            .arg(sanitize(user), sanitize(host));
}

void MainWindow::saveConnectionTreeState()
{
    // Only persist after a confirmed successful login this session; otherwise
    // a failed or aborted login could overwrite a previously good snapshot
    // with an empty / partially-populated tree state.
    if (!m_loginSucceeded)
        return;

    const QString prefix = connectionStateKey();
    if (prefix.isEmpty())
        return;

    QSettings s;
    // Collect currently expanded groups. If the device list never populated
    // (e.g. lost network right after login), m_model is empty and
    // saveTreeState() returns an empty set — but we leave any pending
    // restore values untouched by preferring them in that case.
    QStringList expanded;
    if (m_model->rowCount() > 0) {
        const QSet<QString> set = saveTreeState();
        expanded = QStringList(set.begin(), set.end());
    } else if (m_pendingTreeRestore) {
        // Restore never completed — keep the previously saved snapshot.
        expanded = QStringList(m_pendingExpandedGroups.begin(),
                               m_pendingExpandedGroups.end());
    }

    // Selected AIN: prefer the live selection, fall back to a pending
    // restore value that never got applied.
    QString selectedAin = m_selectedAin;
    if (selectedAin.isEmpty() && m_pendingTreeRestore)
        selectedAin = m_pendingSelectedAin;

    s.setValue(prefix + QStringLiteral("/expandedGroups"), expanded);
    s.setValue(prefix + QStringLiteral("/selectedAin"),    selectedAin);
}

void MainWindow::loadConnectionTreeState()
{
    m_pendingExpandedGroups.clear();
    m_pendingSelectedAin.clear();
    m_pendingTreeRestore = false;

    const QString prefix = connectionStateKey();
    if (prefix.isEmpty())
        return;

    QSettings s;
    if (!s.contains(prefix + QStringLiteral("/expandedGroups"))
        && !s.contains(prefix + QStringLiteral("/selectedAin")))
        return;

    const QStringList expanded =
        s.value(prefix + QStringLiteral("/expandedGroups")).toStringList();
    m_pendingExpandedGroups = QSet<QString>(expanded.begin(), expanded.end());
    m_pendingSelectedAin    =
        s.value(prefix + QStringLiteral("/selectedAin")).toString();
    m_pendingTreeRestore    = true;
}

void MainWindow::initColumnSizes(const FritzDeviceList &devices)
{
    if (m_initialColumnSizeDone || devices.isEmpty())
        return;

    QSettings s;
    const QByteArray headerState = s.value(QStringLiteral("ui/headerState")).toByteArray();
    if (!headerState.isEmpty()) {
        m_deviceTree->header()->restoreState(headerState);
    } else {
        // First ever launch: size to content, with a manual minimum for
        // the Name column to account for the decoration icon.
        m_deviceTree->resizeColumnToContents(0);
        const QFontMetrics fm(m_deviceTree->font());
        int maxNameWidth = fm.horizontalAdvance(
            m_deviceTree->model()->headerData(0, Qt::Horizontal).toString());
        for (const FritzDevice &dev : devices)
            maxNameWidth = qMax(maxNameWidth, fm.horizontalAdvance(dev.name));
        // Add padding for icon (24 px) + cell margins (16 px) + indent
        maxNameWidth += 60;
        if (m_deviceTree->columnWidth(0) < maxNameWidth)
            m_deviceTree->setColumnWidth(0, maxNameWidth);
    }
    m_initialColumnSizeDone = true;
}

void MainWindow::reselectDevice(const QString &ain)
{
    // Iterate tree model: groups at top level, devices as children
    const int groupCount = m_model->rowCount();
    for (int g = 0; g < groupCount; ++g) {
        const QModelIndex groupIdx = m_model->index(g, 0);
        const int devCount = m_model->rowCount(groupIdx);
        for (int d = 0; d < devCount; ++d) {
            const QModelIndex leafIdx = m_model->index(d, 0, groupIdx);
            const FritzDevice dev = m_model->deviceAt(leafIdx);
            if (dev.ain != ain)
                continue;

            m_selectedAin = ain;
            m_deviceTree->selectionModel()->blockSignals(true);
            m_deviceTree->selectionModel()->setCurrentIndex(
                leafIdx,
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_deviceTree->selectionModel()->blockSignals(false);

            m_deviceIconLabel->setPixmap(deviceHeadingPixmap(dev));
            m_deviceIconLabel->show();
            m_deviceNameLabel->setText(dev.name);
            m_deviceNameLabel->show();
            m_chartIconLabel->setPixmap(deviceHeadingPixmap(dev));
            m_chartIconLabel->show();
            m_chartNameLabel->setText(dev.name);
            m_chartNameLabel->show();

            // Suppress the panel update if a switch command was recently
            // issued for this device/group and the Fritz!Box may still be
            // reporting transient (inconsistent) member states.
            const bool suppressed = m_suppressPanelUntil.isValid()
                                 && m_suppressForAin == dev.ain
                                 && QDateTime::currentDateTime() < m_suppressPanelUntil;
            if (!suppressed)
                updateDevicePanel(dev);

            // Poll tick for the same device: update series in-place without
            // rebuilding charts (avoids flicker and unnecessary work).
            // Exception: if the chart has no tabs yet (first call after
            // session restore) or only the Info placeholder tab is shown
            // (e.g. the device was clicked before connection and had no
            // capabilities yet), do a full rebuild now that it may have
            // real chart data.
            const FritzDeviceList memberDevs = dev.isGroup()
                ? collectMemberDevices(dev) : FritzDeviceList();
            if (m_chartWidget->isEmpty() || m_chartWidget->hasOnlyInfoTab())
                m_chartWidget->updateDevice(dev, memberDevs);
            else
                m_chartWidget->updateRollingCharts(dev, memberDevs);

            fetchEnergyStatsIfDue(dev, memberDevs);
            return;
        }
    }
}

void MainWindow::fetchEnergyStatsIfDue(const FritzDevice &dev,
                                        const FritzDeviceList &memberDevs)
{
    if (!dev.hasEnergyMeter())
        return;

    // Throttle interval depends on the currently displayed view:
    // 60 s for the 15-min/24-h chart, 5 min for daily/monthly,
    // 30 s when no chart has been built yet (grid==0, placeholder state).
    const int grid = m_chartWidget->activeEnergyGrid();
    const int throttleSecs = (grid == 0)   ? 30
                           : (grid == 900) ? 60
                                           : 300;
    if (m_lastStatsFetch.isValid() &&
        m_lastStatsFetch.secsTo(QDateTime::currentDateTime()) < throttleSecs)
        return;

    fetchGroupOrDeviceStats(dev, memberDevs);
    m_lastStatsFetch = QDateTime::currentDateTime();
}

void MainWindow::fetchGroupOrDeviceStats(const FritzDevice &dev,
                                          const FritzDeviceList &memberDevs)
{
    // Reset group-fetch state unconditionally before starting new fetches.
    m_groupMemberStats.clear();
    m_groupMemberOrder.clear();
    m_groupStatsPending = 0;
    m_groupAin.clear();

    if (dev.isGroup()) {
        FritzDeviceList energyMembers;
        for (const FritzDevice &m : memberDevs)
            if (m.hasEnergyMeter())
                energyMembers.append(m);
        if (!energyMembers.isEmpty()) {
            m_groupAin = dev.ain;
            for (const FritzDevice &m : energyMembers) {
                m_groupMemberStats.insert(m.ain, DeviceBasicStats{});
                m_groupMemberOrder.append(m.ain);
            }
            m_groupStatsPending = energyMembers.size();
            for (const FritzDevice &m : energyMembers)
                m_api->fetchDeviceStats(m.ain);
        }
    } else {
        m_api->fetchDeviceStats(dev.ain);
    }
}

// ── Device list updated ──────────────────────────────────────────────────────

void MainWindow::onDeviceListUpdated(const FritzDeviceList &devices)
{
    // Save the selected AIN before the model reset: beginResetModel/endResetModel
    // clears the view selection, which fires currentChanged → onDeviceSelected
    // with an invalid index, which in turn clears m_selectedAin.  We must
    // capture it here, before that chain runs.
    const QString previousAin = m_selectedAin;

    QSet<QString> expandedGroups = saveTreeState();
    const bool firstLoad = expandedGroups.isEmpty() && m_model->rowCount() == 0;

    // If a per-connection restore is pending (set by onLoginSuccess), apply
    // the saved snapshot now that the model is about to be populated. This
    // happens only once per successful login.
    QString restoreSelectedAin;
    if (m_pendingTreeRestore) {
        expandedGroups       = m_pendingExpandedGroups;
        restoreSelectedAin   = m_pendingSelectedAin;
        m_pendingTreeRestore = false;
        m_pendingExpandedGroups.clear();
        m_pendingSelectedAin.clear();
    }

    m_lastFritzDevices = devices;
    m_model->updateDevices(devices);
    // Apply local groups after Fritz!Box devices so synthesized entries have
    // access to the full device list for capability/state computation.
    m_model->setLocalGroups(m_localGroupManager->groups(), devices);
    // Re-apply saved producer flags after every model reset (updateDevices
    // and setLocalGroups rebuild from scratch, losing any runtime-only isProducer state).
    loadProducerSettings();

    initColumnSizes(devices);
    // When restoring saved state, never fall back to expand-all even on first
    // load — the user's saved snapshot is authoritative (it may legitimately
    // contain zero expanded groups).
    const bool expandAll = firstLoad && restoreSelectedAin.isEmpty()
                                     && expandedGroups.isEmpty();
    restoreTreeState(expandedGroups, expandAll);

    // Keep the current selection / panel in sync.
    // Prefer the saved restore AIN over previousAin: a session restore should
    // win on the first list update after login. previousAin is only set if the
    // user clicked something between login and the first list arriving — which
    // is unlikely but harmless to honour.
    const QString selectionAin = !restoreSelectedAin.isEmpty()
                                ? restoreSelectedAin
                                : previousAin;
    if (!selectionAin.isEmpty()) {
        reselectDevice(selectionAin);
    }

    const int n = devices.size();
    setStatusMessage(i18n("Connected to %1 — %2 device(s)", m_api->host(), n));
}

void MainWindow::onNetworkError(const QString &error)
{
    setStatusMessage(i18n("Network error: %1", error));
}

void MainWindow::onCommandSuccess(const QString &ain, const QString &cmd)
{
    Q_UNUSED(cmd)
    // If the command was for the selected device/group (or any of its resolved
    // members), suppress updateDevicePanel for 450 ms.  The Fritz!Box transiently
    // reports inconsistent member states for the first poll after a switch command
    // (~300 ms after the reply), which would cause the status label to flash "ON"
    // before correcting to "PARTIAL".  450 ms blocks that first bad poll; the
    // second fetch (fired at 500 ms by onCommandReply) carries stable data.
    //
    // NOTE: selected.memberAins holds numeric device IDs (e.g. "24"), not AIN
    // strings.  We must use collectMemberDevices() to resolve them to FritzDevice
    // objects and then compare .ain — a plain memberAins.contains(ain) would never
    // match.
    if (!m_selectedAin.isEmpty()) {
        const FritzDevice selected = m_model->deviceByAin(m_selectedAin);
        bool affectsSelected = (ain == m_selectedAin);
        if (!affectsSelected && selected.isGroup()) {
            const FritzDeviceList members = collectMemberDevices(selected);
            for (const FritzDevice &m : members) {
                if (m.ain == ain) { affectsSelected = true; break; }
            }
        }
        if (affectsSelected) {
            m_suppressForAin     = m_selectedAin;
            m_suppressPanelUntil = QDateTime::currentDateTime().addMSecs(450);
        }
    }
    // Trigger an immediate refresh so the UI reflects the change
    QTimer::singleShot(300, this, [this]() {
        m_api->fetchDeviceList();
    });
}

void MainWindow::onCommandFailed(const QString &ain, const QString &error)
{
    setStatusMessage(i18n("Command failed for %1: %2", ain, error));
}

// ─────────────────────────────────────────────────────────────────────────────
// Slots — UI actions
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::actionConnect()
{
    showLoginDialog();
}

void MainWindow::actionRefresh()
{
    if (m_api->isLoggedIn()) {
        m_api->fetchDeviceList();
    } else {
        showLoginDialog();
    }
}

void MainWindow::actionSettings()
{
    showLoginDialog();
}

void MainWindow::actionManageLocalGroups()
{
    LocalGroupDialog dlg(m_localGroupManager, m_lastFritzDevices, this);
    dlg.exec();
    // groupsChanged() from manager already updates the model via wireSignals()
}

void MainWindow::onLocalGroupsChanged()
{
    // Rebuild the Local Groups bucket after any add/rename/delete/member change.
    const QString previousAin     = m_selectedAin;
    const QSet<QString> expanded  = saveTreeState();
    m_model->setLocalGroups(m_localGroupManager->groups(), m_lastFritzDevices);
    // Re-apply producer flags: setLocalGroups() rebuilds from scratch so all
    // isProducer fields are reset to false.  collectMemberDevices() reads from
    // the model, so flags must be restored before we rebuild the chart.
    loadProducerSettings();
    restoreTreeState(expanded, false);
    if (previousAin.isEmpty())
        return;
    reselectDevice(previousAin);
    // reselectDevice does a rolling in-place chart update; when group membership
    // changes the chart must be fully rebuilt to reflect the new composition.
    FritzDevice dev = m_model->deviceByAin(previousAin);
    if (dev.ain.isEmpty())
        dev = m_model->deviceById(previousAin);
    if (dev.ain.isEmpty() || !dev.isGroup())
        return;
    const FritzDeviceList memberDevs = collectMemberDevices(dev);
    m_chartWidget->updateDevice(dev, memberDevs);
    if (dev.hasEnergyMeter()) {
        fetchGroupOrDeviceStats(dev, memberDevs);
        m_lastStatsFetch = QDateTime::currentDateTime();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Device selection
// ─────────────────────────────────────────────────────────────────────────────

FritzDeviceList MainWindow::collectMemberDevices(const FritzDevice &groupDev) const
{
    QSet<QString> visited;
    QSet<QString> seenAins;
    return collectMemberDevicesImpl(groupDev, visited, seenAins);
}

FritzDeviceList MainWindow::collectMemberDevicesImpl(
    const FritzDevice &groupDev,
    QSet<QString> &visited,
    QSet<QString> &seenAins) const
{
    // Cycle guard keyed on the group's AIN (includes "local:" prefix for local
    // groups, so native and local groups can never collide).
    if (visited.contains(groupDev.ain))
        return {};
    visited.insert(groupDev.ain);

    FritzDeviceList members;
    for (const QString &memberId : groupDev.memberAins) {
        if (memberId.startsWith(QStringLiteral("local:"))) {
            // Nested local group: resolve its synthetic FritzDevice and recurse.
            FritzDevice m = m_model->deviceByAin(memberId);
            if (!m.ain.isEmpty())
                members += collectMemberDevicesImpl(m, visited, seenAins);
        } else {
            // memberAins contains device AIns resolved from the REST API's
            // memberUnitUids.  Look up by id; fall back to AIN lookup for
            // robustness.
            FritzDevice m = m_model->deviceById(memberId);
            if (m.ain.isEmpty())
                m = m_model->deviceByAin(memberId);
            if (m.ain.isEmpty())
                continue;
            if (!m.isGroup()) {
                if (!seenAins.contains(m.ain)) {
                    seenAins.insert(m.ain);
                    members.append(m);
                }
            } else {
                // Member is a native Fritz!Box group — expand to its leaf
                // devices so the stacked chart and per-member energy history
                // fetch work correctly (each leaf needs its own stats fetch
                // and appears as a separate bar).
                // NOTE: collectMemberDevicesImpl already inserts leaves into
                // seenAins during the recursive call, so the returned list
                // is already deduplicated — append directly without re-checking.
                bool anyLeafHasTemp = false;
                const FritzDeviceList leaves = collectMemberDevicesImpl(m, visited, seenAins);
                for (const FritzDevice &leaf : leaves) {
                    members.append(leaf);
                    if (leaf.hasTemperature() || leaf.hasThermostat())
                        anyLeafHasTemp = true;
                }
                // If the Fritz!Box group unit itself carries temperature data
                // (via its group control unit) but none of the leaves do, add
                // the group device so the temperature chart tab is populated.
                if (!anyLeafHasTemp && (m.hasTemperature() || m.hasThermostat())
                        && !seenAins.contains(m.ain)) {
                    seenAins.insert(m.ain);
                    members.append(m);
                }
            }
        }
    }

    // Allow re-entry from sibling paths (only block true cycles on the current
    // call stack), so shared sub-groups are expanded at most once per top-level
    // call (seenAins deduplicates the result).
    visited.remove(groupDev.ain);
    return members;
}

void MainWindow::onDeviceSelected(const QModelIndex &current, const QModelIndex &/*previous*/)
{
    if (!current.isValid() || m_model->isGroupHeader(current)) {
        m_controlStack->setCurrentIndex(PanelEmpty);
        m_selectedAin.clear();
        m_deviceIconLabel->hide();
        m_deviceNameLabel->hide();
        m_chartIconLabel->hide();
        m_chartNameLabel->hide();
        return;
    }

    const FritzDevice dev = m_model->deviceAt(current);
    m_selectedAin = dev.ain;
    m_deviceIconLabel->setPixmap(deviceHeadingPixmap(dev));
    m_deviceIconLabel->show();
    m_deviceNameLabel->setText(dev.name);
    m_deviceNameLabel->show();
    m_chartIconLabel->setPixmap(deviceHeadingPixmap(dev));
    m_chartIconLabel->show();
    m_chartNameLabel->setText(dev.name);
    m_chartNameLabel->show();
    updateDevicePanel(dev);
    const FritzDeviceList memberDevs = dev.isGroup() ? collectMemberDevices(dev) : FritzDeviceList();
    m_chartWidget->updateDevice(dev, memberDevs);
    // Fetch detailed energy history immediately on selection
    if (dev.hasEnergyMeter()) {
        fetchGroupOrDeviceStats(dev, memberDevs);
        m_lastStatsFetch = QDateTime::currentDateTime();
    }
}

void MainWindow::updateDevicePanel(const FritzDevice &device)
{
    // Priority order: colour > dimmer > blind > thermostat > switch > energy > humidity > alarm
    int panelIdx = PanelEmpty;

    if (device.hasColorBulb()) {
        panelIdx = PanelColor;
    } else if (device.hasDimmer()) {
        panelIdx = PanelDimmer;
    } else if (device.hasBlind()) {
        panelIdx = PanelBlind;
    } else if (device.hasThermostat()) {
        panelIdx = PanelThermostat;
    } else if (device.hasSwitch()) {
        panelIdx = PanelSwitch;
    } else if (device.hasEnergyMeter()) {
        panelIdx = PanelEnergy;
    } else if (device.hasHumidity()) {
        panelIdx = PanelHumidity;
    } else if (device.hasAlarm()) {
        panelIdx = PanelAlarm;
    }

    m_controlStack->setCurrentIndex(panelIdx);

    if (panelIdx == PanelEmpty)
        return;

    // The panels are wrapped in QScrollArea; unwrap to get the DeviceWidget
    QScrollArea *sa = qobject_cast<QScrollArea *>(m_controlStack->widget(panelIdx));
    if (!sa)
        return;
    DeviceWidget *dw = qobject_cast<DeviceWidget *>(sa->widget());
    if (!dw)
        return;

    // For group switch panels, synthesize lock and mixed-state flags from members
    // (the group unit JSON does not carry isLockedDeviceApi/isLockedDeviceLocal),
    // and provide the member list to the widget for per-member dropdown menus.
    if (device.isGroup() && panelIdx == PanelSwitch) {
        FritzDevice dev = device;
        synthesizeGroupSwitchState(dev, dw);
    } else {
        dw->updateDevice(device);
        dw->setMembers(FritzDeviceList()); // clear any stale member menus
    }

    // With dock-based layout, the dock manages the panel's vertical size.
    // No need to pin the stack to a fixed height on device updates.
}

// ── Group switch state synthesis ─────────────────────────────────────────────

void MainWindow::synthesizeGroupSwitchState(FritzDevice &dev, DeviceWidget *dw) const
{
    const FritzDeviceList members = collectMemberDevices(dev);
    int  switchMembers        = 0;
    int  lockedMembers        = 0; // members where locked (matches rebuildMenus predicate)
    int  controllableOn       = 0; // controllable members currently on
    int  controllableTotal    = 0; // controllable (non-locked) switch members
    bool anyOn                = false;
    bool anyOff               = false;
    for (const FritzDevice &m : members) {
        if (!m.hasSwitch()) continue;
        if (!m.present) continue;   // offline members are invisible to group state
        ++switchMembers;
        // Only the API lock (locked) prevents remote control.  The
        // physical-button lock (deviceLocked) disables the on-device
        // button but does NOT block the AHA / REST API.
        const bool memberLocked = m.switchStats.locked;
        if (memberLocked) {
            ++lockedMembers;
            // Locked members cannot be remote-controlled, but their
            // actual power state still matters for the group display
            // label (ON / OFF / PARTIAL).  Without this, a group where
            // every member is locked would always show "OFF".
            if (m.switchStats.on)
                anyOn = true;
            else
                anyOff = true;
        } else {
            ++controllableTotal;
            if (m.switchStats.on) {
                anyOn = true;
                ++controllableOn;
            } else {
                anyOff = true;
            }
        }
    }
    // Group-level buttons are disabled only when every switch-capable member is
    // locked.  If at least one member is controllable the group-level action
    // remains available; locked members are greyed-out in the per-member menus.
    const bool allLocked = (switchMembers > 0) && (lockedMembers == switchMembers);
    dev.switchStats.locked           = allLocked;
    dev.switchStats.deviceLocked     = false; // rolled into locked above
    // on: synthesized from ALL online members (including locked ones) rather
    //   than taken from the raw group "active" field, which the Fritz!Box
    //   sets to true if ANY member is on — meaning it can disagree with what
    //   the member devices actually report.  Locked members contribute their
    //   real power state so that a fully-locked group still shows ON/OFF
    //   correctly instead of always displaying "OFF".
    // mixedSwitchState: true when members have differing on/off states
    //   right now → label shows PARTIAL.
    // hasLockedMembers: true when at least one member is locked and at least
    //   one is controllable → a group-level toggle would leave the group in a
    //   permanently mixed state, so the toggle button must stay in InstantPopup
    //   (dropdown-only) mode even when all controllable members are currently
    //   in the same state.
    // allOn/allOff: all controllable members are already in the target state →
    //   disable the corresponding group-level button (and grey out members in
    //   the dropdown that are already in the target state).
    dev.switchStats.on               = anyOn;
    dev.switchStats.mixedSwitchState = anyOn && anyOff;
    dev.switchStats.hasLockedMembers = (lockedMembers > 0) && (lockedMembers < switchMembers);
    dev.switchStats.allOn            = (controllableTotal > 0) && (controllableOn == controllableTotal);
    dev.switchStats.allOff           = (controllableTotal > 0) && (controllableOn == 0);
    dw->updateDevice(dev);
    dw->setMembers(members);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::setStatusMessage(const QString &msg)
{
    m_statusLabel->setText(msg);
    statusBar()->showMessage(msg, 0);
}

// ──────────────────────────────────────────────────────────────────────────────
// Persist producer/consumer status for a device and rebuild charts.
// Called from device widget signals (producerStatusChanged) when the checkbox is toggled.
void MainWindow::setDeviceProducerStatus(const QString &ain, bool isProducer)
{
    // Persist to QSettings
    QSettings s;
    s.setValue(QStringLiteral("devices/") + ain + QLatin1Char('/') +
               QString::fromLatin1(kSettingsKeyIsProducer), isProducer);

    // Update model so future device list updates and chart rebuilds see the correct flag
    m_model->updateDeviceProducerStatus(ain, isProducer);

    // Rebuild power charts immediately if the affected device is currently selected
    if (ain == m_selectedAin) {
        m_chartWidget->updateForDeviceProducerStatusChange(isProducer);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Load all producer/consumer settings from QSettings and apply to the device model.
// Must be called after updateDevices() so the model is populated.
void MainWindow::loadProducerSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("devices"));
    const QStringList ains = s.childGroups();
    for (const QString &ain : ains) {
        const bool isProducer = s.value(
            ain + QLatin1Char('/') + QString::fromLatin1(kSettingsKeyIsProducer),
            false).toBool();
        if (isProducer)
            m_model->updateDeviceProducerStatus(ain, true);
    }
    s.endGroup();
}
