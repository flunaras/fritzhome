#include "fritzapi.h"
#include "i18n_shim.h"

#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSslError>
#include <QUrl>
#include <QUrlQuery>
#include <QDomDocument>
#include <QCryptographicHash>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#  include <QTextCodec>
#else
#  include <QStringConverter>
#endif
#include <QTimer>
#include <QDebug>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <limits>
#include <cmath>

// PBKDF2-HMAC-SHA256 requires OpenSSL or manual impl; we use Qt's built-in
#include <QMessageAuthenticationCode>

FritzApi::FritzApi(QObject *parent)
    : QObject(parent)
    , m_sid(QStringLiteral("0000000000000000"))
    , m_nam(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(5000);
    connect(m_pollTimer, &QTimer::timeout, this, &FritzApi::onPollTimer);
}

// ---- Cache Management ----

bool FritzApi::isCacheValid(const CacheEntry &entry) const
{
    if (entry.data.isEmpty())
        return false;
    QDateTime now = QDateTime::currentDateTime();
    int ageSeconds = entry.timestamp.secsTo(now);
    return ageSeconds < entry.ttlSeconds;
}

void FritzApi::clearAllCaches()
{
    m_cache.clear();
}

void FritzApi::invalidateAllCaches()
{
    clearAllCaches();
}

// ---- Network helpers ----

// Private helper: issue a GET request and register the reply in
// m_pendingReplies for clean abort on shutdown.
QNetworkReply *FritzApi::get(const QNetworkRequest &req)
{
    QNetworkReply *reply = m_nam->get(req);
    if (m_ignoreSsl) {
        reply->ignoreSslErrors();
    } else {
        // When not ignoring SSL errors, forward the detailed QSslError list as a
        // human-readable string.  Qt will still abort the request after this
        // signal, but callers can show a better error message from the details.
        connect(reply, &QNetworkReply::sslErrors, this,
                [this, reply](const QList<QSslError> &errors) {
            QStringList msgs;
            msgs.reserve(errors.size());
            for (const QSslError &e : errors)
                msgs.append(e.errorString());
            reply->setProperty("sslErrorReported", true);
            emit sslError(msgs.join(QStringLiteral("\n")));
        });
    }
    m_pendingReplies.insert(reply);
    // Remove from the tracking set when the reply is eventually deleted
    // (via deleteLater()).  Use QPointer<FritzApi> so that the lambda is safe
    // even if FritzApi is destroyed before the deferred deletion fires.
    QPointer<FritzApi> self(this);
    connect(reply, &QNetworkReply::destroyed, this, [self, reply]() {
        if (self)
            self->m_pendingReplies.remove(reply);
    });
    return reply;
}

// Private helper: issue a PUT request and register the reply in
// m_pendingReplies for clean abort on shutdown.
QNetworkReply *FritzApi::put(const QNetworkRequest &req, const QByteArray &body)
{
    QNetworkReply *reply = m_nam->put(req, body);
    if (m_ignoreSsl) {
        reply->ignoreSslErrors();
    } else {
        connect(reply, &QNetworkReply::sslErrors, this,
                [this, reply](const QList<QSslError> &errors) {
            QStringList msgs;
            msgs.reserve(errors.size());
            for (const QSslError &e : errors)
                msgs.append(e.errorString());
            reply->setProperty("sslErrorReported", true);
            emit sslError(msgs.join(QStringLiteral("\n")));
        });
    }
    m_pendingReplies.insert(reply);
    QPointer<FritzApi> self(this);
    connect(reply, &QNetworkReply::destroyed, this, [self, reply]() {
        if (self)
            self->m_pendingReplies.remove(reply);
    });
    return reply;
}

// Build a QNetworkRequest for a REST API path, adding the Authorization header.
// The FRITZ!Box REST API requires the header value to be "AVM-SID <sid>".
QNetworkRequest FritzApi::buildRestRequest(const QString &path) const
{
    QUrl url(m_host + path);
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", ("AVM-SID " + m_sid).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));
    return req;
}

// ---- Configuration ----

void FritzApi::setIgnoreSsl(bool ignore)
{
    m_ignoreSsl = ignore;
}

void FritzApi::setHost(const QString &host)
{
    m_host = host;
    if (!m_host.startsWith("http://") && !m_host.startsWith("https://")) {
        m_host.prepend("http://");
    }
    // strip trailing slash
    while (m_host.endsWith('/'))
        m_host.chop(1);
}

void FritzApi::setCredentials(const QString &username, const QString &password)
{
    m_username = username;
    m_password = password;
}

// ---- Authentication ----

void FritzApi::login()
{
    if (m_host.isEmpty()) {
        emit loginFailed(i18n("No host configured"));
        return;
    }
    // Step 1: request challenge
    QUrl url(m_host + "/login_sid.lua");
    QUrlQuery q;
    q.addQueryItem("version", "2");
    url.setQuery(q);

    QNetworkRequest req(url);
    auto *reply = get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLoginChallengeReply(reply);
    });
}

void FritzApi::logout()
{
    QUrl url(m_host + "/login_sid.lua");
    QUrlQuery q;
    q.addQueryItem("logout", "1");
    q.addQueryItem("sid", m_sid);
    url.setQuery(q);
    m_nam->get(QNetworkRequest(url));  // fire-and-forget logout (no reply handler needed)
    m_sid = QStringLiteral("0000000000000000");
}

