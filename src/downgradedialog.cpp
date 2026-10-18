#include "downgradedialog.hpp"

#include "openglwidget.hpp"
#include "view.hpp"

#include "healpix_cxx/fitshandle.h"
#include "healpix_cxx/healpix_map_fitsio.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent>

DowngradeDialog::DowngradeDialog(std::function<View *()> currentView,
                                 QWidget *parent)
    : QDialog(parent), m_currentView(std::move(currentView))
{
    setWindowTitle("Change Resolution");
    setMinimumWidth(440);

    auto *layout = new QVBoxLayout(this);

    m_source = new QLabel();
    layout->addWidget(m_source);

    auto *form    = new QFormLayout();
    m_nsideCombo  = new QComboBox();
    form->addRow("New nside:", m_nsideCombo);
    m_columnsBox = new QWidget();
    {
        auto *cl = new QVBoxLayout(m_columnsBox);
        cl->setContentsMargins(0, 0, 0, 0);
        m_columns = new QListWidget();
        m_columns->setMaximumHeight(96);
        m_columns->setToolTip("The map columns of the HDU (e.g. T, Q and U) to "
                              "change. The result is one tab with these "
                              "columns, and saving it writes them all.");
        cl->addWidget(m_columns);
    }
    form->addRow("Columns:", m_columnsBox);
    m_form = form;
    m_title = new QLineEdit();
    form->addRow("Title:", m_title);
    layout->addLayout(form);

    // Method
    auto *methodBox = new QGroupBox("Method");
    auto *methodLay = new QVBoxLayout(methodBox);

    m_pixelRadio = new QRadioButton("Average the pixels (ud_grade)");
    m_pixelRadio->setToolTip("Each new pixel is the mean of the pixels it "
                             "contains. Fast, but the power above the new "
                             "resolution is aliased into the map.");
    m_pixelRadio->setChecked(true);
    m_pessimistic = new QCheckBox("Pixel is UNSEEN if any sub-pixel is");
    m_pessimistic->setToolTip("Off: only the valid sub-pixels are averaged");
    auto *pixelOpt = new QHBoxLayout();
    pixelOpt->setContentsMargins(24, 0, 0, 0);
    pixelOpt->addWidget(m_pessimistic);
    methodLay->addWidget(m_pixelRadio);
    methodLay->addLayout(pixelOpt);

    m_harmRadio = new QRadioButton("Through the harmonics (map2alm, "
                                   "alm2map)");
    m_harmRadio->setToolTip("Transforms the map to a_lm, removes the modes "
                            "the new resolution cannot hold and transforms "
                            "back: a proper low-pass filter. Slow for large "
                            "maps.");
    methodLay->addWidget(m_harmRadio);

    auto *harmForm = new QFormLayout();
    harmForm->setContentsMargins(24, 0, 0, 0);
    m_iterations = new QSpinBox();
    m_iterations->setRange(0, 10);
    m_iterations->setValue(3);
    m_iterations->setToolTip("Refinements of the map2alm quadrature (0 = "
                             "plain map2alm)");
    harmForm->addRow("Iterations:", m_iterations);
    m_lmax = new QSpinBox();
    m_lmax->setToolTip("Highest l of the analysis. 3 * nside - 1 is the "
                       "usual choice; lower is faster and uses less memory "
                       "but leaves more aliasing.");
    harmForm->addRow("Analysis l max:", m_lmax);
    m_keepUnseen = new QCheckBox("Keep mostly-UNSEEN pixels UNSEEN");
    m_keepUnseen->setChecked(true);
    harmForm->addRow(m_keepUnseen);
    harmForm->addRow(
        "Input pixel window:",
        windowRow(m_windowIn,
                  "Pixel window function w_l of the map's current nside, "
                  "divided out of the a_lm. Optional."));
    harmForm->addRow(
        "Output pixel window:",
        windowRow(m_windowOut,
                  "Pixel window function w_l of the new nside, multiplied "
                  "into the a_lm. Optional."));
    methodLay->addLayout(harmForm);

    m_hint = new QLabel();
    m_hint->setWordWrap(true);
    m_hint->setContentsMargins(24, 0, 0, 0);
    methodLay->addWidget(m_hint);
    layout->addWidget(methodBox);

    m_message = new QLabel();
    m_message->setWordWrap(true);
    m_message->hide();
    layout->addWidget(m_message);

    m_progress = new QProgressBar();
    m_progress->setRange(0, 0); // busy
    m_progress->hide();
    layout->addWidget(m_progress);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch();
    m_computeButton = new QPushButton("Compute");
    m_computeButton->setDefault(true);
    auto *close = new QPushButton("Close");
    buttons->addWidget(m_computeButton);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(m_computeButton, &QPushButton::clicked, this,
            &DowngradeDialog::compute);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_title, &QLineEdit::textEdited, this,
            [this](const QString &) { m_title->setProperty("auto", false); });
    connect(m_pixelRadio, &QRadioButton::toggled, this,
            [this](bool) { updateOptions(); });
    connect(m_nsideCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { updateOptions(); });
    connect(m_lmax, &QSpinBox::valueChanged, this,
            [this](int) { updateOptions(); });
}

