#include "masksourcedialog.hpp"

#include "fitsinfo.hpp"
#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "npyreader.hpp"
#include "openglwidget.hpp"
#include "view.hpp"

#include <QDialogButtonBox>
#include <algorithm>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
constexpr int kSourceRole = Qt::UserRole + 1; // index into m_sources
}

MaskSourceDialog::MaskSourceDialog(const QList<OpenMap> &openMaps,
                                   const View *current, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Add Mask From Map");
    resize(520, 440);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        "Select the columns to use as masks. Values are used as weights "
        "(0 = masked, 1 = kept); the nside must match the map."));

    m_tree = new QTreeWidget();
    m_tree->setHeaderLabels({"Source", "Details"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(m_tree, 1);

    m_message = new QLabel();
    m_message->setWordWrap(true);
    m_message->hide();
    layout->addWidget(m_message);

    auto *buttons = new QDialogButtonBox();
    auto *browse  = buttons->addButton("Add file...",
                                       QDialogButtonBox::ActionRole);
    m_okButton    = buttons->addButton("Add masks", QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, &MaskSourceDialog::browse);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &MaskSourceDialog::updateButton);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int)
    {
        if (item->data(0, kSourceRole).isValid())
            accept();
    });

    // The current tab first, then the other open maps
    QList<OpenMap> ordered = openMaps;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [current](const OpenMap &a, const OpenMap &b)
    { return a.view.data() == current && b.view.data() != current; });
    for (const OpenMap &m : ordered)
    {
        if (!m.view)
            continue;
        if (m.view->isComputed() || m.view->filePath().isEmpty())
            addMemoryMap(m);
        else
            addFile(m.view->filePath(), m.title);
    }
    m_tree->expandAll();
    updateButton();
}

// A file's map HDUs and columns
void
MaskSourceDialog::addFile(const QString &path, const QString &title)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    if (m_files.contains(abs))
        return;

    auto *top = new QTreeWidgetItem(m_tree);
    top->setText(0, title.isEmpty() ? QFileInfo(path).fileName() : title);
    top->setText(1, abs);
    top->setFlags(Qt::ItemIsEnabled);

    try
    {
        if (isNpyPath(path.toStdString()))
        {
            const int count = npyMapCount(path.toStdString());
            for (int i = 1; i <= count; ++i)
            {
                MaskSource s;
                s.path   = path;
                s.column = i;
                auto *item = new QTreeWidgetItem(top);
                item->setText(0, QString("column %1").arg(i));
                item->setText(1, count > 1 ? QString("row %1").arg(i - 1)
                                           : QString());
                item->setData(0, kSourceRole, int(m_sources.size()));
                m_sources << s;
            }
        }
        else
        {
            fitshandle file;
            file.open(path.toStdString());
            const FitsInfo info = scanFits(file);
            if (info.hdus.isEmpty())
                throw PlanckError("no HEALPix map found");
            for (const FitsHdu &h : info.hdus)
            {
                auto *hduItem = new QTreeWidgetItem(top);
                hduItem->setText(0, QString("HDU %1").arg(h.index));
                hduItem->setText(1, h.name);
                hduItem->setFlags(Qt::ItemIsEnabled);
                for (const FitsColumn &c : h.columns)
                {
                    MaskSource s;
                    s.path   = path;
                    s.hdu    = h.index;
                    s.column = c.index;
                    auto *item = new QTreeWidgetItem(hduItem);
                    item->setText(0, c.name.trimmed().isEmpty()
                                         ? QString("column %1").arg(c.index)
                                         : c.name.trimmed());
                    item->setText(1, QString("column %1").arg(c.index));
                    item->setData(0, kSourceRole, int(m_sources.size()));
                    m_sources << s;
                }
            }
        }
        m_files << abs;
    }
    catch (const std::exception &e)
    {
        top->setText(1, QString("cannot read: %1").arg(e.what()));
    }
    catch (const PlanckError &e)
    {
        top->setText(1, QString("cannot read: %1").arg(e.what()));
    }
}

// A map that has no file (a result of an operation)
void
MaskSourceDialog::addMemoryMap(const OpenMap &map)
{
    const OpenGLWidget *gl = map.view->glWidget();
    if (!gl->healpixMap())
        return;
    auto *top = new QTreeWidgetItem(m_tree);
    top->setText(0, map.title);
    top->setText(1, "computed map");
    top->setFlags(Qt::ItemIsEnabled);

    MaskSource s;
    s.label = map.title;
    s.map   = std::make_shared<const Healpix_Map<float>>(*gl->healpixMap());
    auto *item = new QTreeWidgetItem(top);
    item->setText(0, "the map");
    item->setText(1, QString("nside %1").arg(gl->healpixMap()->Nside()));
    item->setData(0, kSourceRole, int(m_sources.size()));
    m_sources << s;
}

void
MaskSourceDialog::browse()
{
    const auto files = QFileDialog::getOpenFileNames(
        this, "Add file", "",
        "Map Files (*.fits *.fit *.fts *.npy);;All Files (*)");
    for (const QString &f : files)
        addFile(f, QString());
    m_tree->expandAll();
}

void
MaskSourceDialog::updateButton()
{
    m_okButton->setEnabled(!selected().isEmpty());
}

QList<MaskSource>
MaskSourceDialog::selected() const
{
    QList<MaskSource> out;
    for (QTreeWidgetItem *item : m_tree->selectedItems())
    {
        const QVariant v = item->data(0, kSourceRole);
        if (v.isValid())
            out << m_sources.at(v.toInt());
    }
    return out;
}
