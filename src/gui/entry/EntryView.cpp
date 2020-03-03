/*
 * Copyright (C) 2018 KeePassXC Team <team@keepassxc.org>
 * Copyright (C) 2010 Felix Geyer <debfx@fobos.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 or (at your option)
 * version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "EntryView.h"

#include <QAccessible>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QShortcut>

#include "gui/SortFilterHideProxyModel.h"

EntryView::EntryView(QWidget* parent)
    : QTreeView(parent)
    , m_model(new EntryModel(this))
    , m_sortModel(new SortFilterHideProxyModel(this))
    , m_inSearchMode(false)
{
    m_sortModel->setSourceModel(m_model);
    m_sortModel->setDynamicSortFilter(true);
    m_sortModel->setSortLocaleAware(true);
    m_sortModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    // Use Qt::UserRole as sort role, see EntryModel::data()
    m_sortModel->setSortRole(Qt::UserRole);
    QTreeView::setModel(m_sortModel);

    setUniformRowHeights(true);
    setRootIsDecorated(false);
    setAlternatingRowColors(true);
    setDragEnabled(true);
    setSortingEnabled(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);

    // QAbstractItemView::startDrag() uses this property as the default drag action
    setDefaultDropAction(Qt::MoveAction);

    // clang-format off
    connect(this, SIGNAL(doubleClicked(QModelIndex)), SLOT(emitEntryActivated(QModelIndex)));
    connect(selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(emitEntrySelectionChanged()));
    connect(m_model, SIGNAL(usernamesHiddenChanged()), SIGNAL(viewStateChanged()));
    connect(m_model, SIGNAL(passwordsHiddenChanged()), SIGNAL(viewStateChanged()));
    // clang-format on

    new QShortcut(Qt::CTRL + Qt::Key_F10, this, SLOT(contextMenuShortcutPressed()), nullptr, Qt::WidgetShortcut);

    m_headerMenu = new QMenu(this);
    m_headerMenu->setTitle(tr("Customize View"));
    m_headerMenu->addSection(tr("Customize View"));

    m_hideUsernamesAction = m_headerMenu->addAction(tr("Hide Usernames"), this, SLOT(setUsernamesHidden(bool)));
    m_hideUsernamesAction->setCheckable(true);
    m_hidePasswordsAction = m_headerMenu->addAction(tr("Hide Passwords"), this, SLOT(setPasswordsHidden(bool)));
    m_hidePasswordsAction->setCheckable(true);
    m_headerMenu->addSeparator();

    resetViewToDefaults();

    // Actions to toggle column visibility, each carrying the corresponding
    // colummn index as data
    m_columnActions = new QActionGroup(this);
    m_columnActions->setExclusive(false);
    for (int visualIndex = 1; visualIndex < header()->count(); ++visualIndex) {
        int logicalIndex = header()->logicalIndex(visualIndex);
        QString caption = m_model->headerData(logicalIndex, Qt::Horizontal, Qt::DisplayRole).toString();
        if (logicalIndex == EntryModel::Paperclip) {
            caption = tr("Has attachments", "Entry attachment icon toggle");
        } else if (logicalIndex == EntryModel::Totp) {
            caption = tr("Has TOTP", "Entry TOTP icon toggle");
        }

        QAction* action = m_headerMenu->addAction(caption);
        action->setCheckable(true);
        action->setData(logicalIndex);
        m_columnActions->addAction(action);
    }
    connect(m_columnActions, SIGNAL(triggered(QAction*)), this, SLOT(toggleColumnVisibility(QAction*)));
    connect(header(), &QHeaderView::sortIndicatorChanged, [this](int index, Qt::SortOrder order) {
        Q_UNUSED(order)
        header()->setSortIndicatorShown(index != EntryModel::Paperclip && index != EntryModel::Totp);
    });

    m_headerMenu->addSeparator();
    m_headerMenu->addAction(tr("Fit to window"), this, SLOT(fitColumnsToWindow()));
    m_headerMenu->addAction(tr("Fit to contents"), this, SLOT(fitColumnsToContents()));
    m_headerMenu->addSeparator();
    m_headerMenu->addAction(tr("Reset to defaults"), this, SLOT(resetViewToDefaults()));

    header()->setMinimumSectionSize(24);
    header()->setDefaultSectionSize(100);
    header()->setStretchLastSection(false);
    header()->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(header(), SIGNAL(customContextMenuRequested(QPoint)), SLOT(showHeaderMenu(QPoint)));
    // clang-format off
    connect(header(), SIGNAL(sectionCountChanged(int,int)), SIGNAL(viewStateChanged()));
    // clang-format on

    // clang-format off
    connect(header(), SIGNAL(sectionMoved(int,int,int)), SIGNAL(viewStateChanged()));
    // clang-format on

    // clang-format off
    connect(header(), SIGNAL(sectionResized(int,int,int)), SIGNAL(viewStateChanged()));
    // clang-format on

    // clang-format off
    connect(header(), SIGNAL(sortIndicatorChanged(int,Qt::SortOrder)), SIGNAL(viewStateChanged()));
    // clang-format on
}

void EntryView::contextMenuShortcutPressed()
{
    auto index = currentIndex();
    if (hasFocus() && index.isValid()) {
        emit customContextMenuRequested(visualRect(index).bottomLeft());
    }
}

void EntryView::keyPressEvent(QKeyEvent* event)
{
    if ((event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) && currentIndex().isValid()) {
        emitEntryActivated(currentIndex());
#ifdef Q_OS_MACOS
        // Pressing return does not emit the QTreeView::activated signal on mac os
        emit activated(currentIndex());
#endif
    }

    int last = m_model->rowCount() - 1;
    if (last > 0) {
        QAccessibleEvent accessibleEvent(this, QAccessible::PageChanged);
        if (event->key() == Qt::Key_Up && currentIndex().row() == 0) {
            QModelIndex index = m_sortModel->mapToSource(m_sortModel->index(last, 0));
            setCurrentEntry(m_model->entryFromIndex(index));
            QAccessible::updateAccessibility(&accessibleEvent);
            return;
        }

        if (event->key() == Qt::Key_Down && currentIndex().row() == last) {
            QModelIndex index = m_sortModel->mapToSource(m_sortModel->index(0, 0));
            setCurrentEntry(m_model->entryFromIndex(index));
            QAccessible::updateAccessibility(&accessibleEvent);
            return;
        }
    }

    QTreeView::keyPressEvent(event);
}

void EntryView::focusInEvent(QFocusEvent* event)
{
    emit entrySelectionChanged(currentEntry());
    QTreeView::focusInEvent(event);
}

void EntryView::focusOutEvent(QFocusEvent* event)
{
    emit entrySelectionChanged(nullptr);
    QTreeView::focusOutEvent(event);
}

void EntryView::displayGroup(Group* group)
{
    m_model->setGroup(group);
    header()->hideSection(EntryModel::ParentGroup);
    setFirstEntryActive();
    m_inSearchMode = false;
}

void EntryView::displaySearch(const QList<Entry*>& entries)
{
    m_model->setEntries(entries);
    header()->showSection(EntryModel::ParentGroup);

    // Reset sort column to 'Group', overrides DatabaseWidgetStateSync
    m_sortModel->sort(EntryModel::ParentGroup, Qt::AscendingOrder);
    sortByColumn(EntryModel::ParentGroup, Qt::AscendingOrder);

    setFirstEntryActive();
    m_inSearchMode = true;
}

void EntryView::setFirstEntryActive()
{
    if (m_model->rowCount() > 0) {
        QModelIndex index = m_sortModel->mapToSource(m_sortModel->index(0, 0));
        setCurrentEntry(m_model->entryFromIndex(index));
    } else {
        emit entrySelectionChanged(currentEntry());
    }
}

bool EntryView::inSearchMode()
{
    return m_inSearchMode;
}

void EntryView::emitEntryActivated(const QModelIndex& index)
{
    Entry* entry = entryFromIndex(index);
    emit entryActivated(entry, static_cast<EntryModel::ModelColumn>(m_sortModel->mapToSource(index).column()));
}

void EntryView::emitEntrySelectionChanged()
{
    emit entrySelectionChanged(currentEntry());
}

void EntryView::setModel(QAbstractItemModel* model)
{
    Q_UNUSED(model);
    Q_ASSERT(false);
}

Entry* EntryView::currentEntry()
{
    QModelIndexList list = selectionModel()->selectedRows();
    if (list.size() == 1) {
        return m_model->entryFromIndex(m_sortModel->mapToSource(list.first()));
    } else {
        return nullptr;
    }
}

int EntryView::numberOfSelectedEntries()
{
    return selectionModel()->selectedRows().size();
}

void EntryView::setCurrentEntry(Entry* entry)
{
    selectionModel()->setCurrentIndex(m_sortModel->mapFromSource(m_model->indexFromEntry(entry)),
                                      QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

Entry* EntryView::entryFromIndex(const QModelIndex& index)
{
    if (index.isValid()) {
        return m_model->entryFromIndex(m_sortModel->mapToSource(index));
    } else {
        return nullptr;
    }
}

/**
 * Get current state of 'Hide Usernames' setting (NOTE: just pass-through for
 * m_model)
 */
