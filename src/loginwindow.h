#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QCheckBox;
class QDialogButtonBox;

/**
 * LoginWindow is shown when credentials (host/username/password) are
 * not supplied via command-line arguments.  It also doubles as the
 * connection-settings dialog accessible from the main menu.
 */
class LoginWindow : public QDialog
{
    Q_OBJECT
public:
    explicit LoginWindow(QWidget *parent = nullptr);

    // Pre-fill fields (e.g. from command-line partial input)
    void setHost(const QString &host);
    void setUsername(const QString &username);
    void setPassword(const QString &password);
    void setAutoLogin(bool enabled);
    void setIgnoreSsl(bool ignore);

    QString host() const;
    QString username() const;
    QString password() const;
    bool    autoLogin() const;
    bool    ignoreSsl() const;

private slots:
    void onAccepted();

private:
    QLineEdit        *m_hostEdit;
    QLineEdit        *m_usernameEdit;
    QLineEdit        *m_passwordEdit;
    QCheckBox        *m_ignoreSslCheck;
    QCheckBox        *m_autoLoginCheck;
    QDialogButtonBox *m_buttons;
};
