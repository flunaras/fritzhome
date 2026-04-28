#include "localgroupdialog.h"
#include "localgroupmanager.h"
#include "i18n_shim.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QFont>
#include <QIcon>

// Default name assigned to newly created groups before the user renames them.
static const char *kDefaultGroupName = QT_TRANSLATE_NOOP("LocalGroupDialog", "New Group");

/// Return a short, singular type label for a Fritz!Box device, used as the
/// parenthetical suffix in the member list (e.g. "Livingroom  (Smart Plug)").
static QString deviceTypeLabel(const FritzDevice &dev)
{
    switch (dev.primaryType()) {
    case FritzDevice::PrimaryType::Group:          return i18n("Fritz!Box group");
    case FritzDevice::PrimaryType::ColorBulb:      return i18n("Color Bulb");
    case FritzDevice::PrimaryType::Dimmer:         return i18n("Dimmer");
    case FritzDevice::PrimaryType::SmartPlug:      return i18n("Smart Plug");
    case FritzDevice::PrimaryType::Switch:         return i18n("Switch");
    case FritzDevice::PrimaryType::Thermostat:     return i18n("Thermostat");
    case FritzDevice::PrimaryType::Blind:          return i18n("Blind");
    case FritzDevice::PrimaryType::Alarm:          return i18n("Alarm");
    case FritzDevice::PrimaryType::HumiditySensor: return i18n("Humidity Sensor");
    case FritzDevice::PrimaryType::Sensor:         return i18n("Sensor");
    }
    return i18n("Sensor");
}

// ── Constructor ───────────────────────────────────────────────────────────────

LocalGroupDialog::LocalGroupDialog(LocalGroupManager *manager,
                                   const FritzDeviceList &allDevices,
                                   QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_allDevices(allDevices)
    , m_groupList(new QListWidget(this))
    , m_addBtn(new QPushButton(i18n("+"), this))
    , m_removeBtn(new QPushButton(i18n("−"), this))
    , m_editorHint(new QLabel(this))
    , m_editorWidget(new QWidget(this))
    , m_nameEdit(new QLineEdit(this))
    , m_memberList(new QListWidget(this))
    , m_nameTimer(new QTimer(this))
{
    setWindowTitle(i18n("Manage Local Groups"));
    resize(640, 460);

    m_nameTimer->setSingleShot(true);
    m_nameTimer->setInterval(400);   // commit name 400 ms after user stops typing

    // ── Root layout ───────────────────────────────────────────────────────────
    QVBoxLayout *dialogLayout = new QVBoxLayout(this);

    QHBoxLayout *contentRow = new QHBoxLayout();
    contentRow->setSpacing(12);
    dialogLayout->addLayout(contentRow, 1);

    // ── Left panel: group list + add / remove toolbar ─────────────────────────
    QWidget     *leftWidget = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    QLabel *groupsLabel = new QLabel(i18n("Groups"), leftWidget);
    QFont boldFont = groupsLabel->font();
    boldFont.setBold(true);
    groupsLabel->setFont(boldFont);
    leftLayout->addWidget(groupsLabel);

    m_groupList->setParent(leftWidget);
    m_groupList->setMinimumWidth(170);
    m_groupList->setMaximumWidth(220);
    m_groupList->setAlternatingRowColors(true);
    leftLayout->addWidget(m_groupList, 1);

    // Toolbar row below the list
    QHBoxLayout *toolbar = new QHBoxLayout();
    toolbar->setSpacing(4);

    m_addBtn->setParent(leftWidget);
    m_addBtn->setToolTip(i18n("Create a new local group"));
    m_addBtn->setFixedWidth(32);

    m_removeBtn->setParent(leftWidget);
    m_removeBtn->setToolTip(i18n("Delete the selected group"));
    m_removeBtn->setFixedWidth(32);
    m_removeBtn->setEnabled(false);

    toolbar->addWidget(m_addBtn);
    toolbar->addWidget(m_removeBtn);
    toolbar->addStretch();
    leftLayout->addLayout(toolbar);

    contentRow->addWidget(leftWidget, 0);

    // ── Vertical separator ────────────────────────────────────────────────────
    QFrame *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    contentRow->addWidget(sep);

    // ── Right panel: editor ───────────────────────────────────────────────────
    QWidget     *rightWidget = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    // Hint shown when nothing is selected
    m_editorHint->setParent(rightWidget);
    m_editorHint->setText(
        i18n("Select a group on the left to edit it,\nor click + to create a new one."));
    m_editorHint->setAlignment(Qt::AlignCenter);
    m_editorHint->setWordWrap(true);
    rightLayout->addWidget(m_editorHint, 1);

    // Editor widget shown when a group is selected
    m_editorWidget->setParent(rightWidget);
    QVBoxLayout *editorLayout = new QVBoxLayout(m_editorWidget);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(8);

    QHBoxLayout *nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(i18n("Name:"), m_editorWidget));
    m_nameEdit->setParent(m_editorWidget);
    m_nameEdit->setPlaceholderText(i18n("Group name"));
    nameRow->addWidget(m_nameEdit, 1);
    editorLayout->addLayout(nameRow);

    editorLayout->addWidget(new QLabel(i18n("Members:"), m_editorWidget));

    m_memberList->setParent(m_editorWidget);
    m_memberList->setSelectionMode(QAbstractItemView::NoSelection);
    editorLayout->addWidget(m_memberList, 1);

    m_editorWidget->hide();
    rightLayout->addWidget(m_editorWidget, 1);

    contentRow->addWidget(rightWidget, 1);

    // ── Close button ──────────────────────────────────────────────────────────
    QDialogButtonBox *bbox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    dialogLayout->addWidget(bbox);

    // ── Signal connections ────────────────────────────────────────────────────
    connect(m_groupList,  &QListWidget::currentItemChanged,
            this, &LocalGroupDialog::onGroupSelectionChanged);
    connect(m_addBtn,     &QPushButton::clicked,   this, &LocalGroupDialog::onAddGroup);
    connect(m_removeBtn,  &QPushButton::clicked,   this, &LocalGroupDialog::onDeleteGroup);
    connect(m_nameEdit,   &QLineEdit::textChanged, this, &LocalGroupDialog::onNameEdited);
    connect(m_nameEdit,   &QLineEdit::editingFinished, this, &LocalGroupDialog::onNameCommit);
    connect(m_nameTimer,  &QTimer::timeout,        this, &LocalGroupDialog::commitName);
    connect(m_memberList, &QListWidget::itemChanged, this, &LocalGroupDialog::onMemberToggled);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::accept);

    // Refresh the group list when the manager changes while the dialog is open
    connect(m_manager, &LocalGroupManager::groupsChanged,
            this, &LocalGroupDialog::populateGroupList);

    populateGroupList();
}

