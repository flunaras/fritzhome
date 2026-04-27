#pragma once

#include <QDialog>
#include <QPointer>
#include "fritzdevice.h"

class LocalGroupManager;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLineEdit;
class QLabel;
class QTimer;
class QWidget;

/**
 * LocalGroupDialog lets the user create, rename, reorder members of, and
 * delete locally-defined device groups.
 *
 * UX design: changes are applied immediately — there is no separate Save
 * button.  Renaming the group name field commits on every edit (debounced
 * 400 ms).  Toggling a member checkbox commits instantly.  The + toolbar
 * button creates a new group right away and puts focus on the name field
 * so the user can type the desired name immediately.  The − button deletes
 * the selected group after a confirmation prompt.
 *
 * Changes propagate to LocalGroupManager (which emits groupsChanged()) so
 * the device tree updates in real time while the dialog is open.
 */
class LocalGroupDialog : public QDialog
{
    Q_OBJECT

public:
    /**
     * @param manager    Owns the local groups; dialog operates on it directly.
     * @param allDevices All Fritz!Box devices currently known — used to
     *                   populate the member candidate list.
     * @param parent     Parent widget.
     */
    explicit LocalGroupDialog(LocalGroupManager *manager,
                              const FritzDeviceList &allDevices,
                              QWidget *parent = nullptr);

private slots:
    // ── Slots ─────────────────────────────────────────────────────────────
    void onGroupSelectionChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onAddGroup();
    void onDeleteGroup();
    void onNameEdited(const QString &text);
    void onNameCommit();
    void onMemberToggled(QListWidgetItem *item);

private:
    // ── Utility methods ───────────────────────────────────────────────────
    void populateGroupList();
    void populateMemberList(const QStringList &checkedAins);
    void selectGroupById(const QString &id);
    void loadGroupIntoEditor(const QString &id);
    void clearEditor();
    void commitName();
    QStringList currentMemberAins() const;
    /// Returns the name for the group currently being edited, falling back to
    /// the name field text (which may have an unsaved debounced edit).
    QString currentEditingName() const;
    /// Returns the group name to use for the group currently in m_editingId,
    /// looking it up in the manager.
    QString groupNameById(const QString &id) const;

    // ── Member variables ──────────────────────────────────────────────────
    QPointer<LocalGroupManager> m_manager;
    FritzDeviceList             m_allDevices;
    QString                     m_editingId;  ///< id of the group currently loaded in the editor

    // Left panel
    QListWidget *m_groupList;
    QPushButton *m_addBtn;
    QPushButton *m_removeBtn;

    // Right panel
    QLabel      *m_editorHint;
    QWidget     *m_editorWidget;  ///< container shown when a group is selected
    QLineEdit   *m_nameEdit;
    QListWidget *m_memberList;

    // Debounce timer for name edits
    QTimer      *m_nameTimer;
};