void FritzApi::onLoginChallengeReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError
                && !reply->property("sslErrorReported").toBool())
            emit loginFailed(reply->errorString());
        return;
    }
    QByteArray data = reply->readAll();
    QDomDocument doc;
    doc.setContent(data);

    // Check existing valid SID
    QString sid = doc.documentElement().firstChildElement("SID").text();
    if (sid != "0000000000000000" && !sid.isEmpty()) {
        m_sid = sid;
        emit loginSuccess();
        return;
    }

    QString challenge = doc.documentElement().firstChildElement("Challenge").text();
    if (challenge.isEmpty()) {
        emit loginFailed(i18n("Empty challenge from Fritz!Box"));
        return;
    }

    // Compute response
    QString response = computeResponse(challenge);

    // Step 2: send username + response
    QUrl url(m_host + "/login_sid.lua");
    QUrlQuery q;
    q.addQueryItem("version", "2");
    q.addQueryItem("username", m_username);
    q.addQueryItem("response", response);
    url.setQuery(q);

    QNetworkRequest req(url);
    auto *reply2 = get(req);
    connect(reply2, &QNetworkReply::finished, this, [this, reply2]() {
        onLoginResponseReply(reply2);
    });
}

void FritzApi::onLoginResponseReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError
                && !reply->property("sslErrorReported").toBool())
            emit loginFailed(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    QDomDocument doc;
    doc.setContent(data);

    m_sid = doc.documentElement().firstChildElement("SID").text();
    int blockTime = doc.documentElement().firstChildElement("BlockTime").text().toInt();

    if (m_sid == "0000000000000000" || m_sid.isEmpty()) {
        QString msg = i18n("Login failed.");
        if (blockTime > 0) {
            msg += i18n(" Fritz!Box is blocking login for %1 seconds.", blockTime);
        }
        emit loginFailed(msg);
        return;
    }

    m_reloginAttempts = 0;  // successful login — reset the relogin counter
    emit loginSuccess();
}

QString FritzApi::computeResponse(const QString &challenge) const
{
    // New PBKDF2 format: "2$<iter1>$<salt1>$<iter2>$<salt2>"
    if (challenge.startsWith("2$")) {
        return computePbkdf2Response(challenge);
    }
    // Legacy MD5 format: plain hex challenge
    return computeMd5Response(challenge);
}

QString FritzApi::computeMd5Response(const QString &challenge) const
{
    // Fritz!Box MD5: hash of "challenge-password" encoded as UTF-16LE
    QString toHash = challenge + "-" + m_password;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QTextCodec *codec = QTextCodec::codecForName("UTF-16LE");
    QByteArray encoded = codec ? codec->fromUnicode(toHash) : toHash.toUtf8();
#else
    QStringEncoder encoder(QStringConverter::Utf16LE);
    QByteArray encoded = encoder.encode(toHash);
#endif

    QByteArray hash = QCryptographicHash::hash(encoded, QCryptographicHash::Md5);
    return challenge + "-" + QString::fromLatin1(hash.toHex());
}

QString FritzApi::computePbkdf2Response(const QString &challengeLine) const
{
    // Format: "2$iter1$salt1$iter2$salt2"
    QStringList parts = challengeLine.split('$');
    if (parts.size() != 5) {
        qWarning() << "Invalid PBKDF2 challenge format:" << challengeLine;
        return computeMd5Response(challengeLine);
    }

    int iter1 = parts[1].toInt();
    QByteArray salt1 = QByteArray::fromHex(parts[2].toLatin1());
    int iter2 = parts[3].toInt();
    QByteArray salt2 = QByteArray::fromHex(parts[4].toLatin1());

    QByteArray pwBytes = m_password.toUtf8();

    // PBKDF2-HMAC-SHA256
    auto pbkdf2 = [](const QByteArray &password, const QByteArray &salt, int iterations) -> QByteArray {
        QByteArray u = salt;
        u.append(char(0)); u.append(char(0)); u.append(char(0)); u.append(char(1));
        QByteArray result = QMessageAuthenticationCode::hash(u, password, QCryptographicHash::Sha256);
        QByteArray prev = result;
        for (int i = 1; i < iterations; ++i) {
            prev = QMessageAuthenticationCode::hash(prev, password, QCryptographicHash::Sha256);
            for (int j = 0; j < result.size(); ++j)
                result[j] = result[j] ^ prev[j];
        }
        return result;
    };

    QByteArray hash1 = pbkdf2(pwBytes, salt1, iter1);
    QByteArray hash2 = pbkdf2(hash1, salt2, iter2);

    return parts[4] + "$" + QString::fromLatin1(hash2.toHex());
}

// ---- Device list (REST: GET /api/v0/smarthome/overview) ----

void FritzApi::fetchDeviceList(DeviceListCallback onSuccess, ErrorCallback onError)
{
    if (!isLoggedIn()) {
        if (onError)
            onError(i18n("Not logged in"));
        return;
    }

    const QString cacheKey = QStringLiteral("device-list");

    // Check if we have valid cached data
    if (m_cache.contains(cacheKey)) {
        const CacheEntry &entry = m_cache.value(cacheKey);
        if (isCacheValid(entry)) {
            // Return cached data immediately (synchronously)
            FritzDeviceList devices = parseDeviceListJson(entry.data);
            onSuccess(devices);
            return;
        }
    }

    // Not cached or expired — check if request is already in-flight
    if (m_cache.contains(cacheKey) && m_cache[cacheKey].pendingReply) {
        // Request already in-flight, just queue the callback
        m_cache[cacheKey].deviceListCallbacks.append({onSuccess, onError});
        return;
    }

    // Start new network request
    QNetworkRequest req = buildRestRequest(QStringLiteral("/api/v0/smarthome/overview"));
    auto *reply = get(req);

    // Initialize cache entry with pending request
    CacheEntry &entry = m_cache[cacheKey];
    entry.ttlSeconds = kDeviceListCacheTtl;
    entry.pendingReply = reply;
    entry.deviceListCallbacks.append({onSuccess, onError});

    connect(reply, &QNetworkReply::finished, this, [this, cacheKey]() {
        onAsyncReply(cacheKey, false, QString());
    });
}

