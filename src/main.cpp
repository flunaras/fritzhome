#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QSettings>
#include <QTranslator>
#include <QLocale>

#if HAVE_KF
#  include <KAboutData>
#  include <KLocalizedString>
#endif
#include "i18n_shim.h"

#include "mainwindow.h"
#include "secretstore.h"
// (No file-based debug logger in final build)

// Install a simple file-based message handler so qDebug/qWarning/etc. are
// captured even when the app is started from a launcher/systemd. The path can
// be overridden with the FRITZHOME_LOGFILE environment variable; default is
// /tmp/fritzhome.log.
// The message handler was used only during debugging and has been removed.

int main(int argc, char *argv[])
{
    // ── Qt / KDE application setup ────────────────────────────────────────────
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("fritzhome"));
    app.setOrganizationName(QStringLiteral("fritzhome"));
    app.setOrganizationDomain(QStringLiteral("flunaras.github.io"));
    app.setApplicationVersion(QStringLiteral("1.0.0"));
    // Tell Qt's XDG portal / tray integration where to find our .desktop file.
    // The file is named "io.github.flunaras.fritzhome.desktop" (reverse-DNS
    // convention based on flunaras.github.io, the author's GitHub Pages domain).
    // This covers the no-KF build path; for the KF build path see below.
    app.setDesktopFileName(QStringLiteral("io.github.flunaras.fritzhome"));

#if HAVE_KF
    // Tell KI18n which message catalogue to use (must be called before any
    // i18n() calls that rely on translations being loaded).
    KLocalizedString::setApplicationDomain("fritzhome");
#else
    // ── Qt-only translation (no KDE Frameworks) ───────────────────────────────
    // Install a QTranslator for the current locale.  The .qm file is embedded
    // in the binary as a Qt resource compiled from translations/fritzhome_de.ts.
    // Must be done before any i18n() calls so translated strings are used.
    {
        static QTranslator translator;
        const QString locale = QLocale::system().name(); // e.g. "de_DE"
        // Try exact locale first (e.g. "fritzhome_de_DE"), then language only
        // (e.g. "fritzhome_de"), then give up gracefully.
        if (translator.load(QStringLiteral(":/translations/fritzhome_%1.qm").arg(locale)) ||
            translator.load(QStringLiteral(":/translations/fritzhome_%1.qm").arg(locale.left(2))))
        {
            QApplication::installTranslator(&translator);
        }
    }
#endif

    // Set translated app display name after translator is installed.
    app.setApplicationDisplayName(i18n("Fritz!Box Smart Home"));

#if HAVE_KF
    KAboutData aboutData(
        QStringLiteral("fritzhome"),
        i18n("Fritz!Home"),
        QStringLiteral("1.0.0"),
        i18n("Fritz!Box Smart Home manager for KDE Plasma"),
        KAboutLicense::GPL_V3,
        i18n("© 2026 Ulf Saran"),
        QString(),
        QStringLiteral("https://github.com/flunaras/fritzhome")
    );
    aboutData.addAuthor(i18n("Ulf Saran"), QString(), QString());
    // KAboutData derives organizationDomain="github.com" and desktopFileName=
    // "com.github.flunaras.fritzhome" from the github.com homepage URL.
    // KAboutData::setApplicationData() propagates these to Qt, overwriting any
    // earlier app.setDesktopFileName() call.  Setting them on the aboutData object
    // *before* setApplicationData() is the correct fix.
    // We use io.github.flunaras — the reverse of flunaras.github.io, the author's
    // actual GitHub Pages domain — as the authoritative reverse-DNS prefix.
    aboutData.setOrganizationDomain(QByteArrayLiteral("flunaras.github.io"));
    aboutData.setDesktopFileName(QStringLiteral("io.github.flunaras.fritzhome"));
    KAboutData::setApplicationData(aboutData);
#endif

    // ── Command-line options ──────────────────────────────────────────────────
    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("Fritz!Box Smart Home Manager"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption hostOpt(
        {QStringLiteral("H"), QStringLiteral("host")},
        i18n("Fritz!Box hostname or IP address (default: fritz.box)"),
        QStringLiteral("host"),
        QStringLiteral("fritz.box"));

    QCommandLineOption userOpt(
        {QStringLiteral("u"), QStringLiteral("username")},
        i18n("Fritz!Box username (leave blank for the default admin account)"),
        QStringLiteral("username"),
        QString());

    QCommandLineOption passOpt(
        {QStringLiteral("p"), QStringLiteral("password")},
        i18n("Fritz!Box password"),
        QStringLiteral("password"),
        QString());

    QCommandLineOption intervalOpt(
        {QStringLiteral("i"), QStringLiteral("interval")},
        i18n("Polling interval in seconds (default: 10)"),
        QStringLiteral("seconds"),
        QStringLiteral("10"));

    parser.addOption(hostOpt);
    parser.addOption(userOpt);
    parser.addOption(passOpt);
    parser.addOption(intervalOpt);
    parser.process(app);

    const QString host     = parser.value(hostOpt).trimmed();
    const QString username = parser.value(userOpt).trimmed();
    const QString password = parser.value(passOpt);
    const int     interval = qBound(2, parser.value(intervalOpt).toInt(), 300);

    // ── Main window ───────────────────────────────────────────────────────────
    // KMainWindow (base of KXmlGuiWindow) always sets Qt::WA_DeleteOnClose in
    // its constructor.  When closeEvent() is accepted Qt posts a DeferredDelete
    // event that destroys the window from within the event loop, *before*
    // app.exec() returns.  We must therefore NEVER call "delete window" after
    // exec(): the object is already gone and doing so is a double-free that
    // overwrites the freed vtable slot and crashes with SIGSEGV.
    //
    // The correct ownership model for a KMainWindow is:
    //   - allocate on the heap (new)
    //   - do NOT store the pointer past the exec() call
    //   - let WA_DeleteOnClose / Qt::DeferredDelete handle destruction
    {
        MainWindow *window = new MainWindow();
        window->show();

        // If password was given on CLI, connect immediately; otherwise check
        // whether auto-login is enabled before falling back to the dialog.
        if (!password.isEmpty()) {
            window->configure(host, username, password, interval);
        } else {
            QSettings s;
            const bool autoLogin = s.value(QStringLiteral("connection/autoLogin"),
                                           false).toBool();
            if (autoLogin) {
                const QString savedHost = s.value(QStringLiteral("connection/host"),
                                                  QStringLiteral("fritz.box")).toString();
                const QString savedUser = s.value(QStringLiteral("connection/username"),
                                                  QString()).toString();
                const bool ignoreSsl   = s.value(QStringLiteral("connection/ignoreSsl"),
                                                  false).toBool();
                const QString savedPass = SecretStore::loadPassword(savedHost, savedUser);
                if (!savedPass.isEmpty()) {
                    // Credentials are available — connect silently.
                    window->configure(savedHost, savedUser, savedPass, interval, ignoreSsl);
                } else {
                    // Auto-login requested but no stored password — show dialog.
                    window->showLoginDialog();
                }
            } else {
                window->showLoginDialog();
            }
        }
    } // window pointer deliberately not used after this point

    return app.exec();
}