// ── Left panel helpers ────────────────────────────────────────────────────────

void LocalGroupDialog::populateGroupList()
{
    const LocalGroupList &groups = m_manager->groups();

    // Build a quick lookup: id → name for the current manager state
    QHash<QString, QString> nameById;
    for (const LocalGroup &g : groups)
        nameById.insert(g.id, g.name);

    // Collect ids currently shown in the list
    QStringList shownIds;
    for (int i = 0; i < m_groupList->count(); ++i)
        shownIds.append(m_groupList->item(i)->data(Qt::UserRole).toString());

    // Decide whether the set of groups has changed (add/remove)
    QStringList managerIds;
    for (const LocalGroup &g : groups)
        managerIds.append(g.id);

    const bool structureChanged = (shownIds != managerIds);

    if (structureChanged) {
        // Full rebuild needed — block signals to avoid spurious editor reloads
        const QString currentId = m_editingId;
        m_groupList->blockSignals(true);
        m_groupList->clear();
        for (const LocalGroup &g : groups) {
            QListWidgetItem *item = new QListWidgetItem(g.name, m_groupList);
            item->setData(Qt::UserRole, g.id);
        }
        m_groupList->blockSignals(false);
        if (!currentId.isEmpty())
            selectGroupById(currentId);
    } else {
        // Only names may have changed — update labels in-place, no selection change
        for (int i = 0; i < m_groupList->count(); ++i) {
            QListWidgetItem *item = m_groupList->item(i);
            const QString id = item->data(Qt::UserRole).toString();
            const QString newName = nameById.value(id);
            if (item->text() != newName)
                item->setText(newName);
        }
    }
}

void LocalGroupDialog::selectGroupById(const QString &id)
{
    for (int i = 0; i < m_groupList->count(); ++i) {
        if (m_groupList->item(i)->data(Qt::UserRole).toString() == id) {
            m_groupList->setCurrentRow(i);
            return;
        }
    }
}

// ── Right panel helpers ───────────────────────────────────────────────────────

