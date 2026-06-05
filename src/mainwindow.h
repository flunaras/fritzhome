#pragma once

#if HAVE_KF
#  include <KXmlGuiWindow>
#else
#  include <QMainWindow>
#endif

#include <QSet>
#include <QString>
#include <QModelIndex>
#include <QDateTime>
#include <QShowEvent>
#include <QMap>

#include "fritzdevice.h"

class FritzApi;
class DeviceModel;
class DeviceWidget;
class ChartWidget;
class LocalGroupManager;
class QTreeView;
class QStackedWidget;
class QDockWidget;
class QLabel;
class QSpinBox;

/**
 * MainWindow is the top-level application window for Fritz!Box Smart Home.
 *
 *   Layout:
 *   ┌────────────────────────────────────────────────────────┐
 *   │  MenuBar                                               │
 *   ├──────────────┬──────────────────┬──────────────────────┤
 *   │              │  Device control  │                      │
 *   │ Device list  │  panel (stacked  │  Charts              │
 *   │ (dockable)   │  per type)       │  (dockable)          │
 *   │              │  (dockable)      │                      │
 *   └──────────────┴──────────────────┴──────────────────────┘
 *   │  Status bar                                            │
 *   └────────────────────────────────────────────────────────┘
 *
 * All three panels are hosted in QDockWidgets and can be docked, floated,
 * or hidden.  Visibility is toggled exclusively via the View menu (the
 * QMainWindow right-click context menu is suppressed by overriding
 * createPopupMenu()).  All positions and visibility are persisted via
 * QMainWindow::saveState().
 */
#if HAVE_KF
class MainWindow : public KXmlGuiWindow
#else
class MainWindow : public QMainWindow
#endif
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Called from main.cpp with values from CLI / LoginWindow
    void configure(const QString &host,
                   const QString &username,
                   const QString &password,
                   int pollingIntervalSeconds = 10,
                   bool ignoreSsl = false);

    // Trigger the login / settings dialog
    void showLoginDialog();

private slots:
    void onDeviceListUpdated(const FritzDeviceList &devices);
    void onDeviceSelected(const QModelIndex &current, const QModelIndex &previous);
    void onLoginSuccess();
    void onLoginFailed(const QString &error);
    void onSslError(const QString &details);
    void onSessionExpired();
    void onNetworkError(const QString &error);
    void onCommandSuccess(const QString &ain, const QString &cmd);
    void onCommandFailed(const QString &ain, const QString &error);
    void onLocalGroupsChanged();

    void actionConnect();
    void actionRefresh();
    void actionSettings();
    void actionManageLocalGroups();

