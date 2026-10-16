#include "mapoperationdialog.hpp"

#include "fitsinfo.hpp"
#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "mapexpression.hpp"
#include "npyreader.hpp"
#include "view.hpp"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace
{
enum Column
{
    NameColumn,
    SourceColumn,
    HduColumn,
    MapColumn,
    ColumnCount
};

// Turns the list of names into the expression a preset stands for
QString
presetExpression(int preset, const QStringList &n)
{
    if (n.size() < 2)
        return n.value(0);
    const QString a = n[0], b = n[1];
    switch (preset)
    {
        case 1: // sum
            return n.join(" + ");
        case 2: // difference
            return a + " - " + b;
        case 3: // product
            return n.join(" * ");
        case 4: // ratio
            return a + " / " + b;
        case 5: // mean
            return QString("(%1) / %2").arg(n.join(" + ")).arg(n.size());
        case 6: // absolute difference
            return QString("abs(%1 - %2)").arg(a, b);
        case 7: // maximum
        {
            QString e = n[0];
            for (int i = 1; i < n.size(); ++i)
                e = QString("max(%1, %2)").arg(e, n[i]);
            return e;
        }
        case 8: // minimum
        {
            QString e = n[0];
            for (int i = 1; i < n.size(); ++i)
                e = QString("min(%1, %2)").arg(e, n[i]);
            return e;
        }
    }
    return {};
}
} // namespace

MapOperationDialog::MapOperationDialog(std::function<QList<OpenMap>()> openMaps,
                                       QWidget *parent)
    : QDialog(parent), m_openMaps(std::move(openMaps))
{
    initUI();
}

void
MapOperationDialog::initUI()
{
    setWindowTitle("Operate on Maps");
    resize(640, 520);

    auto *layout = new QVBoxLayout(this);

    // --- Inputs ---
    auto *inputs       = new QGroupBox("Input maps");
    auto *inputsLayout = new QVBoxLayout(inputs);

    m_table = new QTableWidget(0, ColumnCount);
    m_table->setHorizontalHeaderLabels({"Name", "Source", "HDU", "Column"});
    m_table->horizontalHeader()->setSectionResizeMode(SourceColumn,
                                                      QHeaderView::Stretch);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    inputsLayout->addWidget(m_table);

    auto *buttons   = new QHBoxLayout();
    m_addTabButton  = new QPushButton("Add Open Map");
    m_addFileButton = new QPushButton("Add File...");
    m_removeButton  = new QPushButton("Remove");
    buttons->addWidget(m_addTabButton);
    buttons->addWidget(m_addFileButton);
    buttons->addStretch();
    buttons->addWidget(m_removeButton);
    inputsLayout->addLayout(buttons);
    layout->addWidget(inputs);

    connect(m_addTabButton, &QPushButton::clicked, this, [this]()
    {
        const QList<OpenMap> maps
            = m_openMaps ? m_openMaps() : QList<OpenMap>();
        if (maps.isEmpty())
        {
            showMessage("No map is open.", true);
            return;
        }
        QMenu menu(this);
        for (const OpenMap &m : maps)
            menu.addAction(m.title, this, [this, m]() { addTab(m); });
        menu.exec(
            m_addTabButton->mapToGlobal(QPoint(0, m_addTabButton->height())));
    });
    connect(m_addFileButton, &QPushButton::clicked, this, [this]()
    {
        const QString dir
            = QSettings().value("general/lastDirectory").toString();
        const QStringList files = QFileDialog::getOpenFileNames(
            this, "Add Maps", dir.isEmpty() ? QDir::homePath() : dir,
            "Map Files (*.fits *.fit *.fts *.npy *.npz);;"
            "FITS Files (*.fits *.fit *.fts);;All Files (*)");
        for (const QString &f : files)
            addFile(f);
    });
    connect(m_removeButton, &QPushButton::clicked, this,
            &MapOperationDialog::removeSelected);

    // --- Operation ---
    auto *operation = new QGroupBox("Operation");
    auto *form      = new QFormLayout(operation);

    m_presetCombo = new QComboBox();
    m_presetCombo->addItems({"Custom", "Add", "Subtract (A − B)", "Multiply",
                             "Divide (A / B)", "Mean", "Absolute difference",
                             "Maximum", "Minimum"});
    form->addRow("Operation:", m_presetCombo);

    m_expression = new QLineEdit("A - B");
    m_expression->setToolTip(
        "Use the names of the input maps with + - * / ^ and parentheses.\n"
        "Functions: abs, sqrt, log, log10, exp, sin, cos, tan, min(a, b), "
        "max(a, b), pow(a, b). Constant: pi.\n"
        "Example: (A - B) / sqrt(C)");
    form->addRow("Expression:", m_expression);

    m_policyCombo = new QComboBox();
    m_policyCombo->addItem("Highest nside", int(ResolutionPolicy::Highest));
    m_policyCombo->addItem("Lowest nside", int(ResolutionPolicy::Lowest));
    m_policyCombo->addItem("First map's nside", int(ResolutionPolicy::First));
    m_policyCombo->setToolTip("The resolution of the result when the maps "
                              "differ; the others are up- or downgraded.");
    form->addRow("Resolution:", m_policyCombo);

    m_title = new QLineEdit("Result");
    form->addRow("Name of the result:", m_title);
    layout->addWidget(operation);

    connect(m_presetCombo, &QComboBox::activated, this,
            [this](int i) { applyPreset(i); });
    connect(m_expression, &QLineEdit::textEdited, this,
            [this]() { m_presetCombo->setCurrentIndex(0); });

    // --- Result ---
    m_message = new QLabel();
    m_message->setWordWrap(true);
    m_progress = new QProgressBar();
    m_progress->setRange(0, 0); // busy indicator
    m_progress->setVisible(false);
    layout->addWidget(m_message);
    layout->addWidget(m_progress);

    auto *bottom    = new QHBoxLayout();
    m_computeButton = new QPushButton("Compute && Open");
    m_computeButton->setDefault(true);
    auto *close = new QPushButton("Close");
    bottom->addStretch();
    bottom->addWidget(m_computeButton);
    bottom->addWidget(close);
    layout->addLayout(bottom);

    connect(m_computeButton, &QPushButton::clicked, this,
            &MapOperationDialog::compute);
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    showMessage("Add two or more maps, then choose an operation. The result "
                "opens as a new map.",
                false);
}

