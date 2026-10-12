#pragma once

#include "masksource.hpp"
#include "openmap.hpp"

#include <QDialog>
#include <QList>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// Picks masks from the columns of the open maps' files, or of any other FITS /
// .npy file (a mask is often stored next to the map in the same file). Each
// file is a node with its map HDUs and columns below it.
class MaskSourceDialog : public QDialog
{
    Q_OBJECT
public:
    // `openMaps` are the tabs; `current` is the one the masks are for
    MaskSourceDialog(const QList<OpenMap> &openMaps, const View *current,
                     QWidget *parent = nullptr);

    // The selected columns
    QList<MaskSource> selected() const;

private:
    void addFile(const QString &path, const QString &title);
    void addMemoryMap(const OpenMap &map);
    void browse();
    void updateButton();

    QTreeWidget *m_tree   = nullptr;
    QLabel *m_message     = nullptr;
    QPushButton *m_okButton = nullptr;
    QList<MaskSource> m_sources; // by the item's data
    QStringList m_files;         // already in the tree
};