private:
    void setupActions();
    void setupStatusBar();
    void setupDeviceTree();
    void setupControlPanel();
    void wireSignals();
    void restoreSettings();
    void updateDevicePanel(const FritzDevice &device);
    void setStatusMessage(const QString &msg);
    /// Collect all member FritzDevice objects for a group device.
    FritzDeviceList collectMemberDevices(const FritzDevice &groupDev) const;
    /// Internal recursive implementation with cycle-detection and deduplication.
    FritzDeviceList collectMemberDevicesImpl(const FritzDevice &groupDev,
                                              QSet<QString> &visited,
                                              QSet<QString> &seenAins) const;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    /// Override to suppress the QMainWindow right-click context menu on dock
    /// title bars; the View menu provides proper named toggle actions instead.
    QMenu *createPopupMenu() override { return nullptr; }

    /// Persist producer/consumer status for a device and rebuild charts.
    /// Called from device widget producerStatusChanged signals.
    void setDeviceProducerStatus(const QString &ain, bool isProducer);
    /// Load all producer/consumer settings from QSettings into device model.
    void loadProducerSettings();

    /// Persist native net power status for a device and rebuild charts.
    /// Called from device widget nativeNetPowerChanged signals.
    void setDeviceNativeNetPowerStatus(const QString &ain, bool nativeNetPower);
    /// Load all native net power settings from QSettings into device model.
    void loadNativeNetPowerSettings();

    // ── Per-connection tree state persistence ───────────────────────────────
    /// Build the QSettings key prefix for the currently configured connection
    /// (host + username). Returns an empty string if either is empty.
    /// Used to keep tree state separate per Fritz!Box / per user account.
    QString connectionStateKey() const;
    /// Persist the tree state (expanded groups + selected AIN) for the active
    /// connection. No-op if no successful login has happened this session.
    void saveConnectionTreeState();
    /// Load saved tree state for the active connection into m_pendingExpanded
    /// and m_pendingSelectedAin so the next device-list update can apply them.
    void loadConnectionTreeState();

    // ── onDeviceListUpdated helpers ───────────────────────────────────────
    /// Save expanded-group labels from the device tree (before model reset).
    QSet<QString> saveTreeState() const;
    /// Re-expand groups whose labels are in @p expandedGroups (or all if @p expandAll).
    void restoreTreeState(const QSet<QString> &expandedGroups, bool expandAll = false);
    /// First-load column width setup (uses saved header state or auto-size).
    void initColumnSizes(const FritzDeviceList &devices);
    /// Re-select a device by AIN after model reset, restoring panel and charts.
    void reselectDevice(const QString &ain);
    /// Throttled energy stats fetch — skips if too recent for current chart grid.
    void fetchEnergyStatsIfDue(const FritzDevice &dev, const FritzDeviceList &memberDevs);
    /// Fetch stats for a group's energy-capable members or a single device.
    void fetchGroupOrDeviceStats(const FritzDevice &dev, const FritzDeviceList &memberDevs);
    /// Synthesize group-level switch state (lock, mixed, partial) from members.
    void synthesizeGroupSwitchState(FritzDevice &dev, DeviceWidget *dw) const;

    // Core objects
    FritzApi          *m_api                = nullptr;
    DeviceModel       *m_model              = nullptr;
    LocalGroupManager *m_localGroupManager  = nullptr;
    int                m_pollingInterval    = 10;
    FritzDeviceList    m_lastFritzDevices;  ///< last device list for local group synthesis

    // UI
    QDockWidget    *m_deviceDock      = nullptr;
    QDockWidget    *m_controlDock     = nullptr;
    QDockWidget    *m_chartDock       = nullptr;
    QTreeView      *m_deviceTree      = nullptr;
    QSpinBox       *m_intervalSpin   = nullptr;
    QStackedWidget *m_controlStack   = nullptr;
     ChartWidget    *m_chartWidget    = nullptr;
     QLabel         *m_statusLabel     = nullptr;
     QLabel         *m_deviceIconLabel = nullptr;  ///< icon shown left of the device name heading (control dock)
     QLabel         *m_deviceNameLabel = nullptr;
     QLabel         *m_chartIconLabel  = nullptr;  ///< icon shown left of the device name heading (chart dock)
     QLabel         *m_chartNameLabel  = nullptr;

    // Track which device is selected (for refreshing the panel)
    QString         m_selectedAin;
    bool            m_initialColumnSizeDone = false;
    QDateTime       m_lastStatsFetch;       // throttle poll-driven fetchDeviceStats

    // After a switch command is sent, the Fritz!Box may transiently report
    // inconsistent state (e.g. locked members briefly shown as "on") for the
    // first poll or two.  We suppress updateDevicePanel for the selected group
    // until this deadline has passed, so the label never flashes incorrectly.
    QDateTime       m_suppressPanelUntil;   // invalid = not suppressed
    QString         m_suppressForAin;       // AIN of group being suppressed

    // Group energy history: accumulate per-member basicStats replies.
    // Key = member AIN, Value = the stats that arrived.
    // m_groupStatsPending tracks how many member fetches are still in flight
    // for the current group selection; when it reaches 0 the chart is built.
    // m_groupAin is the AIN of the group whose members we are fetching; stale
    // replies for a previous group are ignored if m_selectedAin has changed.
    QMap<QString, DeviceBasicStats> m_groupMemberStats;
    QStringList                     m_groupMemberOrder; ///< AIns in device-list order
    int                             m_groupStatsPending = 0;
    QString                         m_groupAin;         ///< AIN of the group being fetched

    // ── Per-connection tree state ─────────────────────────────────────────
    /// True once onLoginSuccess() has fired for the active connection.
    /// Only when this is true do we persist tree state on close — otherwise
    /// a failed/aborted login would overwrite a previous good state.
    bool        m_loginSucceeded = false;
    /// Saved expanded-group labels waiting to be applied on the first
    /// device-list update following a successful login.
    QSet<QString> m_pendingExpandedGroups;
    /// Saved selected device AIN waiting to be applied on the first
    /// device-list update following a successful login. Empty once consumed.
    QString     m_pendingSelectedAin;
    /// True while we are waiting to apply the restored tree state.
    bool        m_pendingTreeRestore = false;
    /// True once dock/geometry state has been restored on the first show event.
    bool        m_windowStateRestored = false;
};