void FritzApi::onAsyncReply(const QString &cacheKey, bool isDeviceStats, const QString &ain)
{
    if (!m_cache.contains(cacheKey)) {
        qWarning() << "onAsyncReply: cache entry not found for key" << cacheKey;
        return;
    }

    CacheEntry &entry = m_cache[cacheKey];
    QNetworkReply *reply = entry.pendingReply;

    if (!reply) {
        qWarning() << "onAsyncReply: no pending reply found";
        return;
    }

    reply->deleteLater();
    entry.pendingReply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        // HTTP 401 means session expired
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpStatus == 401) {
            handleSessionExpiry();
            // Clear cache and notify all callbacks
            if (isDeviceStats) {
                for (auto &[cb, errCb] : entry.deviceStatsCallbacks) {
                    if (errCb)
                        errCb(i18n("Session expired"));
                }
            } else {
                for (auto &[cb, errCb] : entry.deviceListCallbacks) {
                    if (errCb)
                        errCb(i18n("Session expired"));
                }
            }
            m_cache.remove(cacheKey);
            return;
        }

        QString errorMsg = reply->errorString();

        // Notify all waiting callbacks (callbacks are responsible for signal emission)
        if (isDeviceStats) {
            for (auto &[cb, errCb] : entry.deviceStatsCallbacks) {
                if (errCb)
                    errCb(errorMsg);
            }
        } else {
            for (auto &[cb, errCb] : entry.deviceListCallbacks) {
                if (errCb)
                    errCb(errorMsg);
            }
        }
        m_cache.remove(cacheKey);
        return;
    }

    // Success! Cache the response and invoke all callbacks
    QByteArray data = reply->readAll();

    if (isDeviceStats) {
        DeviceBasicStats stats = parseUnitStatsJson(data);
        if (stats.valid) {
            // Cache only valid stats responses; invalid ones (e.g. empty
            // "statistics" object) must not be cached so the next request
            // retries from the network instead of re-serving stale data.
            entry.data = data;
            entry.timestamp = QDateTime::currentDateTime();
            for (FritzDevice &dev : m_devices) {
                if (dev.ain == ain) {
                    dev.basicStats = stats;
                    break;
                }
            }
        } else {
            // Don't cache — remove the entry so the next fetch goes to the network.
            // (We still invoke callbacks below before removing.)
        }
        // Invoke all waiting callbacks (callbacks are responsible for signal emission)
        for (auto &[cb, errCb] : entry.deviceStatsCallbacks) {
            cb(stats);
        }
        entry.deviceStatsCallbacks.clear();
        if (!stats.valid)
            m_cache.remove(cacheKey);
    } else {
        entry.data = data;
        entry.timestamp = QDateTime::currentDateTime();
        FritzDeviceList fresh = parseDeviceListJson(data);

        // Carry history forward from the previous poll into the freshly parsed devices
        QDateTime now = QDateTime::currentDateTime();
        for (FritzDevice &dev : fresh) {
            for (FritzDevice &old : m_devices) {
                if (old.ain != dev.ain)
                    continue;

                // Move history lists from the previous poll — avoids deep
                // copy-on-write when we append() below.  The old device
                // is not used after this point.
                dev.temperatureHistory  = std::move(old.temperatureHistory);
                dev.powerHistory        = std::move(old.powerHistory);
                dev.humidityHistory     = std::move(old.humidityHistory);
                dev.basicStats          = std::move(old.basicStats);

                if (dev.hasTemperature() && dev.temperature > -273.0)
                    dev.temperatureHistory.append({now, dev.temperature});
                if (dev.hasEnergyMeter() && dev.energyStats.valid)
                    dev.powerHistory.append({now, dev.energyStats.power});
                if (dev.hasHumidity() && dev.humidityStats.valid)
                    dev.humidityHistory.append({now, static_cast<double>(dev.humidityStats.humidity)});

                // Keep history bounded to the last 24 hours
                const QDateTime cutoff = now.addSecs(-24 * 3600);
                while (!dev.temperatureHistory.isEmpty() && dev.temperatureHistory.first().first < cutoff)
                    dev.temperatureHistory.removeFirst();
                while (!dev.powerHistory.isEmpty() && dev.powerHistory.first().first < cutoff)
                    dev.powerHistory.removeFirst();
                while (!dev.humidityHistory.isEmpty() && dev.humidityHistory.first().first < cutoff)
                    dev.humidityHistory.removeFirst();
                break;
            }
        }

        m_devices = fresh;

        // Invoke all waiting callbacks (callbacks are responsible for signal emission)
        for (auto &[cb, errCb] : entry.deviceListCallbacks) {
            cb(m_devices);
        }
        entry.deviceListCallbacks.clear();
    }
}

// ── Interface parsing helpers ──────────────────────────────────────────────

/**
 * Synthesize FritzDevice::functionBitmask from a unit's interfaces object.
 *
 * Each interface key maps to a specific function bit in the legacy AHA
 * bitmask so that existing has*() queries (hasSwitch(), hasThermostat(), …)
 * work unchanged against REST-sourced data.
 */
