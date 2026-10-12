#include "smoothdialog.hpp"

#include "openglwidget.hpp"
#include "resolution.hpp"
#include "smoothing.hpp"
#include "view.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>
#include <numbers>

SmoothDialog::SmoothDialog(std::function<View *()> currentView,
                           QWidget *parent)
    : QDialog(parent), m_currentView(std::move(currentView))
{
    setWindowTitle("Beam Smoothing");
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    m_source     = new QLabel();
    layout->addWidget(m_source);

    auto *beamBox = new QGroupBox("Beam");
    auto *beamLay = new QVBoxLayout(beamBox);

    m_gaussRadio = new QRadioButton("Gaussian");
    m_gaussRadio->setChecked(true);
    beamLay->addWidget(m_gaussRadio);

    auto *fwhmRow = new QHBoxLayout();
    fwhmRow->setContentsMargins(24, 0, 0, 0);
    fwhmRow->addWidget(new QLabel("FWHM:"));
    m_fwhm        = new QDoubleSpinBox();
    m_fwhm->setDecimals(3);
    m_fwhm->setRange(0.001, 100000.0);
    m_fwhm->setValue(60.0);
    m_fwhm->setToolTip("Full width at half maximum of the Gaussian beam");
    m_unit = new QComboBox();
    m_unit->addItems({"arcmin", "degrees", "arcsec"});
    fwhmRow->addWidget(m_fwhm, 1);
    fwhmRow->addWidget(m_unit);
    beamLay->addLayout(fwhmRow);

    m_customRadio = new QRadioButton("Custom beam window file");
    m_customRadio->setToolTip("b_l from l = 0: a FITS table (the TEMPERATURE "
                              "or first column), a .npy array or a text file "
                              "with one value per line. The same window is "
                              "applied to T, E and B.");
    beamLay->addWidget(m_customRadio);
    auto *fileRow = new QHBoxLayout();
    fileRow->setContentsMargins(24, 0, 0, 0);
    m_beamFile   = new QLineEdit();
    m_beamFile->setPlaceholderText("FITS, .npy or text: b_l from l = 0");
    m_beamBrowse = new QPushButton("Browse...");
    fileRow->addWidget(m_beamFile, 1);
    fileRow->addWidget(m_beamBrowse);
    beamLay->addLayout(fileRow);
    layout->addWidget(beamBox);

    auto *form = new QFormLayout();
    m_polCheck = new QCheckBox("Also smooth Q and U (opens three maps)");
    m_polCheck->setToolTip("Reads the T, Q and U columns of the map's HDU and "
                           "transforms them together as a spin-2 field");
    form->addRow(m_polCheck);

    m_iterations = new QSpinBox();
    m_iterations->setRange(0, 10);
    m_iterations->setValue(3);
    m_iterations->setToolTip("Refinements of the map2alm quadrature (0 = "
                             "plain map2alm)");
    form->addRow("Iterations:", m_iterations);

    m_lmax = new QSpinBox();
    m_lmax->setToolTip("Highest l of the analysis. 3 * nside - 1 is the "
                       "usual choice; lower is faster and uses less memory.");
    form->addRow("Analysis l max:", m_lmax);

    m_keepUnseen = new QCheckBox("Keep UNSEEN pixels UNSEEN");
    m_keepUnseen->setChecked(true);
    form->addRow(m_keepUnseen);

    m_title = new QLineEdit();
    form->addRow("Title:", m_title);
    layout->addLayout(form);

    m_hint = new QLabel();
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);

    m_message = new QLabel();
    m_message->setWordWrap(true);
    m_message->hide();
    layout->addWidget(m_message);

    m_progress = new QProgressBar();
    m_progress->setRange(0, 0); // busy
    m_progress->hide();
    layout->addWidget(m_progress);

    auto *buttons   = new QHBoxLayout();
    m_computeButton = new QPushButton("Compute");
    m_computeButton->setDefault(true);
    auto *close = new QPushButton("Close");
    buttons->addStretch();
    buttons->addWidget(m_computeButton);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(m_computeButton, &QPushButton::clicked, this,
            &SmoothDialog::compute);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_title, &QLineEdit::textEdited, this,
            [this](const QString &) { m_title->setProperty("auto", false); });
    connect(m_gaussRadio, &QRadioButton::toggled, this,
            [this](bool) { updateOptions(); });
    connect(m_beamBrowse, &QPushButton::clicked, this, [this]()
    {
        const QString f = QFileDialog::getOpenFileName(
            this, "Beam window function", QString(),
            "Window Files (*.fits *.fit *.fts *.npy *.txt *.dat *.csv);;All "
            "Files (*)");
        if (!f.isEmpty())
        {
            m_beamFile->setText(f);
            m_customRadio->setChecked(true);
            updateTitle();
        }
    });
    connect(m_beamFile, &QLineEdit::textChanged, this,
            [this](const QString &) { updateTitle(); });
    connect(m_fwhm, &QDoubleSpinBox::valueChanged, this,
            [this](double) { updateTitle(); });
    connect(m_unit, &QComboBox::currentIndexChanged, this,
            [this](int) { updateTitle(); });
    connect(m_lmax, &QSpinBox::valueChanged, this,
            [this](int) { updateTitle(); });
    updateOptions();
}

double
SmoothDialog::fwhmRadians() const
{
    static const double perUnit[] = {std::numbers::pi / (180.0 * 60.0),
                                     std::numbers::pi / 180.0,
                                     std::numbers::pi / (180.0 * 3600.0)};
    return m_fwhm->value() * perUnit[m_unit->currentIndex()];
}