bool EntryView::isUsernamesHidden() const
{
    return m_model->isUsernamesHidden();
}

/**
 * Set state of 'Hide Usernames' setting (NOTE: just pass-through for m_model)
 */
void EntryView::setUsernamesHidden(bool hide)
{
    bool block = m_hideUsernamesAction->signalsBlocked();
    m_hideUsernamesAction->blockSignals(true);
    m_hideUsernamesAction->setChecked(hide);
    m_hideUsernamesAction->blockSignals(block);

    m_model->setUsernamesHidden(hide);
}

/**
 * Get current state of 'Hide Passwords' setting (NOTE: just pass-through for
 * m_model)
 */
bool EntryView::isPasswordsHidden() const
{
    return m_model->isPasswordsHidden();
}

/**
 * Set state of 'Hide Passwords' setting (NOTE: just pass-through for m_model)
 */
void EntryView::setPasswordsHidden(bool hide)
{
    bool block = m_hidePasswordsAction->signalsBlocked();
    m_hidePasswordsAction->blockSignals(true);
    m_hidePasswordsAction->setChecked(hide);
    m_hidePasswordsAction->blockSignals(block);

    m_model->setPasswordsHidden(hide);
}

/**
 * Get current view state
 */
QByteArray EntryView::viewState() const
{
    return header()->saveState();
}