static void synthesizeFunctionBitmask(const QJsonObject &ifaces, FritzDevice &dev)
{
    static const struct { const char *key; int bit; } kMap[] = {
        { "onOffInterface",         9  },   // SWITCH
        { "multimeterInterface",    7  },   // ENERGY_METER
        { "temperatureInterface",   8  },   // TEMPERATURE
        { "thermostatInterface",    6  },   // THERMOSTAT/HKR
        { "levelControlInterface",  13 },   // DIMMER
        { "colorControlInterface",  14 },   // COLOR_BULB
        { "blindInterface",         18 },   // BLIND2
        { "humidityInterface",      20 },   // HUMIDITY
        { "alertInterface",         4  },   // ALARM
    };
    for (const auto &entry : kMap) {
        if (ifaces.contains(QLatin1String(entry.key)))
            dev.functionBitmask |= (1 << entry.bit);
    }
}

/**
 * Parse current device state from a unit's interfaces JSON.
 *
 * Populates the relevant *Stats substruct for every interface the device
 * advertises.  Called for both physical devices and groups — groups simply
 * won't have dimmer/color/blind/humidity/alarm interfaces, so those branches
 * are naturally skipped.
 */
static void parseInterfaceState(const QJsonObject &ifaces, FritzDevice &dev)
{
    if (dev.hasTemperature()) {
        QJsonObject ti = ifaces.value(QStringLiteral("temperatureInterface")).toObject();
        dev.temperature = ti.value(QStringLiteral("celsius")).toDouble(-273.0);
    }

    if (dev.hasSwitch()) {
        QJsonObject oi = ifaces.value(QStringLiteral("onOffInterface")).toObject();
        dev.switchStats.on           = oi.value(QStringLiteral("active")).toBool();
        dev.switchStats.locked       = oi.value(QStringLiteral("isLockedDeviceApi")).toBool();
        dev.switchStats.deviceLocked = oi.value(QStringLiteral("isLockedDeviceLocal")).toBool();
        dev.switchStats.valid = true;
    }

    if (dev.hasEnergyMeter()) {
        QJsonObject mi = ifaces.value(QStringLiteral("multimeterInterface")).toObject();
        // power in mW → divide by 1000 for W
        dev.energyStats.power   = mi.value(QStringLiteral("power")).toDouble(0.0) / 1000.0;
        // energy in Wh — already correct
        dev.energyStats.energy  = mi.value(QStringLiteral("energy")).toDouble(0.0);
        // voltage in mV → divide by 1000 for V
        dev.energyStats.voltage = mi.value(QStringLiteral("voltage")).toDouble(0.0) / 1000.0;
        dev.energyStats.valid   = true;
    }

    if (dev.hasThermostat()) {
        QJsonObject hi = ifaces.value(QStringLiteral("thermostatInterface")).toObject();
        // REST uses float celsius; convert to AHA integer steps (16=8°C .. 56=28°C)
        // so existing ThermostatWidget display code works unchanged.
        QString mode = hi.value(QStringLiteral("mode")).toString();
        if (mode == QStringLiteral("off")) {
            dev.thermostatStats.targetTemp = 253;
        } else if (mode == QStringLiteral("on")) {
            dev.thermostatStats.targetTemp = 254;
        } else {
            // "temperature" mode: celsius → AHA steps: steps = (celsius - 8.0) * 2 + 16
            QJsonObject setPoint = hi.value(QStringLiteral("setPointTemperature")).toObject();
            double celsius = setPoint.value(QStringLiteral("celsius")).toDouble(8.0);
            dev.thermostatStats.targetTemp = qRound((celsius - 8.0) * 2.0) + 16;
        }
        // currentTemp in 0.1°C steps: REST gives °C float → multiply by 10
        QJsonObject measured = hi.value(QStringLiteral("measuredTemperature")).toObject();
        double measCelsius = measured.value(QStringLiteral("celsius")).toDouble(0.0);
        dev.thermostatStats.currentTemp = qRound(measCelsius * 10.0);
    }

    if (dev.hasDimmer()) {
        QJsonObject li = ifaces.value(QStringLiteral("levelControlInterface")).toObject();
        // REST level is 0–100 (percentage); AHA was 0–255 for level, 0–100 for percent
        int levelPct = li.value(QStringLiteral("level")).toInt(0);
        dev.dimmerStats.levelPercent = levelPct;
        // Synthesize AHA-style 0–255 level from percentage
        dev.dimmerStats.level = qRound(levelPct * 255.0 / 100.0);
        // on/off from onOffInterface if present, else treat level>0 as on
        if (dev.hasSwitch())
            dev.dimmerStats.on = dev.switchStats.on;
        else
            dev.dimmerStats.on = levelPct > 0;
        dev.dimmerStats.valid = true;
    }

    if (dev.hasColorBulb()) {
        QJsonObject ci = ifaces.value(QStringLiteral("colorControlInterface")).toObject();
        // REST: hsColor.hue 0–360, hsColor.saturation 0–100; AHA: hue 0–359, sat 0–255
        QJsonObject hsColor = ci.value(QStringLiteral("hsColor")).toObject();
        dev.colorStats.hue = hsColor.value(QStringLiteral("hue")).toInt(0);
        // Convert REST saturation 0–100 → AHA saturation 0–255
        int satPct = hsColor.value(QStringLiteral("saturation")).toInt(0);
        dev.colorStats.saturation = qRound(satPct * 255.0 / 100.0);
        dev.colorStats.unmappedHue = dev.colorStats.hue;
        dev.colorStats.unmappedSaturation = dev.colorStats.saturation;
        dev.colorStats.colorTemperature = ci.value(QStringLiteral("colorTemperature")).toInt(0);
        // Determine colorMode: "1" = hue/sat, "4" = color temperature
        if (ci.contains(QStringLiteral("colorTemperature")) &&
            ci.value(QStringLiteral("colorTemperature")).toInt(0) > 0)
            dev.colorStats.colorMode = QStringLiteral("4");
        else
            dev.colorStats.colorMode = QStringLiteral("1");
        dev.colorStats.valid = true;
    }

    if (dev.hasBlind()) {
        QJsonObject bi = ifaces.value(QStringLiteral("blindInterface")).toObject();
        // REST blindAction: "moveUp"/"moveDown"/"stop"
        // Map to AHA-style "open"/"close"/"stop" for BlindWidget
        QString action = bi.value(QStringLiteral("blindAction")).toString();
        if (action == QStringLiteral("moveUp"))
            dev.blindStats.mode = QStringLiteral("open");
        else if (action == QStringLiteral("moveDown"))
            dev.blindStats.mode = QStringLiteral("close");
        else
            dev.blindStats.mode = QStringLiteral("stop");
        dev.blindStats.endPosition = bi.value(QStringLiteral("position")).toInt(0);
        dev.blindStats.valid = true;
    }

    if (dev.hasHumidity()) {
        QJsonObject humi = ifaces.value(QStringLiteral("humidityInterface")).toObject();
        dev.humidityStats.humidity = qRound(humi.value(QStringLiteral("humidity")).toDouble(0.0));
        dev.humidityStats.valid = true;
    }

    if (dev.hasAlarm()) {
        QJsonObject ai = ifaces.value(QStringLiteral("alertInterface")).toObject();
        dev.alarmStats.triggered = ai.value(QStringLiteral("state")).toString()
            == QStringLiteral("triggered");
        dev.alarmStats.valid = true;
    }
}