// A file path with a Browse button. The file is a FITS table (HEALPix's
// pixel_window_n*.fits), a .npy array or a text file with one w_l per line.
QWidget *
DowngradeDialog::windowRow(QLineEdit *&edit, const QString &tip)
{
    auto *row = new QWidget();
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit();
    edit->setPlaceholderText("none (FITS, .npy or text: w_l from l = 0)");
    edit->setToolTip(tip);
    auto *browse = new QPushButton("Browse...");
    lay->addWidget(edit, 1);
    lay->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit]()
    {
        const QString f = QFileDialog::getOpenFileName(
            this, "Pixel window function", QString(),
            "Window Files (*.fits *.fit *.fts *.npy *.txt *.dat *.csv);;All "
            "Files (*)");
        if (!f.isEmpty())
            edit->setText(f);
    });
    return row;
}

int
DowngradeDialog::nsideOut() const
{
    return m_nsideCombo->currentData().toInt();
}

// Reads the current tab: fills the nside choices (powers of two below the
// map's) and the l max range
void
DowngradeDialog::refresh()
{
    m_view = m_currentView ? m_currentView() : nullptr;
    const Healpix_Map<float> *map
        = m_view ? m_view->glWidget()->healpixMap() : nullptr;

    m_nsideCombo->clear();
    m_title->setProperty("auto", true); // a new map gets a new default title
    m_nside = map ? map->Nside() : 0;
    if (!map || m_nside < 2)
    {
        m_source->setText(map ? "This map is already at nside 1."
                              : "Open a map first.");
        m_computeButton->setEnabled(false);
        m_title->clear();
        updateOptions();
        return;
    }

    m_source->setText(QString("Map: %1, nside %2 (%3 pixels)")
                          .arg(m_view->displayName())
                          .arg(m_nside)
                          .arg(map->Npix()));
    {
        // Several map columns in the HDU (a FITS file, or a computed map)?
        const FitsHdu *hdu = m_view->fitsInfo().find(m_view->hdu());
        const int n        = hdu ? int(hdu->columns.size()) : 0;
        const bool usable
            = n > 1 && m_view->fitsInfo().kind == MapFileKind::Fits;
        m_columns->clear();
        if (usable)
            for (const FitsColumn &c : hdu->columns)
            {
                auto *item = new QListWidgetItem(
                    c.name.isEmpty() ? QString::number(c.index) : c.name);
                item->setData(Qt::UserRole, c.index);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Checked);
                m_columns->addItem(item);
            }
        m_form->setRowVisible(m_columnsBox, usable);
    }
    for (long n = m_nside / 2; n >= 1; n /= 2)
        m_nsideCombo->addItem(QString("%1  (%2 pixels)").arg(n).arg(12 * n * n),
                              int(n));
    m_computeButton->setEnabled(!m_busy);

    QSignalBlocker block(m_lmax);
    m_lmax->setRange(1, int(3 * m_nside - 1));
    m_lmax->setValue(int(3 * m_nside - 1));
    updateOptions();
}