/**
 * Set view state
 */
bool EntryView::setViewState(const QByteArray& state)
{
    bool status = header()->restoreState(state);
    resetFixedColumns();
    m_columnsNeedRelayout = state.isEmpty();
    return status;
}

/**
 * Sync checkable menu actions to current state and display header context
 * menu at specified position
 */
void EntryView::showHeaderMenu(const QPoint& position)
{
    m_hideUsernamesAction->setChecked(m_model->isUsernamesHidden());
    m_hidePasswordsAction->setChecked(m_model->isPasswordsHidden());
    const QList<QAction*> actions = m_columnActions->actions();
    for (auto& action : actions) {
        Q_ASSERT(static_cast<QMetaType::Type>(action->data().type()) == QMetaType::Int);
        if (static_cast<QMetaType::Type>(action->data().type()) != QMetaType::Int) {
            continue;
        }
        int columnIndex = action->data().toInt();
        bool hidden = header()->isSectionHidden(columnIndex) || (header()->sectionSize(columnIndex) == 0);
        action->setChecked(!hidden);
    }

    m_headerMenu->popup(mapToGlobal(position));
}

/**
 * Toggle visibility of column referenced by triggering action
 */
void EntryView::toggleColumnVisibility(QAction* action)
{
    // Verify action carries a column index as data. Since QVariant.toInt()
    // below will accept anything that's interpretable as int, perform a type
    // check here to make sure data actually IS int
    Q_ASSERT(static_cast<QMetaType::Type>(action->data().type()) == QMetaType::Int);
    if (static_cast<QMetaType::Type>(action->data().type()) != QMetaType::Int) {
        return;
    }

    // Toggle column visibility. Visible columns will only be hidden if at
    // least one visible column remains, as the table header will disappear
    // entirely when all columns are hidden
    int columnIndex = action->data().toInt();
    if (action->isChecked()) {
        header()->showSection(columnIndex);
        if (header()->sectionSize(columnIndex) == 0) {
            header()->resizeSection(columnIndex, header()->defaultSectionSize());
        }
        return;
    }
    if ((header()->count() - header()->hiddenSectionCount()) > 1) {
        header()->hideSection(columnIndex);
        return;
    }
    action->setChecked(true);
}

