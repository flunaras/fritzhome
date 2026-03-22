#pragma once

#include <QString>

/**
 * SecretStore — thin cross-backend password storage abstraction.
 *
 * When built with KDE Frameworks (HAVE_KF=1):
 *   Uses KWallet to store the Fritz!Box password securely in the user's
 *   KDE wallet.  The wallet is opened lazily on first use.
 *   Wallet name  : KWallet::Wallet::NetworkWallet()
 *   Folder name  : "fritzhome"
 *   Key          : "<host>" (so multiple Fritz!Boxes are supported)
 *
 * When built without KDE Frameworks (HAVE_KF=0):
 *   Falls back to QSettings ("connection/password").  No encryption —
 *   same behaviour as before this class existed.
 *
 * All three methods are synchronous.  The KWallet path uses
 * Wallet::openWallet() with WId=0 (no parent window), which causes
 * KWallet to use a background, non-modal open request.  If the wallet
 * cannot be opened the methods return false / empty string.
 */
class SecretStore
{
public:
    /**
     * Save @p password for @p host / @p username.
     * Returns true on success.
     */
    static bool savePassword(const QString &host,
                             const QString &username,
                             const QString &password);

    /**
     * Load the password previously saved for @p host / @p username.
     * Returns an empty string if nothing is stored or on error.
     */
    static QString loadPassword(const QString &host,
                                const QString &username);

    /**
     * Remove any stored password for @p host.
     * Returns true on success or if no entry existed.
     */
    static bool deletePassword(const QString &host);
};
