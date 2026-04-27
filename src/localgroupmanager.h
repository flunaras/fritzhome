#pragma once

#include <QObject>
#include "fritzdevice.h"

/**
 * LocalGroupManager manages locally-defined groups that exist only on the
 * client side (not on the Fritz!Box).  Groups are stored in QSettings under
 * the "localGroups" key and survive application restarts.
 *
 * Each local group has:
 *   - a stable UUID (id)
 *   - a user-visible name
 *   - a list of member AIns (Fritz!Box device AIns, or "local:<id>" for
 *     other local groups)
 *
 * This class is a lightweight, non-singleton helper; MainWindow owns the
 * single instance and passes it to the dialog and device model as needed.
 */
class LocalGroupManager : public QObject
{
    Q_OBJECT
public:
    explicit LocalGroupManager(QObject *parent = nullptr);

    /// Return all locally defined groups (read from QSettings on construction).
    const LocalGroupList &groups() const { return m_groups; }

    /// Add a new group and persist it.  Returns the newly created group.
    LocalGroup addGroup(const QString &name, const QStringList &memberAins);

    /// Update an existing group's name and/or members by id.  No-op if not found.
    void updateGroup(const QString &id, const QString &name, const QStringList &memberAins);

    /// Remove a group by id.  No-op if not found.
    void removeGroup(const QString &id);

    /// Reload groups from QSettings.
    void reload();

signals:
    /// Emitted after any change (add/update/remove) so listeners can rebuild.
    void groupsChanged();

private:
    void save() const;

    LocalGroupList m_groups;
};
