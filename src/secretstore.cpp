#include "secretstore.h"

#include <QSettings>

#if HAVE_KF
#  include <kwallet.h>   // KF5::Wallet / KF6::Wallet — same header name
#elif HAVE_LIBSECRET
// GLib's gio/gdbusintrospection.h has a field named "signals" which clashes
// with Qt's "signals" keyword macro.  Temporarily undefine it around the
// libsecret include and restore it afterwards.
#  undef signals
#  include <libsecret/secret.h>
#  define signals Q_SIGNALS
#endif

// ── Internal helpers ─────────────────────────────────────────────────────────

static const char kFolder[]  = "fritzhome";
static const char kService[] = "fritzhome";  // wallet app ID

// Build the per-host key: "user@host" so multiple Fritz!Boxes are distinct
static QString walletKey(const QString &host, const QString &username)
{
    if (username.isEmpty())
        return host;
    return username + QLatin1Char('@') + host;
}

// Strip protocol prefix for use as a stable key regardless of whether the
// user typed "http://fritz.box" or just "fritz.box".
static QString normaliseHost(const QString &host)
{
    QString h = host;
    if (h.startsWith(QStringLiteral("https://")))
        h = h.mid(8);
    else if (h.startsWith(QStringLiteral("http://")))
        h = h.mid(7);
    while (h.endsWith(QLatin1Char('/')))
        h.chop(1);
    return h;
}

#if HAVE_LIBSECRET
// libsecret schema — one attribute "key" holds "<user>@<host>"
static const SecretSchema kSchema = {
    "de.fritzhome.password",
    SECRET_SCHEMA_NONE,
    {
        { "key", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING }
    }
};
#endif

// ── Public API ────────────────────────────────────────────────────────────────

bool SecretStore::savePassword(const QString &host,
                               const QString &username,
                               const QString &password)
{
    const QString normHost = normaliseHost(host);

#if HAVE_KF
    using KW = KWallet::Wallet;

    KW *wallet = KW::openWallet(KW::NetworkWallet(), 0 /*WId*/, KW::Synchronous);
    if (!wallet)
        return false;

    if (!wallet->hasFolder(QLatin1String(kFolder)))
        wallet->createFolder(QLatin1String(kFolder));

    if (!wallet->setFolder(QLatin1String(kFolder)))
        return false;

    const QString key = walletKey(normHost, username);
    return wallet->writePassword(key, password) == 0;

#elif HAVE_LIBSECRET
    const QString key = walletKey(normHost, username);
    GError *err = nullptr;
    gboolean ok = secret_password_store_sync(
        &kSchema,
        SECRET_COLLECTION_DEFAULT,
        "Fritz!Box password",          // label shown in keyring manager
        password.toUtf8().constData(),
        nullptr,                       // cancellable
        &err,
        "key", key.toUtf8().constData(),
        nullptr);
    if (err) {
        g_error_free(err);
        return false;
    }
    return ok == TRUE;

#else
    QSettings s;
    s.setValue(QStringLiteral("connection/password"), password);
    return true;
#endif
}

QString SecretStore::loadPassword(const QString &host,
                                  const QString &username)
{
    const QString normHost = normaliseHost(host);

#if HAVE_KF
    using KW = KWallet::Wallet;

    KW *wallet = KW::openWallet(KW::NetworkWallet(), 0 /*WId*/, KW::Synchronous);
    if (!wallet)
        return {};

    if (!wallet->hasFolder(QLatin1String(kFolder)))
        return {};

    if (!wallet->setFolder(QLatin1String(kFolder)))
        return {};

    const QString key = walletKey(normHost, username);
    QString password;
    if (wallet->readPassword(key, password) != 0)
        return {};

    return password;

#elif HAVE_LIBSECRET
    const QString key = walletKey(normHost, username);
    GError *err = nullptr;
    gchar *raw = secret_password_lookup_sync(
        &kSchema,
        nullptr,                       // cancellable
        &err,
        "key", key.toUtf8().constData(),
        nullptr);
    if (err) {
        g_error_free(err);
        return {};
    }
    if (!raw)
        return {};
    QString result = QString::fromUtf8(raw);
    secret_password_free(raw);
    return result;

#else
    QSettings s;
    return s.value(QStringLiteral("connection/password")).toString();
#endif
}

bool SecretStore::deletePassword(const QString &host)
{
    const QString normHost = normaliseHost(host);

#if HAVE_KF
    using KW = KWallet::Wallet;

    KW *wallet = KW::openWallet(KW::NetworkWallet(), 0 /*WId*/, KW::Synchronous);
    if (!wallet)
        return true;  // nothing to delete

    if (!wallet->hasFolder(QLatin1String(kFolder)))
        return true;

    if (!wallet->setFolder(QLatin1String(kFolder)))
        return false;

    // We don't know the username here, so iterate keys with the host suffix
    const QStringList keys = wallet->entryList();
    for (const QString &key : keys) {
        if (key == normHost || key.endsWith(QLatin1Char('@') + normHost))
            wallet->removeEntry(key);
    }
    return true;

#elif HAVE_LIBSECRET
    // Delete all entries whose "key" attribute ends with "@<host>" or equals "<host>"
    // We use secret_password_clear_sync with only the host part matched — libsecret
    // will remove all items matching the provided attributes.  Since we don't know
    // the username, we call it once without the key attribute to clear by label is
    // not reliable; instead we do a lookup-then-clear loop via the collection API.
    // For simplicity, attempt to clear both "host" and a wildcard isn't supported
    // by the simple API — use secret_service_search_sync to find all matching items.
    GError *err = nullptr;
    SecretService *service = secret_service_get_sync(
        SECRET_SERVICE_NONE, nullptr, &err);
    if (err || !service) {
        if (err) g_error_free(err);
        return true;
    }

    GHashTable *attrs = secret_attributes_build(
        &kSchema,
        nullptr);  // no attribute filter — we'll filter manually below

    GList *items = secret_service_search_sync(
        service,
        &kSchema,
        attrs,
        SECRET_SEARCH_ALL,
        nullptr,
        &err);
    g_hash_table_unref(attrs);

    if (err) {
        g_error_free(err);
        g_object_unref(service);
        return false;
    }

    const QByteArray hostSuffix = (QLatin1Char('@') + normHost).toUtf8();
    const QByteArray hostExact  = normHost.toUtf8();

    for (GList *l = items; l; l = l->next) {
        SecretItem *item = SECRET_ITEM(l->data);
        GHashTable *iattrs = secret_item_get_attributes(item);
        const gchar *k = static_cast<const gchar *>(
            g_hash_table_lookup(iattrs, "key"));
        if (k) {
            const QByteArray kb(k);
            if (kb == hostExact || kb.endsWith(hostSuffix)) {
                GError *delErr = nullptr;
                secret_item_delete_sync(item, nullptr, &delErr);
                if (delErr) g_error_free(delErr);
            }
        }
        g_hash_table_unref(iattrs);
    }

    g_list_free_full(items, g_object_unref);
    g_object_unref(service);
    return true;

#else
    Q_UNUSED(normHost)
    QSettings s;
    s.remove(QStringLiteral("connection/password"));
    return true;
#endif
}
