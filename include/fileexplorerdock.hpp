#pragma once

#include <QDockWidget>

class QCheckBox;
class QFileSystemModel;
class QLineEdit;
class QSortFilterProxyModel;
class QToolButton;
class QTreeView;

// A folder tree for opening maps: shows the folders and the map files
// (FITS, .npy, .npz) under a root folder. Activating a file asks the window
// to open it; files can also be dragged onto the window.
class FileExplorerDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit FileExplorerDock(QWidget *parent = nullptr);

    QString rootPath() const;
    // Shows the contents of the folder (ignored if it does not exist)
    void setRootPath(const QString &path);

    // Selects and scrolls to the file when it is under the root; never
    // changes the root, so browsing is not disturbed
    void revealFile(const QString &path);

signals:
    void openRequested(const QString &path);
    void rootChanged(const QString &path);

private:
    void applyFilter();
    void showContextMenu(const QPoint &pos);
    QString pathAt(const QModelIndex &index) const;

    QFileSystemModel *m_model = nullptr;
    QTreeView *m_tree         = nullptr;
    QLineEdit *m_pathEdit     = nullptr;
    QToolButton *m_upButton   = nullptr;
    QCheckBox *m_showAll      = nullptr;
};
