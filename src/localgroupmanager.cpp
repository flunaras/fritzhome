#include "localgroupmanager.h"

#include <QSettings>
#include <QUuid>

// QSettings layout:
//   localGroups/<uuid>/name    = <string>
//   localGroups/<uuid>/members = <QStringList>

static const char *kSettingsGroup      = "localGroups";
static const char *kSettingsKeyName    = "name";
static const char *kSettingsKeyMembers = "members";

// ── Constructor ───────────────────────────────────────────────────────────────

LocalGroupManager::LocalGroupManager(QObject *parent)
    : QObject(parent)
{
    reload();
}

// ── Public API ────────────────────────────────────────────────────────────────

void LocalGroupManager::reload()
{
    m_groups.clear();
    QSettings s;
    s.beginGroup(QString::fromLatin1(kSettingsGroup));
    const QStringList ids = s.childGroups();
    for (const QString &id : ids) {
        LocalGroup g;
        g.id         = id;
        g.name       = s.value(id + QLatin1Char('/') + QString::fromLatin1(kSettingsKeyName)).toString();
        g.memberAins = s.value(id + QLatin1Char('/') + QString::fromLatin1(kSettingsKeyMembers)).toStringList();
        if (!g.name.isEmpty())
            m_groups.append(g);
    }
    s.endGroup();
}

LocalGroup LocalGroupManager::addGroup(const QString &name, const QStringList &memberAins)
{
    LocalGroup g;
    g.id         = QUuid::createUuid().toString(QUuid::WithoutBraces);
    g.name       = name;
    g.memberAins = memberAins;
    m_groups.append(g);
    save();
    emit groupsChanged();
    return g;
}

void LocalGroupManager::updateGroup(const QString &id,
                                    const QString &name,
                                    const QStringList &memberAins)
{
    for (LocalGroup &g : m_groups) {
        if (g.id == id) {
            g.name       = name;
            g.memberAins = memberAins;
            save();
            emit groupsChanged();
            return;
        }
    }
}

void LocalGroupManager::removeGroup(const QString &id)
{
    for (auto it = m_groups.begin(); it != m_groups.end(); ++it) {
        if (it->id == id) {
            m_groups.erase(it);
            save();
            emit groupsChanged();
            return;
        }
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

void LocalGroupManager::save() const
{
    QSettings s;
    // Remove all existing entries first to handle deletions cleanly
    s.remove(QString::fromLatin1(kSettingsGroup));
    s.beginGroup(QString::fromLatin1(kSettingsGroup));
    for (const LocalGroup &g : m_groups) {
        s.setValue(g.id + QLatin1Char('/') + QString::fromLatin1(kSettingsKeyName),    g.name);
        s.setValue(g.id + QLatin1Char('/') + QString::fromLatin1(kSettingsKeyMembers), g.memberAins);
    }
    s.endGroup();
}
