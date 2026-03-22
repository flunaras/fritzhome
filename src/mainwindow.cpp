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
#include <QSplitter>
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

// Device-specific widgets
#include "switchwidget.h"
#include "thermostatwidget.h"
#include "energywidget.h"
#include "dimmerwidget.h"
#include "blindwidget.h"
#include "colorwidget.h"
#include "humiditysensorwidget.h"
#include "alarmwidget.h"

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
// Mirrors the logic in DeviceModel::primaryIconName().
static QPixmap deviceHeadingPixmap(const FritzDevice &dev)
{
    QString path;
    if (dev.isGroup())
        path = QStringLiteral(":/icons/device-group.svg");
    else if (dev.hasColorBulb())
        path = QStringLiteral(":/icons/device-color-bulb.svg");
    else if (dev.hasDimmer())
        path = QStringLiteral(":/icons/device-dimmer.svg");
    else if (dev.hasSwitch() && dev.hasEnergyMeter())
        path = QStringLiteral(":/icons/device-smart-plug.svg");
    else if (dev.hasSwitch())
        path = QStringLiteral(":/icons/device-switch.svg");
    else if (dev.hasThermostat())
        path = QStringLiteral(":/icons/device-thermostat.svg");
    else if (dev.hasBlind())
        path = QStringLiteral(":/icons/device-blind.svg");
    else if (dev.hasAlarm())
        path = QStringLiteral(":/icons/device-alarm.svg");
    else if (dev.hasHumidity())
        path = QStringLiteral(":/icons/device-humidity.svg");
    else
        path = QStringLiteral(":/icons/device-sensor.svg");
    return QIcon(path).pixmap(32, 32);
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
{
    setWindowTitle(i18n("Fritz!Box Smart Home"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("fritzhome"),
                                   QIcon(QStringLiteral(":/icons/fritzhome.svg"))));
    resize(1100, 680);

    // ── Central widget ────────────────────────────────────────────────────────
    QWidget *central = new QWidget(this);
    QVBoxLayout *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, central);

    // ── Left: device tree + polling interval ──────────────────────────────────
    QWidget *leftPanel = new QWidget(m_splitter);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    m_deviceTree = new QTreeView(leftPanel);
    m_deviceTree->setModel(m_model);
    m_deviceTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTree->setAlternatingRowColors(true);
    m_deviceTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTree->setRootIsDecorated(true);
    m_deviceTree->setItemsExpandable(true);
    m_deviceTree->setUniformRowHeights(false);
    m_deviceTree->setSortingEnabled(false);  // no proxy model, keep insertion order
    m_deviceTree->setMinimumWidth(320);
    // Set Interactive resize mode
    m_deviceTree->header()->setSectionResizeMode(QHeaderView::Interactive);
    m_deviceTree->header()->setMinimumSectionSize(50);
    leftLayout->addWidget(m_deviceTree, 1);

    // Polling interval row below the tree
    {
        QHBoxLayout *intervalRow = new QHBoxLayout();
        intervalRow->setContentsMargins(4, 2, 4, 2);
        QLabel *intervalLabel = new QLabel(i18n("Refresh interval:"), leftPanel);
        m_intervalSpin = new QSpinBox(leftPanel);
        m_intervalSpin->setRange(2, 300);
        m_intervalSpin->setValue(m_pollingInterval);
        m_intervalSpin->setSuffix(i18n(" s"));
        m_intervalSpin->setToolTip(i18n("How often to refresh device states (2–300 seconds)"));
        intervalRow->addWidget(intervalLabel);
        intervalRow->addWidget(m_intervalSpin, 1);
        leftLayout->addLayout(intervalRow);
    }

    m_splitter->addWidget(leftPanel);

    // ── Right: control panel + charts ─────────────────────────────────────────
    QWidget *rightPanel = new QWidget(m_splitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(4, 4, 4, 4);
    rightLayout->setSpacing(6);

    // Device name heading (icon + text in a horizontal row) ──────────────────
    m_deviceIconLabel = new QLabel(rightPanel);
    m_deviceIconLabel->setFixedSize(32, 32);
    m_deviceIconLabel->setAlignment(Qt::AlignCenter);
    m_deviceIconLabel->hide();

    m_deviceNameLabel = new QLabel(rightPanel);
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
    rightLayout->addLayout(nameRow);

    // Control stack ────────────────────────────────────────────────────────────
    m_controlStack = new QStackedWidget(rightPanel);

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

    m_controlStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // Always size to the current page only, not to the tallest page.
    connect(m_controlStack, &QStackedWidget::currentChanged, this, [this](int) {
        if (QWidget *w = m_controlStack->currentWidget())
            m_controlStack->setFixedHeight(w->sizeHint().height());
    });
    rightLayout->addWidget(m_controlStack, 0);

    // Charts ───────────────────────────────────────────────────────────────────
    m_chartWidget = new ChartWidget(rightPanel);
    m_chartWidget->setMinimumHeight(300);
    m_chartWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(m_chartWidget, 1);

    m_splitter->addWidget(rightPanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({340, 760});

    centralLayout->addWidget(m_splitter);
    setCentralWidget(central);

    // ── Status bar ────────────────────────────────────────────────────────────
    setupStatusBar();

    // ── Actions / menus / toolbar ─────────────────────────────────────────────
    setupActions();

    // ── Wire API signals ──────────────────────────────────────────────────────
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
                        // Pair first = device name (for legend/tooltip), second = stats.
                        QList<QPair<QString, DeviceBasicStats>> memberStats;
                        for (const QString &memberAin : m_groupMemberOrder) {
                            auto it = m_groupMemberStats.constFind(memberAin);
                            if (it != m_groupMemberStats.constEnd()) {
                                FritzDevice memberDev = m_model->deviceByAin(memberAin);
                                const QString label = memberDev.name.isEmpty()
                                    ? memberAin : memberDev.name;
                                memberStats.append({label, it.value()});
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

    // ── Wire tree selection ───────────────────────────────────────────────────
    connect(m_deviceTree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onDeviceSelected);

    // ── Wire polling interval spinbox ─────────────────────────────────────────
    connect(m_intervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int seconds) {
                m_pollingInterval = seconds;
                QSettings s;
                s.setValue(QStringLiteral("connection/interval"), seconds);
                if (m_api->isLoggedIn())
                    m_api->startPolling(seconds * 1000);
            });

    // ── Restore saved UI layout ───────────────────────────────────────────────
    QSettings s;
    if (s.contains(QStringLiteral("ui/geometry")))
        restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray());
    if (s.contains(QStringLiteral("ui/windowState")))
        restoreState(s.value(QStringLiteral("ui/windowState")).toByteArray());
    if (s.contains(QStringLiteral("ui/splitterState")))
        m_splitter->restoreState(s.value(QStringLiteral("ui/splitterState")).toByteArray());
    // Restore saved polling interval into spinbox (block signal so we don't
    // call startPolling before login completes).
    {
        const int savedInterval = s.value(QStringLiteral("connection/interval"), 10).toInt();
        m_pollingInterval = savedInterval;
        m_intervalSpin->blockSignals(true);
        m_intervalSpin->setValue(savedInterval);
        m_intervalSpin->blockSignals(false);
    }
    // Header state is restored on first data arrival (onDeviceListUpdated),
    // because the model must be populated before restoreState is reliable.
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Save all UI layout state before closing
    QSettings s;
    s.setValue(QStringLiteral("ui/geometry"),      saveGeometry());
    s.setValue(QStringLiteral("ui/windowState"),   saveState());
    s.setValue(QStringLiteral("ui/splitterState"), m_splitter->saveState());
    s.setValue(QStringLiteral("ui/headerState"),   m_deviceTree->header()->saveState());

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
    KXmlGuiWindow::showEvent(event);
#else
    QMainWindow::showEvent(event);
#endif
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

#if HAVE_KF
    actionCollection()->addAction(QStringLiteral("file_connect"), connectAction);
    actionCollection()->addAction(QStringLiteral("file_refresh"), refreshAction);
    // Assign shortcuts via KActionCollection so KXmlGui can save/restore them.
    // Using QAction::setShortcut() directly triggers a kf.xmlgui warning.
    actionCollection()->setDefaultShortcut(connectAction, QKeySequence(Qt::CTRL | Qt::Key_L));
    actionCollection()->setDefaultShortcut(refreshAction, QKeySequence::Refresh);
    // Use our close()-based quit so closeEvent() runs on KStandardAction::Quit too.
    KStandardAction::quit(this, &MainWindow::close, actionCollection());

    // setupGUI loads fritzhomeui.rc (found by KDE via app name) which defines
    // the File menu order (Connect → Refresh → Separator → Quit) and no toolbar.
    setupGUI(Keys | StatusBar | Save | Create);

    // KMainWindow may still create or restore a toolbar from saved session state
    // even when the .rc file has no <ToolBar> block.  Remove all toolbars
    // unconditionally so none ever appears, regardless of saved config.
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

void MainWindow::onDeviceListUpdated(const FritzDeviceList &devices)
{
    // Save the selected AIN before the model reset: beginResetModel/endResetModel
    // clears the view selection, which fires currentChanged → onDeviceSelected
    // with an invalid index, which in turn clears m_selectedAin.  We must
    // capture it here, before that chain runs.
    const QString previousAin = m_selectedAin;

    // Save which groups are currently expanded (keyed by raw label via UserRole)
    // so we can restore the same state after the model reset.
    QSet<QString> expandedGroups;
    const int oldGroupCount = m_model->rowCount();
    for (int g = 0; g < oldGroupCount; ++g) {
        const QModelIndex gi = m_model->index(g, 0);
        if (m_deviceTree->isExpanded(gi))
            expandedGroups.insert(m_model->data(gi, Qt::UserRole).toString());
    }
    // If nothing was recorded yet (first load), default to expanding everything.
    const bool firstLoad = expandedGroups.isEmpty() && oldGroupCount == 0;

    m_model->updateDevices(devices);

    // On the first data load, restore saved column widths or — if none saved
    // yet — auto-size to content so names are fully visible.
    if (!m_initialColumnSizeDone && !devices.isEmpty()) {
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

    // Restore group expansion state.  On first load expand everything;
    // on subsequent updates re-apply whatever was open/closed before.
    {
        const int newGroupCount = m_model->rowCount();
        for (int g = 0; g < newGroupCount; ++g) {
            const QModelIndex gi = m_model->index(g, 0);
            const QString label  = m_model->data(gi, Qt::UserRole).toString();
            // firstLoad: expand all; otherwise expand only if it was expanded before.
            m_deviceTree->setExpanded(gi, firstLoad || expandedGroups.contains(label));
        }
    }

    // Keep the current selection / panel in sync.
    // Use previousAin because m_selectedAin was cleared by the model reset
    // triggering currentChanged → onDeviceSelected(invalid).
    if (!previousAin.isEmpty()) {
        // Iterate tree model: groups at top level, devices as children
        const int groupCount = m_model->rowCount();
        bool found = false;
        for (int g = 0; g < groupCount && !found; ++g) {
            const QModelIndex groupIdx = m_model->index(g, 0);
            const int devCount = m_model->rowCount(groupIdx);
            for (int d = 0; d < devCount && !found; ++d) {
                const QModelIndex leafIdx = m_model->index(d, 0, groupIdx);
                const FritzDevice dev = m_model->deviceAt(leafIdx);
                if (dev.ain == previousAin) {
                    found = true;
                    m_selectedAin = previousAin;
                    m_deviceTree->selectionModel()->blockSignals(true);
                    m_deviceTree->selectionModel()->setCurrentIndex(
                        leafIdx,
                        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                    m_deviceTree->selectionModel()->blockSignals(false);

                    m_deviceIconLabel->setPixmap(deviceHeadingPixmap(dev));
                    m_deviceIconLabel->show();
                    m_deviceNameLabel->setText(dev.name);
                    m_deviceNameLabel->show();
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
                    const FritzDeviceList memberDevs = dev.isGroup()
                        ? collectMemberDevices(dev) : FritzDeviceList();
                    m_chartWidget->updateRollingCharts(dev, memberDevs);
                    // Refresh energy history stats; throttle interval depends on the
                    // currently displayed view: 60 s for the 15-min/24-h chart
                    // (so the most-recent bar stays fresh), 5 min otherwise.
                    if (dev.hasEnergyMeter()) {
                        const int throttleSecs = (m_chartWidget->activeEnergyGrid() == 900) ? 60 : 300;
                        if (!m_lastStatsFetch.isValid() ||
                            m_lastStatsFetch.secsTo(QDateTime::currentDateTime()) >= throttleSecs) {
                            if (dev.isGroup()) {
                                FritzDeviceList energyMembers;
                                for (const FritzDevice &m : memberDevs)
                                    if (m.hasEnergyMeter())
                                        energyMembers.append(m);
                                // Reset group-fetch state before starting new fetches.
                                m_groupMemberStats.clear();
                                m_groupMemberOrder.clear();
                                m_groupStatsPending = 0;
                                m_groupAin.clear();
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
                            m_lastStatsFetch = QDateTime::currentDateTime();
                        }
                    }
                }
            }
        }
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
    QTimer::singleShot(300, m_api, &FritzApi::fetchDeviceList);
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

// ─────────────────────────────────────────────────────────────────────────────
// Device selection
// ─────────────────────────────────────────────────────────────────────────────

FritzDeviceList MainWindow::collectMemberDevices(const FritzDevice &groupDev) const
{
    FritzDeviceList members;
    for (const QString &memberId : groupDev.memberAins) {
        // memberAins contains device AIns resolved from the REST API's
        // memberUnitUids.  Look up by id; fall back to AIN lookup for
        // robustness.
        FritzDevice m = m_model->deviceById(memberId);
        if (m.ain.isEmpty())
            m = m_model->deviceByAin(memberId);
        if (!m.ain.isEmpty())
            members.append(m);
    }
    return members;
}

void MainWindow::onDeviceSelected(const QModelIndex &current, const QModelIndex &/*previous*/)
{
    if (!current.isValid() || m_model->isGroupHeader(current)) {
        m_controlStack->setCurrentIndex(PanelEmpty);
        m_selectedAin.clear();
        m_deviceIconLabel->hide();
        m_deviceNameLabel->hide();
        return;
    }

    const FritzDevice dev = m_model->deviceAt(current);
    m_selectedAin = dev.ain;
    m_deviceIconLabel->setPixmap(deviceHeadingPixmap(dev));
    m_deviceIconLabel->show();
    m_deviceNameLabel->setText(dev.name);
    m_deviceNameLabel->show();
    updateDevicePanel(dev);
    const FritzDeviceList memberDevs = dev.isGroup() ? collectMemberDevices(dev) : FritzDeviceList();
    m_chartWidget->updateDevice(dev, memberDevs);
    // Fetch detailed energy history immediately on selection
    if (dev.hasEnergyMeter()) {
        if (dev.isGroup()) {
            // For groups, fetch stats for each energy-capable member rather than
            // the group AIN itself (Fritz!Box does not return history for group AIns).
            FritzDeviceList energyMembers;
            for (const FritzDevice &m : memberDevs)
                if (m.hasEnergyMeter())
                    energyMembers.append(m);
            // Reset group-fetch state unconditionally before starting new fetches.
            // This also cancels any in-flight replies from a previous group selection
            // (the lambda guards against them via m_groupAin == m_selectedAin).
            m_groupMemberStats.clear();
            m_groupMemberOrder.clear();
            m_groupStatsPending = 0;
            m_groupAin.clear();
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
            // Non-group device: cancel any pending group fetch and fetch directly.
            m_groupMemberStats.clear();
            m_groupMemberOrder.clear();
            m_groupStatsPending = 0;
            m_groupAin.clear();
            m_api->fetchDeviceStats(dev.ain);
        }
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
        const FritzDeviceList members = collectMemberDevices(device);
        int  switchMembers        = 0;
        int  lockedMembers        = 0; // members where locked || deviceLocked (matches rebuildMenus predicate)
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
        return;
    }

    dw->updateDevice(device);
    dw->setMembers(FritzDeviceList()); // clear any stale member menus
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::setStatusMessage(const QString &msg)
{
    m_statusLabel->setText(msg);
    statusBar()->showMessage(msg, 0);
}