// ── REST JSON device list parser ──────────────────────────────────────────

FritzDeviceList FritzApi::parseDeviceListJson(const QByteArray &json) const
{
    FritzDeviceList devices;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull()) {
        qWarning() << "Failed to parse overview JSON:" << parseError.errorString();
        return devices;
    }

    QJsonObject root = doc.object();
    QJsonArray unitsArr  = root.value(QStringLiteral("units")).toArray();
    QJsonArray devicesArr = root.value(QStringLiteral("devices")).toArray();
    QJsonArray groupsArr = root.value(QStringLiteral("groups")).toArray();

    // Build a map from unitUID → device AIN, so we can resolve
    // group memberUnitUids and unit→device back-references.
    // Also build unitUID → unit JSON for quick lookup.
    QHash<QString, QString> unitToDeviceAin;   // unitUID → device AIN
    QHash<QString, QJsonObject> unitMap;        // unitUID → unit object

    for (const QJsonValue &uv : unitsArr) {
        QJsonObject u = uv.toObject();
        QString uid = u.value(QStringLiteral("UID")).toString();
        if (uid.isEmpty()) continue;
        unitMap.insert(uid, u);

        // Find the parent device AIN from devicesArr
        QString deviceUid = u.value(QStringLiteral("deviceUid")).toString();
        for (const QJsonValue &dv : devicesArr) {
            QJsonObject d = dv.toObject();
            if (d.value(QStringLiteral("UID")).toString() == deviceUid) {
                QString ain = d.value(QStringLiteral("ain")).toString().simplified().replace(QLatin1Char(' '), QString());
                unitToDeviceAin.insert(uid, ain);
                break;
            }
        }
    }

    // Parse physical devices
    for (const QJsonValue &dv : devicesArr) {
        QJsonObject d = dv.toObject();
        FritzDevice dev;
        dev.ain        = d.value(QStringLiteral("ain")).toString().simplified().replace(QLatin1Char(' '), QString());
        dev.identifier = d.value(QStringLiteral("ain")).toString();
        dev.fwversion  = d.value(QStringLiteral("firmwareVersion")).toString();
        dev.manufacturer = d.value(QStringLiteral("manufacturer")).toString();
        dev.productname  = d.value(QStringLiteral("productName")).toString();
        dev.name         = d.value(QStringLiteral("name")).toString();
        dev.present      = d.value(QStringLiteral("isConnected")).toBool();
        dev.group        = false;

        // REST API has no numeric device id; use ain as id fallback so
        // deviceById() in mainwindow still finds something via ain fallback
        dev.id = dev.ain;

        // Pick the primary unit: the first entry in unitUids[]
        QJsonArray unitUids = d.value(QStringLiteral("unitUids")).toArray();
        if (!unitUids.isEmpty()) {
            dev.unitUID = unitUids.at(0).toString();
        }

        // Synthesize functionBitmask from the unit's interfaces
        if (!dev.unitUID.isEmpty() && unitMap.contains(dev.unitUID)) {
            QJsonObject u = unitMap.value(dev.unitUID);
            QJsonObject ifaces = u.value(QStringLiteral("interfaces")).toObject();

            synthesizeFunctionBitmask(ifaces, dev);
            parseInterfaceState(ifaces, dev);

            // Battery state (device-specific; read from device JSON, not interfaces)
            bool battLow = d.value(QStringLiteral("isBatteryLow")).toBool();
            if (dev.hasThermostat()) {
                dev.thermostatStats.batteryLow = battLow;
                dev.thermostatStats.battery = d.value(QStringLiteral("batteryValue")).toInt(-1);
            }
        }

        if (!dev.ain.isEmpty())
            devices.append(dev);
    }

    // Parse groups
    for (const QJsonValue &gv : groupsArr) {
        QJsonObject g = gv.toObject();
        FritzDevice dev;
        dev.ain        = g.value(QStringLiteral("ain")).toString().simplified().replace(QLatin1Char(' '), QString());
        dev.identifier = g.value(QStringLiteral("ain")).toString();
        dev.name       = g.value(QStringLiteral("name")).toString();
        dev.group      = true;
        dev.present    = true;
        dev.id         = dev.ain;

        // Group's own control unit UID
        dev.unitUID = g.value(QStringLiteral("unitUid")).toString();

        // Groups inherit bitmask from their unitUid's interfaces
        if (!dev.unitUID.isEmpty() && unitMap.contains(dev.unitUID)) {
            QJsonObject u = unitMap.value(dev.unitUID);
            QJsonObject ifaces = u.value(QStringLiteral("interfaces")).toObject();
            synthesizeFunctionBitmask(ifaces, dev);
            parseInterfaceState(ifaces, dev);
        }

        // Map memberUnitUids → device AIns
        QJsonArray memberUnitUids = g.value(QStringLiteral("memberUnitUids")).toArray();
        for (const QJsonValue &mv : memberUnitUids) {
            QString memberUID = mv.toString();
            QString memberAin = unitToDeviceAin.value(memberUID);
            if (!memberAin.isEmpty() && !dev.memberAins.contains(memberAin))
                dev.memberAins.append(memberAin);
        }

        if (!dev.ain.isEmpty())
            devices.append(dev);
    }

    return devices;
}