void LocalGroupDialog::populateMemberList(const QStringList &checkedAins)
{
    m_memberList->blockSignals(true);
    m_memberList->clear();

    // Fritz!Box individual devices
    for (const FritzDevice &dev : m_allDevices) {
        if (dev.isGroup())
            continue;
        QListWidgetItem *item = new QListWidgetItem(
            QString("%1  (%2)").arg(dev.name, deviceTypeLabel(dev)), m_memberList);
        item->setData(Qt::UserRole, dev.ain);
        item->setToolTip(dev.ain);
        item->setIcon(QIcon(dev.iconPath()));
        item->setCheckState(checkedAins.contains(dev.ain) ? Qt::Checked : Qt::Unchecked);
    }

    // Fritz!Box native groups
    for (const FritzDevice &dev : m_allDevices) {
        if (!dev.isGroup())
            continue;
        QListWidgetItem *item = new QListWidgetItem(
            QString("%1  (%2)").arg(dev.name, deviceTypeLabel(dev)), m_memberList);
        item->setData(Qt::UserRole, dev.ain);
        item->setToolTip(dev.ain);
        item->setIcon(QIcon(dev.iconPath()));
        item->setCheckState(checkedAins.contains(dev.ain) ? Qt::Checked : Qt::Unchecked);
    }

    // Other local groups (a group cannot be its own direct or transitive member —
    // the cycle check is done at chart-render time; here we only exclude
    // self-membership to keep the UI simple).
    for (const LocalGroup &g : m_manager->groups()) {
        if (g.id == m_editingId)
            continue;
        const QString ain = QStringLiteral("local:") + g.id;
        QListWidgetItem *item = new QListWidgetItem(
            QString("%1  (%2)").arg(g.name, i18n("local group")), m_memberList);
        item->setData(Qt::UserRole, ain);
        item->setToolTip(ain);
        item->setIcon(QIcon(QStringLiteral(":/icons/device-local-group.svg")));
        item->setCheckState(checkedAins.contains(ain) ? Qt::Checked : Qt::Unchecked);
    }

    m_memberList->blockSignals(false);
}

void LocalGroupDialog::loadGroupIntoEditor(const QString &id)
{
    m_editingId = id;
    m_editorHint->hide();
    m_editorWidget->show();

    for (const LocalGroup &g : m_manager->groups()) {
        if (g.id == id) {
            m_nameEdit->blockSignals(true);
            m_nameEdit->setText(g.name);
            m_nameEdit->blockSignals(false);
            populateMemberList(g.memberAins);
            return;
        }
    }
}

void LocalGroupDialog::clearEditor()
{
    m_editingId.clear();
    m_nameTimer->stop();
    m_editorWidget->hide();
    m_editorHint->show();
    m_removeBtn->setEnabled(false);
    m_groupList->clearSelection();
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void LocalGroupDialog::onGroupSelectionChanged(QListWidgetItem *current,
                                               QListWidgetItem * /*previous*/)
{
    if (!current) {
        clearEditor();
        return;
    }
    const QString id = current->data(Qt::UserRole).toString();
    m_removeBtn->setEnabled(true);
    loadGroupIntoEditor(id);
}

void LocalGroupDialog::onAddGroup()
{
    // Create a new group immediately with a placeholder name
    const LocalGroup g = m_manager->addGroup(i18n(kDefaultGroupName), {});
    // populateGroupList is triggered by groupsChanged; select the new entry
    selectGroupById(g.id);
    // Pre-select the name text so the user can start typing right away
    m_nameEdit->selectAll();
    m_nameEdit->setFocus();
}

void LocalGroupDialog::onDeleteGroup()
{
    if (m_editingId.isEmpty())
        return;

    const int ret = QMessageBox::question(
        this,
        i18n("Delete Group"),
        i18n("Delete the group \"%1\"? This cannot be undone.", groupNameById(m_editingId)),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    m_manager->removeGroup(m_editingId);
    clearEditor();
}

void LocalGroupDialog::onNameEdited(const QString & /*text*/)
{
    // Restart the debounce timer on every keystroke
    m_nameTimer->start();
}

void LocalGroupDialog::onNameCommit()
{
    // Called when the user presses Enter or the field loses focus
    m_nameTimer->stop();
    commitName();
}

void LocalGroupDialog::commitName()
{
    if (m_editingId.isEmpty())
        return;

    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        return;

    // Update the manager — emits groupsChanged so MainWindow refreshes the
    // device tree.  populateGroupList() handles this safely in-place.
    m_manager->updateGroup(m_editingId, name, currentMemberAins());

    // Update the list item label in-place without a full rebuild
    for (int i = 0; i < m_groupList->count(); ++i) {
        if (m_groupList->item(i)->data(Qt::UserRole).toString() == m_editingId) {
            m_groupList->item(i)->setText(name);
            break;
        }
    }
}

void LocalGroupDialog::onMemberToggled(QListWidgetItem * /*item*/)
{
    if (m_editingId.isEmpty())
        return;

    // Collect current name (may have unsaved debounced text)
    m_manager->updateGroup(m_editingId, currentEditingName(), currentMemberAins());
}

// ── Utility methods ───────────────────────────────────────────────────────────

QStringList LocalGroupDialog::currentMemberAins() const
{
    QStringList result;
    for (int i = 0; i < m_memberList->count(); ++i) {
        const QListWidgetItem *item = m_memberList->item(i);
        if (item->checkState() == Qt::Checked)
            result.append(item->data(Qt::UserRole).toString());
    }
    return result;
}

QString LocalGroupDialog::currentEditingName() const
{
    const QString typed = m_nameEdit->text().trimmed();
    return typed.isEmpty() ? i18n(kDefaultGroupName) : typed;
}

QString LocalGroupDialog::groupNameById(const QString &id) const
{
    for (const LocalGroup &g : m_manager->groups()) {
        if (g.id == id)
            return g.name;
    }
    return QString();
}