void
DowngradeDialog::updateOptions()
{
    const bool harmonic = m_harmRadio->isChecked();
    m_pessimistic->setEnabled(!harmonic);
    m_iterations->setEnabled(harmonic);
    m_lmax->setEnabled(harmonic);
    m_keepUnseen->setEnabled(harmonic);
    m_windowIn->setEnabled(harmonic);
    m_windowOut->setEnabled(harmonic);

    if (m_nside < 2 || m_nsideCombo->count() == 0)
    {
        m_hint->clear();
        return;
    }

    const int out = nsideOut();
    if (m_title->text().isEmpty() || m_title->property("auto").toBool())
    {
        m_title->setText(QString("%1 (nside %2, %3)")
                             .arg(m_view ? m_view->displayName() : "map")
                             .arg(out)
                             .arg(harmonic ? "harmonic" : "ud_grade"));
        m_title->setProperty("auto", true);
    }

    if (harmonic)
    {
        const double gb = harmonicMemoryEstimate(int(m_nside), m_lmax->value())
                          / (1024.0 * 1024.0 * 1024.0);
        m_hint->setText(
            QString("Keeps l up to %1. Needs roughly %2 GB of memory; this can "
                    "take a long time above nside 1024.")
                .arg(std::min(3 * out - 1, m_lmax->value()))
                .arg(gb, 0, 'f', gb < 10 ? 1 : 0));
    }
    else
        m_hint->setText(QString("Each new pixel averages %1 pixels.")
                            .arg((m_nside / out) * (m_nside / out)));
}

void
DowngradeDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_progress->setVisible(busy);
    m_computeButton->setEnabled(!busy && m_nsideCombo->count() > 0);
    m_nsideCombo->setEnabled(!busy);
}

void
DowngradeDialog::showMessage(const QString &text, bool error)
{
    m_message->setText(text);
    m_message->setStyleSheet(error ? "color: #d33;" : "");
    m_message->setVisible(!text.isEmpty());
}