// ---- Device stats (REST: GET /api/v0/smarthome/overview/units/{unitUID}) ----

void FritzApi::fetchDeviceStats(const QString &ain, DeviceStatsCallback onSuccess, ErrorCallback onError)
{
    if (!isLoggedIn() || ain.isEmpty()) {
        if (onError)
            onError(i18n("Not logged in or empty AIN"));
        return;
    }

    QString uid = unitUIDForAin(ain);
    if (uid.isEmpty()) {
        qWarning() << "fetchDeviceStats: no unitUID found for AIN" << ain;
        if (onError)
            onError(i18n("Unknown device AIN: %1", ain));
        return;
    }

    const QString cacheKey = QStringLiteral("device-stats-") + ain;

    // Check if we have valid cached data
    if (m_cache.contains(cacheKey)) {
        const CacheEntry &entry = m_cache.value(cacheKey);
        if (isCacheValid(entry)) {
            // Return cached data immediately (synchronously)
            DeviceBasicStats stats = parseUnitStatsJson(entry.data);
            onSuccess(stats);
            return;
        }
    }

    // Not cached or expired — check if request is already in-flight
    if (m_cache.contains(cacheKey) && m_cache[cacheKey].pendingReply) {
        // Request already in-flight, just queue the callback
        m_cache[cacheKey].deviceStatsCallbacks.append({onSuccess, onError});
        return;
    }

    // Start new network request
    QString path = QStringLiteral("/api/v0/smarthome/overview/units/") + QString(QUrl::toPercentEncoding(uid));
    QNetworkRequest req = buildRestRequest(path);
    auto *reply = get(req);

    // Initialize cache entry with pending request
    CacheEntry &entry = m_cache[cacheKey];
    entry.ttlSeconds = kDeviceStatsCacheTtl;
    entry.pendingReply = reply;
    entry.deviceStatsCallbacks.append({onSuccess, onError});

    connect(reply, &QNetworkReply::finished, this, [this, cacheKey, ain]() {
        onAsyncReply(cacheKey, true, ain);
    });
}