void
MapOperationDialog::showMessage(const QString &text, bool error)
{
    m_message->setStyleSheet(error ? "color: #d9534f;" : "");
    m_message->setText(text);
}

QString
MapOperationDialog::nextName() const
{
    for (char c = 'A'; c <= 'Z'; ++c)
    {
        bool used = false;
        for (const Row &r : m_rows)
            used |= r.name == QString(QChar(c));
        if (!used)
            return QString(QChar(c));
    }
    return {};
}

QStringList
MapOperationDialog::names() const
{
    QStringList list;
    for (const Row &r : m_rows)
        list << r.name;
    return list;
}

void
MapOperationDialog::addTab(const OpenMap &map)
{
    if (!map.view)
        return;
    Row row;
    row.view   = map.view;
    row.source = map.title + "  (open)";
    addRow(row);
}

void
MapOperationDialog::addFile(const QString &path)
{
    Row row;
    row.path   = path;
    row.source = QFileInfo(path).fileName();
    if (!QFileInfo::exists(path))
    {
        showMessage(QString("%1 does not exist.").arg(row.source), true);
        return;
    }
    try
    {
        if (isNumpyPath(path.toStdString()))
            row.info = scanNumpy(path.toStdString());
        else
        {
            fitshandle file;
            file.open(path.toStdString());
            row.info = scanFits(file);
        }
    }
    catch (const PlanckError &)
    {
        // HEALPix's own exception type, not derived from std::exception
        showMessage(QString("%1 is not a readable map file.").arg(row.source),
                    true);
        return;
    }
    catch (const std::exception &e)
    {
        showMessage(QString("%1: %2").arg(row.source, e.what()), true);
        return;
    }
    if (row.info.hdus.isEmpty())
    {
        showMessage(QString("%1 has no HEALPix map.").arg(row.source), true);
        return;
    }
    addRow(row);
}

void
MapOperationDialog::addRow(Row row)
{
    row.name = nextName();
    if (row.name.isEmpty())
    {
        showMessage("At most 26 maps can be used.", true);
        return;
    }

    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, NameColumn, new QTableWidgetItem(row.name));
    m_table->setItem(r, SourceColumn, new QTableWidgetItem(row.source));

    if (row.view)
    {
        // An open map is used as it is (its current HDU and column)
        const View *v     = row.view;
        const QString hdu = v->filePath().isEmpty() ? QString("-")
                                                    : QString::number(v->hdu());
        const QString col = v->filePath().isEmpty()
                                ? QString("-")
                                : QString::number(v->column());
        m_table->setItem(r, HduColumn, new QTableWidgetItem(hdu));
        m_table->setItem(r, MapColumn, new QTableWidgetItem(col));
    }
    else
    {
        row.hduCombo = new QComboBox();
        row.colCombo = new QComboBox();
        for (const FitsHdu &h : row.info.hdus)
            row.hduCombo->addItem(
                h.name.isEmpty() ? QString::number(h.index)
                                 : QString("%1: %2").arg(h.index).arg(h.name),
                h.index);
        m_table->setCellWidget(r, HduColumn, row.hduCombo);
        m_table->setCellWidget(r, MapColumn, row.colCombo);
    }
    m_rows << row;

    if (row.hduCombo)
    {
        const int index = m_rows.size() - 1;
        fillColumns(index, row.info.hdus.first().index, 0);
        connect(row.hduCombo, &QComboBox::activated, this, [this, index](int) {
            fillColumns(index, m_rows[index].hduCombo->currentData().toInt(),
                        0);
        });
    }

    showMessage(QString("%1 map(s) added.").arg(m_rows.size()), false);
}