void
DowngradeDialog::compute()
{
    if (m_busy || !m_view || !m_view->glWidget()->healpixMap()
        || m_nsideCombo->count() == 0)
        return;

    // The worker gets its own copy: the tab may change while it runs
    auto in = std::make_shared<const Healpix_Map<float>>(
        *m_view->glWidget()->healpixMap());
    const SkyFrame frame = m_view->glWidget()->mapFrame();

    // Where the columns come from: memory (a computed map) or the file
    const bool all = m_columnsBox->isVisible();
    struct Ref
    {
        int index = 1; // FITS column
        size_t position = 0; // among the columns of the HDU
        QString name, unit;
    };
    std::vector<Ref> refs;
    std::vector<ComputedColumn> inMemory;
    QString path;
    int hdu     = 0;
    int current = 0;
    if (all)
    {
        if (m_view->isComputed())
            inMemory = m_view->computedColumns();
        else
        {
            path = m_view->filePath();
            hdu  = m_view->hdu();
        }
        const FitsHdu *h = m_view->fitsInfo().find(m_view->hdu());
        for (int i = 0; h && i < m_columns->count(); ++i)
        {
            const QListWidgetItem *item = m_columns->item(i);
            if (item->checkState() != Qt::Checked)
                continue;
            const FitsColumn &c = h->columns[i];
            if (c.index == m_view->column())
                current = int(refs.size());
            refs.push_back({c.index, size_t(i), c.name, c.unit});
        }
        if (refs.empty())
        {
            showMessage("Tick at least one column.", true);
            return;
        }
    }

    DowngradeOptions o;
    o.nsideOut    = nsideOut();
    o.method      = m_harmRadio->isChecked() ? DowngradeMethod::Harmonic
                                             : DowngradeMethod::Pixel;
    o.pessimistic = m_pessimistic->isChecked();
    o.iterations  = m_iterations->value();
    o.lmaxIn      = m_lmax->value();
    o.keepUnseen  = m_keepUnseen->isChecked();

    // The pixel windows (small files, read here so a bad one is reported
    // before anything starts)
    if (o.method == DowngradeMethod::Harmonic)
    {
        try
        {
            if (!m_windowIn->text().trimmed().isEmpty())
                o.windowIn = readPixelWindow(m_windowIn->text().trimmed());
            if (!m_windowOut->text().trimmed().isEmpty())
                o.windowOut = readPixelWindow(m_windowOut->text().trimmed());
        }
        catch (const std::exception &e)
        {
            showMessage(QString("Pixel window: %1").arg(e.what()), true);
            return;
        }
    }

    const QString title = m_title->text().trimmed().isEmpty()
                              ? QString("nside %1").arg(o.nsideOut)
                              : m_title->text().trimmed();

    setBusy(true);
    showMessage(o.method == DowngradeMethod::Harmonic
                    ? "Transforming to a_lm and back..."
                    : "Averaging the pixels...",
                false);

    if (!all)
    {
        auto *watcher = new QFutureWatcher<DowngradeResult>(this);
        connect(watcher, &QFutureWatcher<DowngradeResult>::finished, this,
                [this, watcher, title, frame]()
        {
            DowngradeResult r = watcher->result();
            watcher->deleteLater();
            setBusy(false);
            if (!r.map)
            {
                showMessage("Failed: " + r.error, true);
                return;
            }
            showMessage(QString("Done (nside %1). %2")
                            .arg(r.map->Nside())
                            .arg(r.notes.join(" ")),
                        false);
            emit mapReady(std::move(r.map), title, frame);
        });
        watcher->setFuture(
            QtConcurrent::run([in, o]() { return downgradeMap(*in, o); }));
        return;
    }

    struct Many
    {
        std::vector<ComputedColumn> columns;
        QStringList notes;
        QString error;
    };
    auto *watcher = new QFutureWatcher<Many>(this);
    connect(watcher, &QFutureWatcher<Many>::finished, this,
            [this, watcher, title, frame, current]()
    {
        Many r = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (r.columns.empty() || !r.error.isEmpty())
        {
            showMessage("Failed: " + r.error, true);
            return;
        }
        showMessage(QString("Done (nside %1, %2 column(s)). %3")
                        .arg(r.columns[0].map->Nside())
                        .arg(r.columns.size())
                        .arg(r.notes.join(" ")),
                    false);
        emit columnsReady(std::move(r.columns), title, frame, current);
    });
    watcher->setFuture(QtConcurrent::run(
        [refs, inMemory, path, hdu, o]()
    {
        Many r;
        try
        {
            fitshandle fits;
            if (!path.isEmpty())
                fits.open(path.toStdString());
            for (size_t i = 0; i < refs.size(); ++i)
            {
                std::shared_ptr<const Healpix_Map<float>> in;
                if (!path.isEmpty())
                {
                    fits.goto_hdu(hdu);
                    auto m = std::make_shared<Healpix_Map<float>>();
                    read_Healpix_map_from_fits(fits, *m, refs[i].index);
                    in = m;
                }
                else
                    in = inMemory[refs[i].position].map;

                const DowngradeResult d = downgradeMap(*in, o);
                if (!d.map)
                {
                    r.columns.clear();
                    r.error = QString("%1: %2").arg(refs[i].name, d.error);
                    return r;
                }
                if (i == 0)
                    r.notes = d.notes;
                ComputedColumn c;
                c.name = refs[i].name;
                c.unit = refs[i].unit;
                c.map  = d.map;
                r.columns.push_back(std::move(c));
            }
            if (o.method == DowngradeMethod::Harmonic && refs.size() > 1)
                r.notes << "Each column was filtered as a scalar field "
                           "(Q and U are not treated as a spin-2 pair).";
        }
        catch (const std::exception &e)
        {
            r.columns.clear();
            r.error = e.what();
        }
        return r;
    }));
}