DeviceBasicStats FritzApi::parseUnitStatsJson(const QByteArray &json) const
{
    DeviceBasicStats result;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull()) {
        qWarning() << "Failed to parse unit stats JSON:" << parseError.errorString();
        return result;
    }

    QJsonObject root = doc.object();
    QJsonObject statsObj = root.value(QStringLiteral("statistics")).toObject();
    if (statsObj.isEmpty()) {
        // Unit has no statistics (e.g. group unit)
        return result;
    }

    result.fetchTime = QDateTime::currentDateTime();

    // Helper: parse a statistics array (e.g. statistics.energies[])
    // Each element has: interval (int), period (string), values (array)
    // Values are newest-first; NaN sentinel is represented as null in JSON.
    auto parseStatArray = [](const QJsonArray &arr, int targetInterval,
                              double scale, const QString &unitStr) -> StatSeries {
        StatSeries s;
        for (const QJsonValue &sv : arr) {
            QJsonObject entry = sv.toObject();
            int interval = entry.value(QStringLiteral("interval")).toInt(0);
            if (interval != targetInterval)
                continue;
            s.grid = interval;
            s.unit = unitStr;
            QJsonArray vals = entry.value(QStringLiteral("values")).toArray();
            s.values.reserve(vals.size());
            for (const QJsonValue &vv : vals) {
                if (vv.isNull() || vv.isUndefined())
                    s.values.append(std::numeric_limits<double>::quiet_NaN());
                else
                    s.values.append(vv.toDouble() * scale);
            }
            break;
        }
        return s;
    };

    // energies[]: values in Wh, scale=1.0
    //   interval=900   → 96 values (15-min, 24 h)  — the "Last 24 hours" series
    //   interval=21600 → 28 values (6-h, 1 week)   — no direct AHA equivalent, skip
    //   interval=86400 → 31 values (daily, 1 month)
    //   interval=2678400 → 24 values (monthly, ~2 years)
    QJsonArray energiesArr = statsObj.value(QStringLiteral("energies")).toArray();
    {
        StatSeries s900  = parseStatArray(energiesArr, 900,     1.0, QStringLiteral("Wh"));
        StatSeries s86400 = parseStatArray(energiesArr, 86400,  1.0, QStringLiteral("Wh"));
        StatSeries s2678400 = parseStatArray(energiesArr, 2678400, 1.0, QStringLiteral("Wh"));
        if (s900.grid > 0 && !s900.values.isEmpty())
            result.energy.append(s900);
        if (s86400.grid > 0 && !s86400.values.isEmpty())
            result.energy.append(s86400);
        if (s2678400.grid > 0 && !s2678400.values.isEmpty())
            result.energy.append(s2678400);
    }

    // When no usable energy series were found, capture the Fritz!Box-reported
    // statisticsState so the UI can show a meaningful reason.  The state is
    // per-entry in the JSON (e.g. "valid", "unknown", "notConnected"); we
    // pick the first non-"valid" state from the energies array.
    if (result.energy.isEmpty() && !energiesArr.isEmpty()) {
        for (const QJsonValue &ev : energiesArr) {
            QString state = ev.toObject()
                                .value(QStringLiteral("statisticsState"))
                                .toString();
            if (!state.isEmpty() && state != QLatin1String("valid")) {
                result.energyStatsState = state;
                break;
            }
        }
    }

    // powers[]: values in mW, scale=0.001 to convert to W
    QJsonArray powersArr = statsObj.value(QStringLiteral("powers")).toArray();
    {
        // mW → W: scale = 1/1000 = 0.001
        StatSeries s10 = parseStatArray(powersArr, 10, 0.001, QStringLiteral("W"));
        if (s10.grid > 0 && !s10.values.isEmpty())
            result.power.append(s10);
    }

    // temperatures[]: values in °C (float), interval=900
    QJsonArray tempsArr = statsObj.value(QStringLiteral("temperatures")).toArray();
    {
        StatSeries s900 = parseStatArray(tempsArr, 900, 1.0, QStringLiteral("°C"));
        if (s900.grid > 0 && !s900.values.isEmpty())
            result.temperature.append(s900);
    }

    // voltages[]: values in mV, interval=10; convert to V (scale=0.001)
    QJsonArray voltagesArr = statsObj.value(QStringLiteral("voltages")).toArray();
    {
        StatSeries s10 = parseStatArray(voltagesArr, 10, 0.001, QStringLiteral("V"));
        if (s10.grid > 0 && !s10.values.isEmpty())
            result.voltage.append(s10);
    }

    result.valid = true;
    return result;
}

// ---- REST control helpers ----

QString FritzApi::unitUIDForAin(const QString &ain) const
{
    for (const FritzDevice &dev : m_devices) {
        if (dev.ain == ain)
            return dev.unitUID;
    }
    return QString();
}

// Issue a PUT /api/v0/smarthome/overview/units/{unitUID} with the given interfaces JSON.
void FritzApi::putUnitInterfaces(const QString &ain, const QJsonObject &interfaces,
                                  const QString &cmdName)
{
    QString uid = unitUIDForAin(ain);
    if (uid.isEmpty()) {
        emit commandFailed(ain, i18n("Unknown device AIN: %1", ain));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("interfaces"), interfaces);
    QByteArray bodyData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QString path = QStringLiteral("/api/v0/smarthome/overview/units/") + QString(QUrl::toPercentEncoding(uid));
    QNetworkRequest req = buildRestRequest(path);
    auto *reply = put(req, bodyData);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ain, cmdName]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // HTTP 401 → session expired
            int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (httpStatus == 401) {
                handleSessionExpiry();
                return;
            }
            if (reply->error() != QNetworkReply::OperationCanceledError)
                emit commandFailed(ain, reply->errorString());
            return;
        }

        emit commandSuccess(ain, cmdName);
        // Invalidate cache and refresh device list after command
        clearAllCaches();
        QTimer::singleShot(500, this, [this]() {
            if (isLoggedIn())
                fetchDeviceList();
        });
    });
}

// ---- Switch commands ----

void FritzApi::setSwitchOn(const QString &ain)
{
    QJsonObject ifaces;
    QJsonObject onOff;
    onOff.insert(QStringLiteral("active"), true);
    ifaces.insert(QStringLiteral("onOffInterface"), onOff);
    putUnitInterfaces(ain, ifaces, QStringLiteral("setswitchon"));
}

void FritzApi::setSwitchOff(const QString &ain)
{
    QJsonObject ifaces;
    QJsonObject onOff;
    onOff.insert(QStringLiteral("active"), false);
    ifaces.insert(QStringLiteral("onOffInterface"), onOff);
    putUnitInterfaces(ain, ifaces, QStringLiteral("setswitchoff"));
}

void FritzApi::setSwitchToggle(const QString &ain)
{
    // Find current state and invert it
    bool currentOn = false;
    for (const FritzDevice &dev : m_devices) {
        if (dev.ain == ain) {
            currentOn = dev.switchStats.on;
            break;
        }
    }
    QJsonObject ifaces;
    QJsonObject onOff;
    onOff.insert(QStringLiteral("active"), !currentOn);
    ifaces.insert(QStringLiteral("onOffInterface"), onOff);
    putUnitInterfaces(ain, ifaces, QStringLiteral("setswitchtoggle"));
}

// ---- Thermostat ----