void
MapOperationDialog::fillColumns(int row, int hdu, int column)
{
    Row &r = m_rows[row];
    QSignalBlocker block(r.colCombo);
    r.colCombo->clear();
    if (const FitsHdu *h = r.info.find(hdu))
        for (const FitsColumn &c : h->columns)
            r.colCombo->addItem(
                c.name.isEmpty() ? QString::number(c.index) : c.name, c.index);
    const int i = r.colCombo->findData(column);
    if (i >= 0)
        r.colCombo->setCurrentIndex(i);
}

void
MapOperationDialog::removeSelected()
{
    const int r = m_table->currentRow();
    if (r < 0 || r >= m_rows.size())
        return;
    m_table->removeRow(r);
    m_rows.removeAt(r);
    // The combos hold their row index; reconnect by rebuilding is not needed
    // because the index is looked up from the widget
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].hduCombo)
        {
            QComboBox *combo = m_rows[i].hduCombo;
            combo->disconnect(this);
            connect(combo, &QComboBox::activated, this, [this, i](int)
            { fillColumns(i, m_rows[i].hduCombo->currentData().toInt(), 0); });
        }
}

void
MapOperationDialog::applyPreset(int preset)
{
    if (preset == 0)
        return;
    if (m_rows.size() < 2)
    {
        showMessage("Add at least two maps first.", true);
        m_presetCombo->setCurrentIndex(0);
        return;
    }
    m_expression->setText(presetExpression(preset, names()));
}

std::vector<OperandSource>
MapOperationDialog::operands() const
{
    std::vector<OperandSource> list;
    for (const Row &r : m_rows)
    {
        OperandSource op;
        op.name = r.name;
        if (r.view)
        {
            const View *v = r.view;
            if (!v->filePath().isEmpty())
            {
                // Read the values from the file again: the view shows a
                // binary 0 / 1 map with its zeros as unseen
                op.path   = v->filePath();
                op.hdu    = v->hdu();
                op.column = v->column();
                op.frame  = v->glWidget()->mapFrame();
            }
            else if (v->glWidget()->healpixMap())
            {
                // A computed map only exists in memory
                op.map = std::make_shared<Healpix_Map<float>>(
                    *v->glWidget()->healpixMap());
                op.frame = v->glWidget()->mapFrame();
            }
        }
        else
        {
            op.path   = r.path;
            op.hdu    = r.hduCombo->currentData().toInt();
            op.column = r.colCombo->currentData().toInt();
        }
        list.push_back(op);
    }
    return list;
}

void
MapOperationDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_progress->setVisible(busy);
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_computeButton, m_addTabButton, m_addFileButton, m_removeButton,
             m_expression, m_presetCombo, m_policyCombo, m_title, m_table})
        w->setEnabled(!busy);
}

void
MapOperationDialog::compute()
{
    if (m_busy)
        return;
    if (m_rows.isEmpty())
    {
        showMessage("Add at least one map.", true);
        return;
    }

    // Report a mistake in the expression before reading any file
    MapExpression check;
    QString error;
    if (!check.compile(m_expression->text(), names(), &error))
    {
        showMessage("Expression: " + error, true);
        return;
    }

    const std::vector<OperandSource> sources = operands();
    const QString expression                 = m_expression->text();
    const auto policy = ResolutionPolicy(m_policyCombo->currentData().toInt());
    QString title     = m_title->text().trimmed();
    if (title.isEmpty())
        title = "Result";
    title += ": " + expression;

    setBusy(true);
    showMessage("Reading the maps and computing…", false);

    auto *watcher = new QFutureWatcher<OperationResult>(this);
    connect(watcher, &QFutureWatcher<OperationResult>::finished, this,
            [this, watcher, title]()
    {
        const OperationResult r = watcher->result();
        watcher->deleteLater();
        setBusy(false);

        if (!r.map)
        {
            showMessage("Failed: " + r.error, true);
            return;
        }

        QString text = QString("Opened \"%1\": nside %2, %3 of %4 pixels have "
                               "a value.")
                           .arg(title)
                           .arg(r.map->Nside())
                           .arg(r.validPixels)
                           .arg(r.map->Npix());
        if (!r.notes.isEmpty())
            text += "\n" + r.notes.join("\n");
        showMessage(text, false);
        emit mapReady(r.map, title, r.frame);
    });
    watcher->setFuture(QtConcurrent::run([sources, expression, policy]()
    { return runOperation(sources, expression, policy); }));
}
