#pragma once

#include <QObject>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSet>
#include <QString>
#include <QList>
#include <QTimer>
#include <QDomElement>
#include <QJsonObject>
#include "fritzdevice.h"

/**
 * FritzApi implements the Fritz!Box Smart Home REST API.
 *
 * Authentication flow:
 *   1. GET /login_sid.lua?version=2  -> challenge (pbkdf2 or MD5)
 *   2. Compute response hash
 *   3. GET /login_sid.lua?version=2&username=...&response=... -> SID
 *   4. Use SID as HTTP header "Authorization: AVM-SID <SID>" for all REST API calls
 */
class FritzApi : public QObject
{
    Q_OBJECT
public:
    explicit FritzApi(QObject *parent = nullptr);

    void setHost(const QString &host);
    void setCredentials(const QString &username, const QString &password);
    void setIgnoreSsl(bool ignore);

    QString host() const { return m_host; }
    QString username() const { return m_username; }
    bool isLoggedIn() const { return m_sid != "0000000000000000" && !m_sid.isEmpty(); }

    // Authentication
    void login();
    void logout();

    // Device list
    void fetchDeviceList();

    // Per-device historical stats (REST: GET /smarthome/overview/units/{unitUID})
    void fetchDeviceStats(const QString &ain);

    // Switch commands
    void setSwitchOn(const QString &ain);
    void setSwitchOff(const QString &ain);
    void setSwitchToggle(const QString &ain);

    // Thermostat (HKR)
    // temp: 16=8°C .. 56=28°C, 253=off, 254=on (=comfort)
    void setThermostatTarget(const QString &ain, int tempValue);

    // Dimmer / bulb level (0-255)
    void setLevel(const QString &ain, int level);
    void setLevelPercentage(const QString &ain, int percent);

    // Color bulb
    void setColor(const QString &ain, int hue, int saturation, int duration = 0);
    void setColorTemperature(const QString &ain, int kelvin, int duration = 0);

    // Blind / roller shutter
    void setBlind(const QString &ain, const QString &target); // "open"/"close"/"stop"

    // Start periodic polling
    void startPolling(int intervalMs = 5000);
    void stopPolling();

    // Abort all in-flight network requests (call before application teardown)
    void abortPendingRequests();

signals:
    void loginSuccess();
    void loginFailed(const QString &error);
    void sslError(const QString &details);   ///< emitted when a TLS certificate error occurs
    /**
     * Emitted when the Fritz!Box session has expired (HTTP 401 detected from a
     * REST API response) and an automatic re-login has been initiated.  The caller
     * should update the UI to reflect the reconnecting state; it must NOT open
     * the login dialog — the re-login happens silently.  If re-login succeeds,
     * loginSuccess() is emitted; if it fails, loginFailed() is emitted as usual.
     */
    void sessionExpired();
    void deviceListUpdated(const FritzDeviceList &devices);
    void deviceStatsUpdated(const QString &ain, const DeviceBasicStats &stats);
    void deviceStatsError(const QString &ain, const QString &error);
    void commandSuccess(const QString &ain, const QString &command);
    void commandFailed(const QString &ain, const QString &error);
    void networkError(const QString &error);

private slots:
    void onLoginChallengeReply(QNetworkReply *reply);
    void onLoginResponseReply(QNetworkReply *reply);
    void onDeviceListReply(QNetworkReply *reply);
    void onDeviceStatsReply(QNetworkReply *reply, const QString &ain);
    void onCommandReply(QNetworkReply *reply, const QString &ain, const QString &cmd);
    void onPollTimer();

private:
    QString computeResponse(const QString &challenge) const;
    QString computePbkdf2Response(const QString &challengeLine) const;
    QString computeMd5Response(const QString &challenge) const;
    QUrl buildCommandUrl(const QString &command, const QString &ain = QString()) const;

    // REST API helpers
    QNetworkRequest buildRestRequest(const QString &path) const;
    QNetworkReply *put(const QNetworkRequest &req, const QByteArray &body);

    // Wrapper around QNetworkAccessManager::get() that registers the reply in
    // m_pendingReplies for clean abort on shutdown.
    QNetworkReply *get(const QNetworkRequest &req);

    // REST JSON parsing
    FritzDeviceList parseDeviceListJson(const QByteArray &json) const;
    DeviceBasicStats parseUnitStatsJson(const QByteArray &json) const;

    // REST control helper: issue PUT to a unit's endpoint with a JSON interfaces body
    void putUnitInterfaces(const QString &ain, const QJsonObject &interfaces,
                           const QString &cmdName);

    // Lookup unitUID for a given AIN in m_devices
    QString unitUIDForAin(const QString &ain) const;

    /**
     * Called when a poll or command response indicates the session has expired
     * (HTTP 401 from the REST API).
     *
     * Resets the SID to the null sentinel, emits sessionExpired(), and
     * attempts an automatic re-login — unless we have already exceeded
     * kMaxReloginAttempts, in which case loginFailed() is emitted to let the
     * user intervene.
     */
    void handleSessionExpiry();

    QString m_host;
    QString m_username;
    QString m_password;
    QString m_sid;
    bool    m_ignoreSsl = false;

    // Auto-relogin state: how many consecutive relogin attempts have been made
    // since the last successful login.  Reset to 0 on loginSuccess().
    // Capped at kMaxReloginAttempts to prevent infinite loops when credentials
    // are permanently invalid.
    int     m_reloginAttempts = 0;
    static constexpr int kMaxReloginAttempts = 3;

    QNetworkAccessManager *m_nam;
    QTimer *m_pollTimer;
    FritzDeviceList m_devices;
    QSet<QNetworkReply *> m_pendingReplies;  // track in-flight requests for abort
};