/**
 * Resize columns to fit all visible columns within the available space
 *
 * NOTE:
 * If EntryView::resizeEvent() is overridden at some point in the future,
 * its implementation MUST call the corresponding parent method using
 * 'QTreeView::resizeEvent(event)'. Without this, fitting to window will
 * be broken and/or work unreliably (stumbled upon during testing)
 *
 * NOTE:
 * Testing showed that it is absolutely necessary to emit signal 'viewState
 * Changed' here. Without this, an incomplete view state might get saved by
 * 'DatabaseWidgetStateSync' (e.g. only some columns resized)
 */
void EntryView::fitColumnsToWindow()
{
    header()->setSectionResizeMode(QHeaderView::Stretch);
    resetFixedColumns();
    QCoreApplication::processEvents();
    header()->setSectionResizeMode(QHeaderView::Interactive);
    resetFixedColumns();
    emit viewStateChanged();
}

/**
 * Resize columns to fit current table contents, i.e. make all contents
 * entirely visible
 */
void EntryView::fitColumnsToContents()
{
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    resetFixedColumns();
    QCoreApplication::processEvents();
    header()->setSectionResizeMode(QHeaderView::Interactive);
    resetFixedColumns();
    emit viewStateChanged();
}

/**
 * Mark icon-only columns as fixed and resize them to their minimum section size.
 */
void EntryView::resetFixedColumns()
{
    header()->setSectionResizeMode(EntryModel::Paperclip, QHeaderView::Fixed);
    header()->resizeSection(EntryModel::Paperclip, header()->minimumSectionSize());

    header()->setSectionResizeMode(EntryModel::Totp, QHeaderView::Fixed);
    header()->resizeSection(EntryModel::Totp, header()->minimumSectionSize());
}

/**
 * Reset item view to defaults.
 */
void EntryView::resetViewToDefaults()
{
    m_model->setUsernamesHidden(false);
    m_model->setPasswordsHidden(true);

    // Reduce number of columns that are shown by default
    if (m_inSearchMode) {
        header()->showSection(EntryModel::ParentGroup);
    } else {
        header()->hideSection(EntryModel::ParentGroup);
    }
    header()->showSection(EntryModel::Title);
    header()->showSection(EntryModel::Username);
    header()->showSection(EntryModel::Url);
    header()->showSection(EntryModel::Notes);
    header()->showSection(EntryModel::Modified);
    header()->showSection(EntryModel::Paperclip);
    header()->showSection(EntryModel::Totp);

    header()->hideSection(EntryModel::Password);
    header()->hideSection(EntryModel::Expires);
    header()->hideSection(EntryModel::Created);
    header()->hideSection(EntryModel::Accessed);
    header()->hideSection(EntryModel::Attachments);

    // Reset column order to logical indices
    for (int i = 0; i < header()->count(); ++i) {
        header()->moveSection(header()->visualIndex(i), i);
    }

    // Reorder some columns
    header()->moveSection(header()->visualIndex(EntryModel::Paperclip), 1);
    header()->moveSection(header()->visualIndex(EntryModel::Totp), 2);

    // Sort by title or group (depending on the mode)
    m_sortModel->sort(EntryModel::Title, Qt::AscendingOrder);
    sortByColumn(EntryModel::Title, Qt::AscendingOrder);

    if (m_inSearchMode) {
        m_sortModel->sort(EntryModel::ParentGroup, Qt::AscendingOrder);
        sortByColumn(EntryModel::ParentGroup, Qt::AscendingOrder);
    }

    // The following call only relayouts reliably if the widget has been shown
    // already, so only do it if the widget is visible and let showEvent() handle
    // the initial default layout.
    if (isVisible()) {
        fitColumnsToWindow();
    }
}

void EntryView::showEvent(QShowEvent* event)
{
    QTreeView::showEvent(event);

    // Check if header columns need to be resized to sensible defaults.
    // This is only needed if no previous view state has been loaded.
    if (m_columnsNeedRelayout) {
        fitColumnsToWindow();
        m_columnsNeedRelayout = false;
    }
}
