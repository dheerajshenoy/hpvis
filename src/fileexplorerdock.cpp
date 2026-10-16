#include "fileexplorerdock.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QSettings>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

FileExplorerDock::FileExplorerDock(QWidget *parent)
    : QDockWidget("Files", parent)
{
    setObjectName("fileExplorerDock"); // for saveState()

    m_model = new QFileSystemModel(this);
    m_model->setReadOnly(true);
    // Folders are always listed; the name filters only apply to files
    m_model->setNameFilterDisables(false);

    m_tree = new QTreeView();
    m_tree->setModel(m_model);
    m_tree->setHeaderHidden(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setDragEnabled(true); // the files carry their URLs
    m_tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree->setColumnHidden(2, true); // type
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    m_upButton = new QToolButton();
    m_upButton->setArrowType(Qt::UpArrow);
    m_upButton->setToolTip("Parent folder");
    m_upButton->setAutoRaise(true);

    m_pathEdit = new QLineEdit();
    m_pathEdit->setPlaceholderText("Folder");
    m_pathEdit->setClearButtonEnabled(true);

    m_showAll = new QCheckBox("Show all files");
    m_showAll->setToolTip("Also list files that are not maps");

    auto *bar = new QHBoxLayout();
    bar->setContentsMargins(0, 0, 0, 0);
    bar->addWidget(m_upButton);
    bar->addWidget(m_pathEdit, 1);

    auto *body   = new QWidget();
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(bar);
    layout->addWidget(m_tree, 1);
    layout->addWidget(m_showAll);
    setWidget(body);

    connect(m_upButton, &QToolButton::clicked, this, [this]()
    {
        QDir dir(rootPath());
        if (dir.cdUp())
            setRootPath(dir.absolutePath());
    });
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this]()
    {
        QString p = m_pathEdit->text().trimmed();
        if (p.startsWith('~'))
            p = QDir::homePath() + p.mid(1);
        if (QFileInfo(p).isDir())
            setRootPath(p);
        else
            m_pathEdit->setText(rootPath()); // put back what is shown
    });
    connect(m_showAll, &QCheckBox::toggled, this, [this](bool on)
    {
        QSettings().setValue("fileExplorer/showAll", on);
        applyFilter();
    });
    // Enter or double click; a folder expands or collapses by itself
    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex &i)
    {
        if (i.isValid() && !m_model->isDir(i))
            emit openRequested(m_model->filePath(i));
    });
    connect(m_tree, &QTreeView::customContextMenuRequested, this,
            &FileExplorerDock::showContextMenu);

    const QSettings settings;
    {
        QSignalBlocker block(m_showAll);
        m_showAll->setChecked(
            settings.value("fileExplorer/showAll", false).toBool());
    }
    applyFilter();

    QString root = settings.value("fileExplorer/root").toString();
    if (root.isEmpty() || !QDir(root).exists())
        root = QDir::homePath();
    setRootPath(root);
}

QString
FileExplorerDock::rootPath() const
{
    return m_model->rootPath();
}

void
FileExplorerDock::setRootPath(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (!QFileInfo(abs).isDir())
        return;
    m_tree->setRootIndex(m_model->setRootPath(abs));
    m_pathEdit->setText(QDir::toNativeSeparators(abs));
    m_upButton->setEnabled(!QDir(abs).isRoot());
    QSettings().setValue("fileExplorer/root", abs);
    emit rootChanged(abs);
}

void
FileExplorerDock::applyFilter()
{
    QDir::Filters filters = QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot;
    m_model->setFilter(filters);
    if (m_showAll->isChecked())
        m_model->setNameFilters({});
    else
        m_model->setNameFilters(
            {"*.fits", "*.fit", "*.fts", "*.npy", "*.npz"});
}

void
FileExplorerDock::revealFile(const QString &path)
{
    if (path.isEmpty())
        return;
    const QString abs  = QFileInfo(path).absoluteFilePath();
    const QString root = QDir::cleanPath(rootPath());
    if (!abs.startsWith(root + '/'))
        return;

    const QModelIndex index = m_model->index(abs);
    if (!index.isValid())
        return;
    for (QModelIndex p = index.parent(); p.isValid() && p != m_tree->rootIndex();
         p = p.parent())
        m_tree->expand(p);
    m_tree->setCurrentIndex(index);
    m_tree->scrollTo(index);
}

QString
FileExplorerDock::pathAt(const QModelIndex &index) const
{
    return index.isValid() ? m_model->filePath(index) : QString();
}

void
FileExplorerDock::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_tree->indexAt(pos);
    const QString path      = pathAt(index);
    if (path.isEmpty())
        return;
    const bool isDir = m_model->isDir(index);

    QMenu menu(this);
    if (isDir)
        menu.addAction("Set As Root", this, [this, path]() { setRootPath(path); });
    else
        menu.addAction("Open", this, [this, path]() { emit openRequested(path); });
    menu.addAction("Open Containing Folder", this, [path, isDir]()
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            isDir ? path : QFileInfo(path).absolutePath()));
    });
    menu.addAction("Copy Path", this,
                   [path]() { QApplication::clipboard()->setText(path); });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}
