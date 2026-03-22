#pragma once

#if HAVE_KF
#  include <KXmlGuiWindow>
#else
#  include <QMainWindow>
#endif

#include <QString>
#include <QModelIndex>
#include <QDateTime>
#include <QShowEvent>
#include <QMap>

#include "fritzdevice.h"

class FritzApi;
class DeviceModel;
class ChartWidget;
class QTreeView;
class QStackedWidget;
class QSplitter;
class QLabel;
class QSpinBox;

/**
 * MainWindow is the top-level application window for Fritz!Box Smart Home.
 *
 * Layout:
 *   ┌──────────────────────────────────────────┐
 *   │  MenuBar  /  ToolBar                     │
 *   ├──────────────┬───────────────────────────┤
 *   │              │  Device control panel     │
 *   │ Device list  │  (stacked per type)       │
 *   │ (QTreeView)  ├───────────────────────────┤
 *   │              │  Charts (ChartWidget)     │
 *   └──────────────┴───────────────────────────┘
 *   │  Status bar                              │
 *   └──────────────────────────────────────────┘
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

    void actionConnect();
    void actionRefresh();
    void actionSettings();

private:
    void setupActions();
    void setupStatusBar();
    void updateDevicePanel(const FritzDevice &device);
    void setStatusMessage(const QString &msg);
    /// Collect all member FritzDevice objects for a group device.
    FritzDeviceList collectMemberDevices(const FritzDevice &groupDev) const;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

    // Core objects
    FritzApi    *m_api    = nullptr;
    DeviceModel *m_model  = nullptr;
    int          m_pollingInterval = 10;

    // UI
    QSplitter      *m_splitter       = nullptr;
    QTreeView      *m_deviceTree     = nullptr;
    QSpinBox       *m_intervalSpin   = nullptr;
    QStackedWidget *m_controlStack   = nullptr;
    ChartWidget    *m_chartWidget    = nullptr;
    QLabel         *m_statusLabel     = nullptr;
    QLabel         *m_deviceIconLabel = nullptr;  ///< icon shown left of the device name heading
    QLabel         *m_deviceNameLabel = nullptr;

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
};
