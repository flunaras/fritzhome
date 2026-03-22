#include "loginwindow.h"
#include "i18n_shim.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QIcon>

LoginWindow::LoginWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Connect to Fritz!Box"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("network-connect")));
    setMinimumWidth(400);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // ── Header ───────────────────────────────────────────────────────────────
    QLabel *header = new QLabel(
        i18n("<b>Fritz!Box Smart Home</b><br>"
           "<small>Enter your Fritz!Box connection details.</small>"));
    header->setTextFormat(Qt::RichText);
    header->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(header);
    mainLayout->addSpacing(8);

    // ── Connection group ─────────────────────────────────────────────────────
    QGroupBox *connGroup = new QGroupBox(i18n("Connection"));
    QFormLayout *form = new QFormLayout(connGroup);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);

    m_hostEdit = new QLineEdit(QStringLiteral("fritz.box"));
    m_hostEdit->setPlaceholderText(i18n("e.g. 192.168.178.1 or fritz.box"));
    form->addRow(i18n("Host:"), m_hostEdit);

    m_ignoreSslCheck = new QCheckBox(i18n("Ignore TLS certificate warnings"));
    m_ignoreSslCheck->setChecked(false);
    m_ignoreSslCheck->setToolTip(
        i18n("When checked, TLS/SSL certificate errors are silently ignored.\n"
           "Use this only if your Fritz!Box uses a self-signed certificate\n"
           "and you trust the network you are connected to."));
    form->addRow(QString(), m_ignoreSslCheck);

    mainLayout->addWidget(connGroup);

    // ── Credentials group ─────────────────────────────────────────────────────
    QGroupBox *credGroup = new QGroupBox(i18n("Credentials"));
    QFormLayout *credForm = new QFormLayout(credGroup);

    m_usernameEdit = new QLineEdit();
    m_usernameEdit->setPlaceholderText(i18n("Fritz!Box username (leave blank for default)"));
    credForm->addRow(i18n("Username:"), m_usernameEdit);

    m_passwordEdit = new QLineEdit();
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(i18n("Fritz!Box password"));
    credForm->addRow(i18n("Password:"), m_passwordEdit);

    mainLayout->addWidget(credGroup);

    // ── Auto-login ────────────────────────────────────────────────────────────
    m_autoLoginCheck = new QCheckBox(i18n("Connect automatically on startup"));
    m_autoLoginCheck->setToolTip(
        i18n("When checked, the application connects immediately on next launch\n"
           "without showing this dialog."));
    mainLayout->addWidget(m_autoLoginCheck);
    mainLayout->addSpacing(4);

    // ── Buttons ───────────────────────────────────────────────────────────────
    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(i18n("Connect"));
    m_buttons->button(QDialogButtonBox::Ok)->setIcon(
        QIcon::fromTheme(QStringLiteral("network-connect")));

    connect(m_buttons, &QDialogButtonBox::accepted, this, &LoginWindow::onAccepted);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    mainLayout->addWidget(m_buttons);

    // Allow pressing Enter in any field to accept
    connect(m_hostEdit,     &QLineEdit::returnPressed, m_buttons->button(QDialogButtonBox::Ok), &QPushButton::click);
    connect(m_usernameEdit, &QLineEdit::returnPressed, m_buttons->button(QDialogButtonBox::Ok), &QPushButton::click);
    connect(m_passwordEdit, &QLineEdit::returnPressed, m_buttons->button(QDialogButtonBox::Ok), &QPushButton::click);
}

void LoginWindow::setHost(const QString &host)
{
    if (!host.isEmpty())
        m_hostEdit->setText(host);
}

void LoginWindow::setUsername(const QString &username)
{
    m_usernameEdit->setText(username);
}

void LoginWindow::setPassword(const QString &password)
{
    m_passwordEdit->setText(password);
}

void LoginWindow::setAutoLogin(bool enabled)
{
    m_autoLoginCheck->setChecked(enabled);
}

void LoginWindow::setIgnoreSsl(bool ignore)
{
    m_ignoreSslCheck->setChecked(ignore);
}

QString LoginWindow::host() const
{
    return m_hostEdit->text().trimmed();
}

QString LoginWindow::username() const
{
    return m_usernameEdit->text().trimmed();
}

QString LoginWindow::password() const
{
    return m_passwordEdit->text();
}

bool LoginWindow::autoLogin() const
{
    return m_autoLoginCheck->isChecked();
}

bool LoginWindow::ignoreSsl() const
{
    return m_ignoreSslCheck->isChecked();
}

void LoginWindow::onAccepted()
{
    // Basic validation: host and password are required
    if (host().isEmpty()) {
        m_hostEdit->setFocus();
        m_hostEdit->setStyleSheet(QStringLiteral("border: 1px solid red;"));
        return;
    }
    if (password().isEmpty()) {
        m_passwordEdit->setFocus();
        m_passwordEdit->setStyleSheet(QStringLiteral("border: 1px solid red;"));
        return;
    }
    // Reset any error styles
    m_hostEdit->setStyleSheet(QString());
    m_passwordEdit->setStyleSheet(QString());
    accept();
}
