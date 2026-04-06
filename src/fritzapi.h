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
#include <QDateTime>
#include <QMap>
#include <functional>
#include "fritzdevice.h"

/**
 * FritzApi implements the Fritz!Box Smart Home REST API with:
 *   - Asynchronous HTTP requests (true async/await pattern via callbacks)
 *   - Request deduplication (in-flight requests are shared among callers)
 *   - Response caching with TTL-based invalidation
 *   - Minimal request overhead to Fritz!Box
 *
 * Authentication flow:
 *   1. GET /login_sid.lua?version=2  -> challenge (pbkdf2 or MD5)
 *   2. Compute response hash
 *   3. GET /login_sid.lua?version=2&username=...&response=... -> SID
 *   4. Use SID as HTTP header "Authorization: AVM-SID <SID>" for all REST API calls
 *
 * Caching strategy:
 *   - Device list: cached for 2 seconds (updated by polling anyway)
 *   - Device stats: cached for 5 seconds (on-demand stats fetch)
 *   - Shared cache keys prevent duplicate requests during overlapping calls
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

    // Device list (async with callback)
    // If result is cached and valid, callback is called immediately (synchronously)
    // Otherwise, callback is called when the network request completes
    // Multiple simultaneous calls to fetchDeviceList will share the same network request
    using DeviceListCallback = std::function<void(const FritzDeviceList &)>;
    using ErrorCallback = std::function<void(const QString &)>;
    void fetchDeviceList(DeviceListCallback onSuccess, ErrorCallback onError = nullptr);

    // Backward compatibility: old signal-based API (deprecated)
    // These overloads call fetchDeviceList(callback) with a callback that emits signals
    void fetchDeviceList();

    // Per-device historical stats (REST: GET /smarthome/overview/units/{unitUID})
    // Multiple calls for the same AIN will share the same network request
    using DeviceStatsCallback = std::function<void(const DeviceBasicStats &)>;
    void fetchDeviceStats(const QString &ain, DeviceStatsCallback onSuccess, ErrorCallback onError = nullptr);

    // Backward compatibility: old signal-based API (deprecated)
    // These overloads call fetchDeviceStats(ain, callback) with a callback that emits signals
    void fetchDeviceStats(const QString &ain);

    // Switch commands (these perform immediately, then refresh device list)
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

    // Invalidate all caches (e.g., after a command is issued)
    void invalidateAllCaches();

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
    // Legacy signals kept for backward compatibility with polling flows
    void deviceListUpdated(const FritzDeviceList &devices);
    void deviceStatsUpdated(const QString &ain, const DeviceBasicStats &stats);
    void deviceStatsError(const QString &ain, const QString &error);
    void commandSuccess(const QString &ain, const QString &command);
    void commandFailed(const QString &ain, const QString &error);
    void networkError(const QString &error);

private slots:
    void onLoginChallengeReply(QNetworkReply *reply);
    void onLoginResponseReply(QNetworkReply *reply);
    void onPollTimer();

private:
    // ---- Cache Entry Structure ----
    struct CacheEntry {
        QByteArray data;
        QDateTime timestamp;
        int ttlSeconds;
        QList<std::pair<DeviceListCallback, ErrorCallback>> deviceListCallbacks;
        QList<std::pair<DeviceStatsCallback, ErrorCallback>> deviceStatsCallbacks;
        QNetworkReply *pendingReply = nullptr;  // non-null if request is in-flight
    };

    // ---- Cache Management ----
    bool isCacheValid(const CacheEntry &entry) const;
    void clearAllCaches();

    // ---- Async HTTP helpers ----
    QString computeResponse(const QString &challenge) const;
    QString computePbkdf2Response(const QString &challengeLine) const;
    QString computeMd5Response(const QString &challenge) const;

    // REST API helpers
    QNetworkRequest buildRestRequest(const QString &path) const;
    QNetworkReply *put(const QNetworkRequest &req, const QByteArray &body);

    // Wrapper around QNetworkAccessManager::get() that registers the reply in
    // m_pendingReplies for clean abort on shutdown.
    QNetworkReply *get(const QNetworkRequest &req);

    // Reply handler that supports multiple callbacks via cache entries
    void onAsyncReply(const QString &cacheKey, bool isDeviceStats, const QString &ain);

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

    // ---- Response Cache ----
    // Maps cache key → CacheEntry (device list, device stats, etc.)
    QMap<QString, CacheEntry> m_cache;
    static constexpr int kDeviceListCacheTtl = 2;   // 2 seconds
    static constexpr int kDeviceStatsCacheTtl = 5;  // 5 seconds
};