void
SmoothDialog::refresh()
{
    m_view = m_currentView ? m_currentView() : nullptr;
    const Healpix_Map<float> *map
        = m_view ? m_view->glWidget()->healpixMap() : nullptr;

    m_title->setProperty("auto", true); // a new map gets a new default title
    m_nside = map ? map->Nside() : 0;
    if (!map)
    {
        m_source->setText("Open a map first.");
        m_computeButton->setEnabled(false);
        m_title->clear();
        m_hint->clear();
        return;
    }

    m_source->setText(QString("Map: %1, nside %2 (%3 pixels)")
                          .arg(m_view->displayName())
                          .arg(m_nside)
                          .arg(map->Npix()));
    m_computeButton->setEnabled(!m_busy);

    const bool pol = hasPolarization(m_view->filePath(), m_view->hdu())
                     && !m_view->isComputed();
    m_polCheck->setEnabled(pol);
    m_polCheck->setChecked(pol);
    m_polCheck->setToolTip(pol ? "Reads the T, Q and U columns of the map's "
                                 "HDU and transforms them together as a "
                                 "spin-2 field"
                               : "The map's HDU has no Q and U columns");

    QSignalBlocker block(m_lmax);
    m_lmax->setRange(1, int(3 * m_nside - 1));
    m_lmax->setValue(int(3 * m_nside - 1));
    updateTitle();
}

void
SmoothDialog::updateOptions()
{
    const bool gauss = m_gaussRadio->isChecked();
    m_fwhm->setEnabled(gauss);
    m_unit->setEnabled(gauss);
    m_beamFile->setEnabled(!gauss);
    m_beamBrowse->setEnabled(!gauss);
    updateTitle();
}

void
SmoothDialog::updateTitle()
{
    if (m_nside < 1)
        return;

    if (m_title->text().isEmpty() || m_title->property("auto").toBool())
    {
        const QString beam
            = m_gaussRadio->isChecked()
                  ? QString("%1 %2").arg(m_fwhm->value()).arg(
                      m_unit->currentText())
                  : QFileInfo(m_beamFile->text()).fileName();
        m_title->setText(QString("%1 (%2 beam)")
                             .arg(m_view ? m_view->displayName() : "map")
                             .arg(beam.isEmpty() ? "custom" : beam));
        m_title->setProperty("auto", true);
    }

    // The beam is mostly gone above l ~ 1 / sigma; warn when it's narrower
    // than the pixels, where smoothing does very little
    const double pixel = std::sqrt(4.0 * std::numbers::pi / (12.0 * m_nside * m_nside));
    QString hint;
    if (m_gaussRadio->isChecked() && fwhmRadians() < pixel)
        hint = "The beam is narrower than a pixel: the map will hardly "
               "change.";
    const double gb = harmonicMemoryEstimate(int(m_nside), m_lmax->value())
                      / (1024.0 * 1024.0 * 1024.0);
    hint += QString(" Needs roughly %1 GB of memory; this can take a long "
                    "time above nside 1024.")
                .arg(gb, 0, 'f', gb < 10 ? 1 : 0);
    m_hint->setText(hint.trimmed());
}

void
SmoothDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_progress->setVisible(busy);
    m_computeButton->setEnabled(!busy && m_nside > 0);
    m_fwhm->setEnabled(!busy && m_gaussRadio->isChecked());
}

void
SmoothDialog::showMessage(const QString &text, bool error)
{
    m_message->setText(text);
    m_message->setStyleSheet(error ? "color: #d33;" : "");
    m_message->setVisible(!text.isEmpty());
}

void
SmoothDialog::compute()
{
    if (m_busy || !m_view || !m_view->glWidget()->healpixMap())
        return;

    // The worker gets its own copy: the tab may change while it runs
    auto in = std::make_shared<const Healpix_Map<float>>(
        *m_view->glWidget()->healpixMap());
    const SkyFrame frame = m_view->glWidget()->mapFrame();

    SmoothOptions o;
    o.fwhmRad    = fwhmRadians();
    o.iterations = m_iterations->value();
    o.lmax       = m_lmax->value();
    o.keepUnseen = m_keepUnseen->isChecked();
    if (m_polCheck->isEnabled() && m_polCheck->isChecked())
    {
        o.polarization = true;
        o.path         = m_view->filePath();
        o.hdu          = m_view->hdu();
    }
    if (m_customRadio->isChecked())
    {
        try
        {
            if (m_beamFile->text().trimmed().isEmpty())
                throw std::runtime_error("choose a beam window file");
            o.beam = readPixelWindow(m_beamFile->text().trimmed());
        }
        catch (const std::exception &e)
        {
            showMessage(QString("Beam window: %1").arg(e.what()), true);
            return;
        }
    }

    const QString title = m_title->text().trimmed().isEmpty()
                              ? QString("Smoothed")
                              : m_title->text().trimmed();

    setBusy(true);
    showMessage("Transforming to a_lm and back...", false);

    auto *watcher = new QFutureWatcher<SmoothResult>(this);
    connect(watcher, &QFutureWatcher<SmoothResult>::finished, this,
            [this, watcher, title, frame]()
    {
        SmoothResult r = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        if (!r.map)
        {
            showMessage("Failed: " + r.error, true);
            return;
        }
        showMessage("Done. " + r.notes.join(" "), false);
        if (r.mapQ && r.mapU)
        {
            emit mapReady(std::move(r.map), title + " [T]", frame);
            emit mapReady(std::move(r.mapQ), title + " [Q]", frame);
            emit mapReady(std::move(r.mapU), title + " [U]", frame);
        }
        else
            emit mapReady(std::move(r.map), title, frame);
    });
    watcher->setFuture(
        QtConcurrent::run([in, o]() { return smoothMap(*in, o); }));
}