void FritzApi::setThermostatTarget(const QString &ain, int tempValue)
{
    // tempValue: 16=8°C .. 56=28°C, 253=off, 254=on (AHA encoding)
    QJsonObject ifaces;
    QJsonObject hkr;

    if (tempValue == 253) {
        hkr.insert(QStringLiteral("mode"), QStringLiteral("off"));
    } else if (tempValue == 254) {
        hkr.insert(QStringLiteral("mode"), QStringLiteral("on"));
    } else {
        // AHA steps → celsius: celsius = 8.0 + (tempValue - 16) * 0.5
        double celsius = 8.0 + (tempValue - 16) * 0.5;
        QJsonObject setPoint;
        setPoint.insert(QStringLiteral("celsius"), celsius);
        hkr.insert(QStringLiteral("mode"), QStringLiteral("temperature"));
        hkr.insert(QStringLiteral("setPointTemperature"), setPoint);
    }

    ifaces.insert(QStringLiteral("thermostatInterface"), hkr);
    putUnitInterfaces(ain, ifaces, QStringLiteral("sethkrtsoll"));
}

// ---- Dimmer ----

void FritzApi::setLevel(const QString &ain, int level)
{
    // AHA level is 0–255; REST levelControlInterface.level is 0–100 (percentage)
    int pct = qRound(level * 100.0 / 255.0);
    pct = qBound(0, pct, 100);

    QJsonObject ifaces;
    QJsonObject lc;
    lc.insert(QStringLiteral("level"), pct);
    ifaces.insert(QStringLiteral("levelControlInterface"), lc);
    putUnitInterfaces(ain, ifaces, QStringLiteral("setlevel"));
}

void FritzApi::setLevelPercentage(const QString &ain, int percent)
{
    int pct = qBound(0, percent, 100);

    QJsonObject ifaces;
    QJsonObject lc;
    lc.insert(QStringLiteral("level"), pct);
    ifaces.insert(QStringLiteral("levelControlInterface"), lc);
    putUnitInterfaces(ain, ifaces, QStringLiteral("setlevelpercentage"));
}

// ---- Color ----

void FritzApi::setColor(const QString &ain, int hue, int saturation, int duration)
{
    Q_UNUSED(duration)
    // AHA saturation 0–255 → REST saturation 0–100
    int satPct = qRound(saturation * 100.0 / 255.0);
    satPct = qBound(0, satPct, 100);

    QJsonObject ifaces;
    QJsonObject cc;
    QJsonObject hsColor;
    hsColor.insert(QStringLiteral("hue"), hue);
    hsColor.insert(QStringLiteral("saturation"), satPct);
    cc.insert(QStringLiteral("hsColor"), hsColor);
    ifaces.insert(QStringLiteral("colorControlInterface"), cc);
    putUnitInterfaces(ain, ifaces, QStringLiteral("setcolor"));
}

void FritzApi::setColorTemperature(const QString &ain, int kelvin, int duration)
{
    Q_UNUSED(duration)

    QJsonObject ifaces;
    QJsonObject cc;
    cc.insert(QStringLiteral("colorTemperature"), kelvin);
    ifaces.insert(QStringLiteral("colorControlInterface"), cc);
    putUnitInterfaces(ain, ifaces, QStringLiteral("setcolortemperature"));
}

// ---- Blind ----

void FritzApi::setBlind(const QString &ain, const QString &target)
{
    // AHA: "open"/"close"/"stop" → REST: "moveUp"/"moveDown"/"stop"
    QString restAction;
    if (target == QStringLiteral("open"))
        restAction = QStringLiteral("moveUp");
    else if (target == QStringLiteral("close"))
        restAction = QStringLiteral("moveDown");
    else
        restAction = QStringLiteral("stop");

    QJsonObject ifaces;
    QJsonObject bi;
    bi.insert(QStringLiteral("blindAction"), restAction);
    ifaces.insert(QStringLiteral("blindInterface"), bi);
    putUnitInterfaces(ain, ifaces, QStringLiteral("setblind"));
}

// ---- Abort / Polling ----

void FritzApi::abortPendingRequests()
{
    const auto replies = m_pendingReplies;
    for (QNetworkReply *reply : replies) {
        reply->abort();
    }
    m_pendingReplies.clear();
}

void FritzApi::handleSessionExpiry()
{
    m_sid = QStringLiteral("0000000000000000");

    emit sessionExpired();

    if (m_reloginAttempts >= kMaxReloginAttempts) {
        emit loginFailed(i18n(
            "Session expired and automatic re-login failed %1 time(s). "
            "Please check your credentials.", kMaxReloginAttempts));
        return;
    }

    ++m_reloginAttempts;
    login();
}

void FritzApi::startPolling(int intervalMs)
{
    m_pollTimer->setInterval(intervalMs);
    m_pollTimer->start();
}

void FritzApi::stopPolling()
{
    m_pollTimer->stop();
}

void FritzApi::onPollTimer()
{
    if (isLoggedIn()) {
        fetchDeviceList();
    }
}

// ---- Backward-compatible signal-based API (deprecated) ----

void FritzApi::fetchDeviceList()
{
    // Backward-compatible signal-based API: emit signals via callbacks
    fetchDeviceList(
        [this](const FritzDeviceList &devices) {
            emit deviceListUpdated(devices);
        },
        [this](const QString &error) {
            emit networkError(error);
        });
}

void FritzApi::fetchDeviceStats(const QString &ain)
{
    // Backward-compatible signal-based API: emit signals via callbacks
    fetchDeviceStats(ain,
        [this, ain](const DeviceBasicStats &stats) {
            if (stats.valid)
                emit deviceStatsUpdated(ain, stats);
            else
                emit deviceStatsError(ain, i18n("Fritz!Box returned no statistics for this device."));
        },
        [this, ain](const QString &error) {
            emit networkError(error);
            emit deviceStatsError(ain, error);
        });
}
