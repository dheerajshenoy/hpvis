#include "powerspectrumdock.hpp"
#include "unseen.hpp"
#include "parallel.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "alm_powspec_tools.h"
#include "appsettings.hpp"
#include "fitsinfo.hpp"
#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "powspec.h"
#include "masterspectrum.hpp"
#include "crossspectrum.hpp"
#include "openglwidget.hpp"
#include "plotrenderer.hpp"
#include "theoryspectrum.hpp"
#include "units.hpp"
#include "view.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineSeries>
#include <QMenu>
#include <QMessageBox>
#include <QJSValue>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QValueAxis>
#include <QVariantList>
#include <QtConcurrent>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>

// Highest multipole for the lmax choice: 0 = 3 nside - 1 (the most the map
// holds), 1 = 2 nside, 2 = nside
static int
lmaxFor(int nside, int mode)
{
    return mode == 0 ? 3 * nside - 1 : mode == 1 ? 2 * nside : nside;
}

// The maps behind one computation; Q and U are empty for an intensity map
struct Stokes
{
    Healpix_Map<double> T, Q, U;
    bool polarized = false;
    bool unseen    = false; // some pixels were UNSEEN / NaN (zeroed)
};

// Copies to double in RING order; UNSEEN / NaN pixels become zero
static Healpix_Map<double>
toDouble(const Healpix_Map<float> &map, bool &unseen)
{
    Healpix_Map<double> m(map.Nside(), map.Scheme(), SET_NSIDE);
    const bool any = parallel::reduceRange<bool>(
        map.Npix(),
        [&](long begin, long end)
    {
        bool bad = false;
        for (long i = begin; i < end; ++i)
        {
            const bool good = isSeen(map[i]);
            bad |= !good;
            m[i] = good ? map[i] : 0.0;
        }
        return bad;
    }, [](bool &total, const bool &part) { total = total || part; });
    unseen |= any;
    // map2alm expects RING ordering
    if (m.Scheme() == NEST)
        m.swap_scheme();
    return m;
}

// Looks for T, Q and U columns in the map's HDU. Returns false (leaving
// `out` untouched) when it isn't a polarized map.
static bool
readStokes(const PowerSpectrumInput &in, Stokes &out)
{
    if (in.path.isEmpty() || in.hdu <= 0)
        return false;

    try
    {
        fitshandle file;
        file.open(in.path.toStdString());
        const FitsInfo info = scanFits(file);
        const FitsHdu *hdu  = info.find(in.hdu);
        if (!hdu)
            return false;

        int qCol = 0, uCol = 0, tCol = 0;
        for (const FitsColumn &col : hdu->columns)
        {
            const QString n = col.name.trimmed().toUpper();
            if (!qCol && n.startsWith('Q'))
                qCol = col.index;
            else if (!uCol && n.startsWith('U'))
                uCol = col.index;
        }
        if (!qCol || !uCol)
            return false;
        for (const FitsColumn &col : hdu->columns)
            if (col.index != qCol && col.index != uCol)
            {
                tCol = col.index;
                break;
            }
        if (!tCol)
            return false;

        Healpix_Map<float> maps[3];
        const int cols[3] = {tCol, qCol, uCol};
        for (int k = 0; k < 3; ++k)
        {
            file.goto_hdu(in.hdu);
            read_Healpix_map_from_fits(file, maps[k], cols[k]);
            // The view may show the map with another pixel ordering than the
            // file says (View > Pixel Ordering): read the values the same way
            if (in.map && maps[k].Scheme() != in.map->Scheme())
                maps[k].Healpix_Base::SetNside(maps[k].Nside(),
                                               in.map->Scheme());
        }
        if (maps[1].Nside() != maps[0].Nside()
            || maps[2].Nside() != maps[0].Nside())
            return false;

        out.T         = toDouble(maps[0], out.unseen);
        out.Q         = toDouble(maps[1], out.unseen);
        out.U         = toDouble(maps[2], out.unseen);
        out.polarized = true;
        return true;
    }
    catch (const PlanckError &)
    {
        return false; // fall back to the intensity map
    }
}

// Shared between the worker thread and the GUI: how far a computation is,
// and whether the user asked to stop it
struct PowerSpectrumJob
{
    // Reading + converting the map, the first transform, then a synthesis and
    // an analysis for each refinement iteration
    int iterations = 1;
    std::atomic<int> total{0}; // set when the work differs from the default
    int totalSteps() const
    {
        const int t = total;
        return t > 0 ? t : 1 + 1 + 2 * iterations;
    }

    std::atomic<int> step{0}; // finished steps
    std::atomic<const char *> stage{"Starting"};
    std::atomic<bool> cancelled{false};
    QElapsedTimer clock;

    struct Cancelled
    {
    };
    void begin(const char *what)
    {
        if (cancelled)
            throw Cancelled();
        stage = what;
    }
    void done()
    {
        ++step;
    }
};

// map2alm_iter / map2alm_pol_iter written out, so progress can be reported
// (and a cancel noticed) between the transforms
static PowSpec
computeCl(const Stokes &s, int lmax, PowerSpectrumJob &job)
{
    const int nside = s.T.Nside();
    arr<double> weights(2 * nside);
    weights.fill(1.0); // or load HEALPix ring weights
    const int iter = job.iterations;

    Alm<xcomplex<double>> aT(lmax, lmax);
    if (!s.polarized)
    {
        job.begin("Transform to a_lm");
        map2alm(s.T, aT, weights, false);
        job.done();
        for (int i = 0; i < iter; ++i)
        {
            job.begin("Refinement: synthesis");
            Healpix_Map<double> rec(nside, s.T.Scheme(), SET_NSIDE);
            alm2map(aT, rec);
            for (long m = 0; m < s.T.Npix(); ++m)
                rec[m] = s.T[m] - rec[m];
            job.done();
            job.begin("Refinement: analysis");
            map2alm(rec, aT, weights, true);
            job.done();
        }
        PowSpec ps(1, lmax);
        extract_powspec(aT, ps); // C_l = mean over m of |a_lm|^2
        return ps;
    }

    Alm<xcomplex<double>> aE(lmax, lmax), aB(lmax, lmax);
    job.begin("Transform to a_lm");
    map2alm_pol(s.T, s.Q, s.U, aT, aE, aB, weights, false);
    job.done();
    for (int i = 0; i < iter; ++i)
    {
        job.begin("Refinement: synthesis");
        Healpix_Map<double> rT(nside, s.T.Scheme(), SET_NSIDE),
            rQ(nside, s.T.Scheme(), SET_NSIDE),
            rU(nside, s.T.Scheme(), SET_NSIDE);
        alm2map_pol(aT, aE, aB, rT, rQ, rU);
        for (long m = 0; m < s.T.Npix(); ++m)
        {
            rT[m] = s.T[m] - rT[m];
            rQ[m] = s.Q[m] - rQ[m];
            rU[m] = s.U[m] - rU[m];
        }
        job.done();
        job.begin("Refinement: analysis");
        map2alm_pol(rT, rQ, rU, aT, aE, aB, weights, true);
        job.done();
    }
    PowSpec ps(6, lmax); // TT, EE, BB, TE, TB, EB
    extract_powspec(aT, aE, aB, ps);
    return ps;
}

static Spectra
fromPowSpec(const PowSpec &ps)
{
    auto copy = [](const arr<double> &a)
    {
        return std::vector<double>(a.begin(), a.end());
    };
    Spectra s;
    s.TT = copy(ps.tt());
    if (ps.Num_specs() > 1) // polarized maps only
    {
        s.EE = copy(ps.gg());
        s.BB = copy(ps.cc());
        s.TE = copy(ps.tg());
    }
    if (ps.Num_specs() > 4)
    {
        s.TB = copy(ps.tc());
        s.EB = copy(ps.gc());
    }
    return s;
}

struct CalcResult
{
    Spectra spectra;
    bool unseen    = false;
    bool cancelled = false;
    QString error;
    // Set when the spectra were corrected for the mask
    bool corrected = false;
    int lmax       = 0;
    double fsky    = 0.0;
    double condition = 1.0;
    QStringList notes;
    std::shared_ptr<const master::Result> master;
};

// Bandpowers of a mask-corrected computation as the plot's spectra
static Spectra
fromMaster(const master::Result &r)
{
    Spectra s;
    s.ell = r.ell;
    s.TT  = r.TT;
    s.EE  = r.EE;
    s.BB  = r.BB;
    s.TE  = r.TE;
    s.TB  = r.TB;
    s.EB  = r.EB;
    return s;
}

// The spectra corrected for the mask (MASTER, with optional purification).
// `st` is consumed.
static CalcResult
computeMasked(const PowerSpectrumInput &in, Stokes &st, int wanted,
              PowerSpectrumJob &job)
{
    CalcResult r;
    r.unseen          = st.unseen;
    const MaskCorrection &c = in.correction;
    const bool pol    = st.polarized;

    // The correction keeps several full-resolution copies of the map, and
    // NaMaster's apodization takes a time that grows with nside^2 times the
    // apodization area (minutes at nside 1024, hours above)
    const long nside = st.T.Nside();
    if (nside > 2048)
        throw std::runtime_error(
            "Correcting for a mask is limited to maps up to nside 2048 (it "
            "keeps several copies of the map in memory). Downgrade the map "
            "first (Tools > Change Resolution).");
    if (c.apodization != 0 && nside > 1024)
        throw std::runtime_error(
            "Apodizing the mask is too slow above nside 1024. Downgrade the "
            "map, or load a mask that is already apodized and choose \"No "
            "apodization\".");

    // The coupling matrix grows with lmax^2 (four times that for E and B)
    const int cap = pol ? 2048 : 4096;
    const int lmax = std::min(wanted, cap);
    if (lmax < wanted)
        r.notes << QString("lmax was limited to %1: the mode-coupling matrix "
                           "of the mask grows with lmax\u00b2.")
                       .arg(lmax);

    bool pureE = c.purifyE, pureB = c.purifyB;
    if ((pureE || pureB) && !pol)
    {
        r.notes << "Purification needs Q and U maps, so it was not applied "
                   "to this intensity map.";
        pureE = pureB = false;
    }
    if ((pureE || pureB) && c.apodization == 0)
    {
        r.notes << "Purification needs an apodized mask, so it was not "
                   "applied.";
        pureE = pureB = false;
    }

    master::Options o;
    o.lmax       = lmax;
    o.bandwidth  = c.bandwidth;
    o.apodization = c.apodization == 1   ? master::Apodization::C1
                    : c.apodization == 2 ? master::Apodization::C2
                    : c.apodization == 3 ? master::Apodization::Smooth
                                         : master::Apodization::None;
    o.apodizationScale = c.apodizationScale;
    o.purifyE    = pureE;
    o.purifyB    = pureB;
    o.iterations = job.iterations;

    master::Input mi;
    mi.polarized = pol;
    mi.T         = std::move(st.T);
    if (pol)
    {
        mi.Q = std::move(st.Q);
        mi.U = std::move(st.U);
    }
    // The weights follow the map's pixel order; unseen pixels have none
    {
        const Healpix_Map<float> &map = *in.map;
        Healpix_Map<double> w(map.Nside(), map.Scheme(), SET_NSIDE);
        const std::vector<float> &weights = *in.weights;
        parallel::forRange(map.Npix(), [&](long begin, long end)
        {
            for (long i = begin; i < end; ++i)
            {
                const bool good = isSeen(map[i]);
                w[i]            = good ? double(weights[size_t(i)]) : 0.0;
            }
        });
        if (w.Scheme() == NEST)
            w.swap_scheme();
        mi.weights = std::move(w);
    }

    master::Hooks hooks;
    hooks.total = [&job](int n)
    {
        job.total = n + 1; // the map was read already
    };
    hooks.stage = [&job](const char *what) { job.begin(what); };
    hooks.done  = [&job]() { job.done(); };

    auto result = std::make_shared<master::Result>(
        master::compute(mi, o, hooks));
    r.spectra   = fromMaster(*result);
    r.corrected = true;
    r.lmax      = lmax;
    r.fsky      = result->fsky;
    r.condition = result->conditionNumber;
    r.master    = std::move(result);
    return r;
}

PowerSpectrumDock::PowerSpectrumDock(QWidget *parent)
    : QDockWidget("Power Spectrum", parent)
{
    setObjectName("PowerSpectrumDock");
    initUI();
    refresh();
}

void
PowerSpectrumDock::initUI()
{
    auto *content = new QWidget();
    auto *layout  = new QVBoxLayout(content);

    m_computeButton = new QPushButton("Compute");
    connect(m_computeButton, &QPushButton::clicked, this, [this]()
    {
        // The button is "Cancel" while the current map is computing
        if (m_view && m_busy.contains(m_view.data()))
            cancel();
        else
            compute();
    });

    m_progressBar = new QProgressBar();
    m_progressBar->setFormat("%v of %m steps");
    m_progressLabel = new QLabel();
    m_progressLabel->setAlignment(Qt::AlignCenter);
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(250);
    connect(m_progressTimer, &QTimer::timeout, this,
            &PowerSpectrumDock::updateProgress);

    m_statusLabel = new QLabel();
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);

    m_spectrumCombo = new QComboBox();
    m_spectrumCombo->setVisible(false); // filled once the spectra are known
    connect(m_spectrumCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { replot(); });

    m_warningLabel = new QLabel();
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(
        "background: #5a4300; color: #ffe9a8; padding: 6px; "
        "border-radius: 4px;");
    m_warningLabel->setVisible(false);

    m_chartView = new QQuickView();
    m_chartView->setColor(QColor("#121217"));
    m_chartView->setResizeMode(QQuickView::SizeRootObjectToView);
    m_chartContainer = QWidget::createWindowContainer(m_chartView);
    m_chartContainer->setMinimumSize(320, 220);

    // How much work to do: fewer refinement rounds and a lower lmax are faster
    m_accuracyCombo = new QComboBox();
    m_accuracyCombo->addItem("Fast", 0);
    m_accuracyCombo->addItem("Normal", 1);
    m_accuracyCombo->addItem("Accurate", 3);
    m_accuracyCombo->setToolTip(
        "Refinement rounds after the first transform: 0, 1 or 3.\n"
        "Each round costs two more transforms and improves accuracy.");
    m_lmaxCombo = new QComboBox();
    m_lmaxCombo->addItem("2 \u00d7 nside", 1);
    m_lmaxCombo->addItem("3 \u00d7 nside \u2212 1 (maximum)", 0);
    m_lmaxCombo->addItem("nside", 2);
    m_lmaxCombo->setToolTip(
        "Highest multipole. The cost grows roughly with its cube, and above\n"
        "about 2 \u00d7 nside the spectrum is mostly noise and aliasing.");
    {
        QSettings settings;
        const int a = m_accuracyCombo->findData(
            settings.value("powerspectrum/iterations", 1).toInt());
        const int l = m_lmaxCombo->findData(
            settings.value("powerspectrum/lmaxMode", 1).toInt());
        m_accuracyCombo->setCurrentIndex(a >= 0 ? a : 1);
        m_lmaxCombo->setCurrentIndex(l >= 0 ? l : 0);
    }
    connect(m_accuracyCombo, &QComboBox::currentIndexChanged, this, [this]()
    {
        QSettings().setValue("powerspectrum/iterations",
                             m_accuracyCombo->currentData());
        refresh();
    });
    connect(m_lmaxCombo, &QComboBox::currentIndexChanged, this, [this]()
    {
        QSettings().setValue("powerspectrum/lmaxMode",
                             m_lmaxCombo->currentData());
        refresh();
    });

    m_infoLabel = new QLabel();
    m_infoLabel->setAlignment(Qt::AlignCenter);

    auto *options = new QHBoxLayout();
    options->addWidget(new QLabel("Accuracy:"));
    options->addWidget(m_accuracyCombo);
    options->addWidget(new QLabel("lmax:"));
    options->addWidget(m_lmaxCombo);
    options->addStretch();

    // --- Correction for the masks applied to the map (NaMaster) ---
    m_maskGroup = new QGroupBox("Correct for the mask (NaMaster)");
    m_maskGroup->setCheckable(true);
    m_maskGroup->setToolTip(
        "Apply the map's masks as weights and correct the spectra for them\n"
        "(MASTER method: pseudo-C\u2113, mode-coupling matrix, bandpowers).\n"
        "Without it the masks are ignored. Needs a mask in the Masks menu.");
    m_apodCombo = new QComboBox();
    m_apodCombo->addItem("No apodization", 0);
    m_apodCombo->addItem("C1 apodization", 1);
    m_apodCombo->addItem("C2 apodization", 2);
    m_apodCombo->addItem("Smooth apodization", 3);
    m_apodCombo->setToolTip(
        "Smooths the mask's edge (a sharp edge leaks power between scales).\n"
        "C1 / C2: a cosine ramp of the given width; Smooth: Gaussian.\n"
        "Slow for large nside. Needed for purification.");
    m_apodScale = new QDoubleSpinBox();
    m_apodScale->setRange(0.1, 30.0);
    m_apodScale->setDecimals(1);
    m_apodScale->setSingleStep(0.5);
    m_apodScale->setSuffix(QString::fromUtf8("\u00b0"));
    m_apodScale->setToolTip("Apodization scale (width of the ramp)");
    m_bandwidthCombo = new QComboBox();
    for (int w : {4, 8, 16, 32, 64})
        m_bandwidthCombo->addItem(QString::fromUtf8("\u0394\u2113 = %1").arg(w), w);
    m_bandwidthCombo->setToolTip(
        "Multipoles per bandpower. Wider bands are less noisy and the\n"
        "coupling matrix is easier to invert.");
    m_purifyECheck = new QCheckBox("Purify E");
    m_purifyBCheck = new QCheckBox("Purify B");
    const QString purifyTip
        = "Remove the leakage between E and B modes caused by the mask\n"
          "(Smith & Zaldarriaga 2007). Needs Q and U maps and an apodized\n"
          "mask. Costs more time and memory, and raises the E (or B) noise.";
    m_purifyECheck->setToolTip(purifyTip);
    m_purifyBCheck->setToolTip(purifyTip);
    {
        auto *maskLayout = new QVBoxLayout(m_maskGroup);
        maskLayout->setContentsMargins(6, 6, 6, 6);
        // The options are hidden until the correction is turned on
        m_maskOptions = new QWidget();
        auto *optionsLayout = new QVBoxLayout(m_maskOptions);
        optionsLayout->setContentsMargins(0, 0, 0, 0);
        maskLayout->addWidget(m_maskOptions);
        auto *r1 = new QHBoxLayout();
        r1->addWidget(m_apodCombo);
        r1->addWidget(m_apodScale);
        r1->addStretch();
        auto *r2 = new QHBoxLayout();
        r2->addWidget(m_bandwidthCombo);
        r2->addWidget(m_purifyECheck);
        r2->addWidget(m_purifyBCheck);
        r2->addStretch();
        m_maskHint = new QLabel();
        m_maskHint->setWordWrap(true);
        optionsLayout->addLayout(r1);
        optionsLayout->addLayout(r2);
        optionsLayout->addWidget(m_maskHint);
    }
    {
        const QSettings settings;
        const MaskCorrection d;
        m_maskGroup->setChecked(
            settings.value("powerspectrum/maskCorrect", false).toBool());
        m_maskOptions->setVisible(m_maskGroup->isChecked());
        const int a = m_apodCombo->findData(
            settings.value("powerspectrum/apodization", 2).toInt());
        m_apodCombo->setCurrentIndex(a >= 0 ? a : 2);
        m_apodScale->setValue(
            settings.value("powerspectrum/apodScale", d.apodizationScale)
                .toDouble());
        const int b = m_bandwidthCombo->findData(
            settings.value("powerspectrum/bandwidth", d.bandwidth).toInt());
        m_bandwidthCombo->setCurrentIndex(b >= 0 ? b : 2);
        m_purifyECheck->setChecked(
            settings.value("powerspectrum/purifyE", false).toBool());
        m_purifyBCheck->setChecked(
            settings.value("powerspectrum/purifyB", false).toBool());
    }
    auto onMaskOption = [this]()
    {
        QSettings settings;
        settings.setValue("powerspectrum/maskCorrect",
                          m_maskGroup->isChecked());
        settings.setValue("powerspectrum/apodization",
                          m_apodCombo->currentData());
        settings.setValue("powerspectrum/apodScale", m_apodScale->value());
        settings.setValue("powerspectrum/bandwidth",
                          m_bandwidthCombo->currentData());
        settings.setValue("powerspectrum/purifyE", m_purifyECheck->isChecked());
        settings.setValue("powerspectrum/purifyB", m_purifyBCheck->isChecked());
        refresh();
    };
    connect(m_maskGroup, &QGroupBox::toggled, m_maskOptions,
            &QWidget::setVisible);
    connect(m_maskGroup, &QGroupBox::toggled, this, onMaskOption);
    connect(m_apodCombo, &QComboBox::currentIndexChanged, this, onMaskOption);
    connect(m_apodScale, &QDoubleSpinBox::editingFinished, this, onMaskOption);
    connect(m_bandwidthCombo, &QComboBox::currentIndexChanged, this,
            onMaskOption);
    connect(m_purifyECheck, &QCheckBox::toggled, this, onMaskOption);
    connect(m_purifyBCheck, &QCheckBox::toggled, this, onMaskOption);

    // What the plot shows: the quantity, the axis scales, and export
    m_quantityCombo = new QComboBox();
    m_quantityCombo->addItem("D\u2113 = \u2113(\u2113+1) C\u2113 / 2\u03c0", 0);
    m_quantityCombo->addItem("C\u2113", 1);
    m_quantityCombo->addItem("\u2113\u00b2 C\u2113", 2);
    m_logXCheck    = new QCheckBox("Log \u2113");
    m_logYCheck    = new QCheckBox("Log y");
    m_exportButton = new QToolButton();
    m_exportButton->setText("Export");
    m_exportButton->setPopupMode(QToolButton::InstantPopup);
    auto *exportMenu = new QMenu(m_exportButton);
    exportMenu->addAction("Plot as &image...", this,
                          &PowerSpectrumDock::exportImage);
    exportMenu->addAction("Spectra as &CSV...", this,
                          [this]() { exportCsv(); });
    m_exportButton->setMenu(exportMenu);
    {
        QSettings settings;
        const int q = m_quantityCombo->findData(
            settings.value("powerspectrum/quantity", 0).toInt());
        m_quantityCombo->setCurrentIndex(q >= 0 ? q : 0);
        m_logXCheck->setChecked(
            settings.value("powerspectrum/logX", false).toBool());
        m_logYCheck->setChecked(
            settings.value("powerspectrum/logY", false).toBool());
    }
    auto onPlotOption = [this]()
    {
        QSettings settings;
        settings.setValue("powerspectrum/quantity",
                          m_quantityCombo->currentData());
        settings.setValue("powerspectrum/logX", m_logXCheck->isChecked());
        settings.setValue("powerspectrum/logY", m_logYCheck->isChecked());
        this->replot();
    };
    connect(m_quantityCombo, &QComboBox::currentIndexChanged, this,
            onPlotOption);
    connect(m_logXCheck, &QCheckBox::toggled, this, onPlotOption);
    connect(m_logYCheck, &QCheckBox::toggled, this, onPlotOption);

    // Plot colors: background, foreground (text and axes) and the line
    {
        const AppSettings defaults = AppSettings::load();
        m_plotBackground           = defaults.plotBackground;
        m_plotForeground           = defaults.plotForeground;
        m_plotLine                 = defaults.plotLine;
    }
    auto makeColorButton = [this](QPushButton *&button, QColor &color,
                                  const QString &tip, const QString &title)
    {
        button = new QPushButton();
        button->setFixedSize(26, 20);
        button->setToolTip(tip);
        showColorOnButton(button, color);
        connect(button, &QPushButton::clicked, this,
                [this, &button, &color, title]()
        { pickPlotColor(button, color, title); });
    };
    makeColorButton(m_bgButton, m_plotBackground, "Plot background",
                    "Plot Background");
    makeColorButton(m_fgButton, m_plotForeground,
                    "Plot foreground (text and axes)", "Plot Foreground");
    makeColorButton(m_lineButton, m_plotLine, "Plot line", "Plot Line");
    m_chartView->setColor(m_plotBackground);

    // Comparison with other maps and with curves loaded from files
    m_modeCombo = new QComboBox();
    m_modeCombo->addItem("Overlay", 0);
    m_modeCombo->addItem("Ratio to this map", 1);
    m_modeCombo->addItem("Difference from this map", 2);
    m_modeCombo->setToolTip("How the compared spectra are shown relative to "
                            "the spectrum of the current map");
    m_compareButton = new QToolButton();
    m_compareButton->setText("Compare");
    m_compareButton->setPopupMode(QToolButton::InstantPopup);
    auto *compareMenu = new QMenu(m_compareButton);
    connect(compareMenu, &QMenu::aboutToShow, this,
            &PowerSpectrumDock::buildCompareMenu);
    m_compareButton->setMenu(compareMenu);
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this,
            [this]() { replot(); });
    m_theoryOn = QSettings().value("powerspectrum/theory", false).toBool();
    m_binTheoryCheck = new QCheckBox("Bin theory");
    m_binTheoryCheck->setToolTip(
        "Show the theory spectrum convolved with the bandpower windows of\n"
        "the mask-corrected spectrum (what the bandpowers should match),\n"
        "instead of the unbinned curve. Only for mask-corrected spectra.");
    m_binTheoryCheck->setChecked(
        QSettings().value("powerspectrum/binTheory", true).toBool());
    connect(m_binTheoryCheck, &QCheckBox::toggled, this, [this](bool on)
    {
        QSettings().setValue("powerspectrum/binTheory", on);
        replot(true);
    });
    m_commonCheck = new QCheckBox("Common \u2113 range");
    m_commonCheck->setToolTip(
        "Show every curve only up to the highest multipole they all have.\n"
        "Maps of different nside, or computed with a different lmax, end at "
        "different multipoles.");
    m_commonCheck->setChecked(
        QSettings().value("powerspectrum/commonRange", false).toBool());
    connect(m_commonCheck, &QCheckBox::toggled, this, [this](bool on)
    {
        QSettings().setValue("powerspectrum/commonRange", on);
        replot();
    });

    // The plot's controls sit in rows that are shown with the plot
    auto makeRow = [this](QWidget *&row)
    {
        row         = new QWidget();
        auto *l     = new QHBoxLayout(row);
        l->setContentsMargins(0, 0, 0, 0);
        m_plotRows << row;
        return l;
    };
    QWidget *compareRow = nullptr, *axisRow = nullptr, *plotRow = nullptr;
    auto *compareOptions = makeRow(compareRow);
    compareOptions->addWidget(m_compareButton);
    compareOptions->addWidget(m_modeCombo);
    compareOptions->addWidget(m_commonCheck);
    compareOptions->addWidget(m_binTheoryCheck);
    compareOptions->addStretch();

    // Below the plot: what it shows and the axis scales
    auto *axisOptions = makeRow(axisRow);
    axisOptions->addWidget(new QLabel("y:"));
    axisOptions->addWidget(m_quantityCombo);
    axisOptions->addWidget(m_logXCheck);
    axisOptions->addWidget(m_logYCheck);
    axisOptions->addStretch();

    auto *plotOptions = makeRow(plotRow);
    plotOptions->addStretch();
    plotOptions->addWidget(m_bgButton);
    plotOptions->addWidget(m_fgButton);
    plotOptions->addWidget(m_lineButton);
    plotOptions->addWidget(m_exportButton);

    auto *top = new QHBoxLayout();
    top->addWidget(m_spectrumCombo, 1);

    layout->addLayout(options);
    layout->addWidget(m_maskGroup);
    layout->addLayout(top);
    layout->addWidget(m_computeButton); // its own row, the full width
    layout->addWidget(m_warningLabel);
    layout->addWidget(m_infoLabel);
    layout->addWidget(m_progressBar);
    layout->addWidget(m_progressLabel);
    layout->addWidget(m_statusLabel);
    layout->addWidget(compareRow);
    layout->addWidget(plotRow);
    layout->addWidget(m_chartContainer, 1);
    layout->addWidget(axisRow);
    setWidget(content);
}

MaskCorrection
PowerSpectrumDock::currentCorrection() const
{
    MaskCorrection c;
    c.enabled          = m_maskGroup->isChecked();
    c.apodization      = m_apodCombo->currentData().toInt();
    c.apodizationScale = m_apodScale->value();
    c.bandwidth        = m_bandwidthCombo->currentData().toInt();
    c.purifyE          = m_purifyECheck->isChecked();
    c.purifyB          = m_purifyBCheck->isChecked();
    return c;
}

void
PowerSpectrumDock::setView(View *view)
{
    m_view = view;

    // The accuracy and lmax of a tab are those its spectrum was computed with
    // (so they are not reported as "changed" after a tab switch); a tab
    // without a spectrum gets the saved defaults. Not persisted: the combos
    // are only set to show them.
    int iterations = -1, lmaxMode = -1;
    const auto it = m_results.constFind(view);
    if (view && it != m_results.constEnd())
    {
        iterations = it->iterations;
        lmaxMode   = it->lmaxMode;
    }
    else
    {
        QSettings settings;
        iterations = settings.value("powerspectrum/iterations", 1).toInt();
        lmaxMode   = settings.value("powerspectrum/lmaxMode", 1).toInt();
    }
    {
        QSignalBlocker a(m_accuracyCombo), b(m_lmaxCombo);
        const int ia = m_accuracyCombo->findData(iterations);
        const int il = m_lmaxCombo->findData(lmaxMode);
        if (ia >= 0)
            m_accuracyCombo->setCurrentIndex(ia);
        if (il >= 0)
            m_lmaxCombo->setCurrentIndex(il);
    }
    refresh();
}

void
PowerSpectrumDock::forget(const View *view)
{
    m_results.remove(view);
    m_compareViews.remove(view);
    if (auto job = m_jobs.take(view))
        job->cancelled = true; // let the worker stop early
    m_busy.remove(view);
    if (m_view.data() == view)
        m_view = nullptr;
    refresh();
}

// Shows the state of the current view: nothing, busy, a result or "not yet"
void
PowerSpectrumDock::refresh()
{
    const View *view = m_view.data();
    const bool have  = view && view->glWidget()->healpixMap();
    const bool busy  = have && m_busy.contains(view);
    const auto it    = m_results.constFind(view);
    const bool done  = have && it != m_results.constEnd();

    bool masksNow = false;
    if (view)
        for (int i = 0; i < view->glWidget()->maskCount(); ++i)
            masksNow |= view->glWidget()->maskEnabled(i);

    // Bin theory only concerns mask-corrected spectra
    m_binTheoryCheck->setVisible(m_maskGroup->isChecked());

    m_computeButton->setEnabled(have);
    m_computeButton->setText(busy ? "Cancel" : done ? "Recompute" : "Compute");

    m_accuracyCombo->setEnabled(!busy);
    m_lmaxCombo->setEnabled(!busy);
    // The correction options apply when the map has a mask; purification also
    // needs an apodized mask
    m_maskGroup->setEnabled(have && !busy);
    m_apodScale->setEnabled(m_apodCombo->currentData().toInt() != 0);
    const bool canPurify = m_apodCombo->currentData().toInt() != 0;
    m_purifyECheck->setEnabled(canPurify);
    m_purifyBCheck->setEnabled(canPurify);
    m_maskHint->setText(masksNow ? QString()
                                 : "No mask is enabled on this map (Masks "
                                   "menu), so there is nothing to correct "
                                   "for.");
    m_maskHint->setVisible(!masksNow);
    m_infoLabel->setVisible(false);

    m_progressBar->setVisible(busy);
    m_progressLabel->setVisible(busy);
    if (busy)
    {
        m_progressTimer->start();
        updateProgress();
    }
    else
        m_progressTimer->stop();

    m_statusLabel->setVisible(!done);
    m_chartContainer->setVisible(done);
    for (QWidget *row : m_plotRows)
        row->setVisible(done);
    m_spectrumCombo->setVisible(false);
    m_warningLabel->setVisible(false);

    if (!have)
    {
        m_statusLabel->setText("Open a map to compute its power spectrum.");
        return;
    }
    if (busy)
    {
        m_statusLabel->setText("Computing power spectrum\u2026");
        if (!done)
            return;
    }
    if (!done)
    {
        m_statusLabel->setText("Press Compute to calculate the power "
                               "spectrum of this map.");
        return;
    }

    // What the result is based on, and whether it still matches the view
    QStringList notes;
    if (it->maskActive && !it->corrected)
        notes << "\u26a0 A mask was applied to this map, but the spectra were "
                 "computed from the unmasked map and are not corrected for "
                 "the mask, so they may be wrong. Turn on \"Correct for the "
                 "mask\" and press Recompute.";
    if (it->unseen && !it->corrected)
        notes << "\u26a0 The map has unseen pixels, which are set to zero. "
                 "The spectra are not corrected for them and may be wrong.";
    if (it->corrected)
    {
        for (const QString &n : it->notes)
            notes << "\u2139 " + n;
        if (it->condition > 1e7)
            notes << QString("\u26a0 The mode-coupling matrix is "
                             "ill-conditioned (condition number %1): the "
                             "bandpowers amplify noise. Use wider bandpowers "
                             "or a larger mask.")
                         .arg(it->condition, 0, 'g', 3);
    }

    if (it->hdu != view->hdu() || it->column != view->column()
        || it->maskActive != masksNow)
        notes << "The map's HDU, column or masks changed since this was "
                 "computed. Press Recompute to update it.";
    else
    {
        const MaskCorrection now = currentCorrection();
        const bool wantCorrected = now.enabled && masksNow;
        if (it->corrected != wantCorrected
            || (it->corrected && !(it->correction == now)))
            notes << "The mask correction settings changed since this was "
                     "computed. Press Recompute to apply them.";
    }

    QString info = QString("lmax = %1, %2 refinement round(s)")
                       .arg(it->lmax)
                       .arg(it->iterations);
    if (it->corrected)
    {
        static const char *const apod[] = {"no apodization", "C1", "C2",
                                           "Smooth"};
        info += QString::fromUtf8(", \u0394\u2113 = %1, f_sky = %2")
                    .arg(it->correction.bandwidth)
                    .arg(it->fsky, 0, 'f', 3);
        info += QString(", %1").arg(apod[it->correction.apodization]);
        if (it->correction.apodization != 0)
            info += QString::fromUtf8(" %1\u00b0")
                        .arg(it->correction.apodizationScale, 0, 'f', 1);
        if (it->master && (it->master->purifyE || it->master->purifyB))
            info += QString(", purified %1%2")
                        .arg(it->master->purifyE ? "E" : "",
                             it->master->purifyB ? "B" : "");
    }
    m_infoLabel->setText(info);
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setVisible(true);
    if (it->iterations != m_accuracyCombo->currentData().toInt()
        || it->lmaxWanted
               != lmaxFor(it->nside, m_lmaxCombo->currentData().toInt()))
        notes << "The accuracy or lmax setting changed since this was "
                 "computed. Press Recompute to apply it.";
    m_binTheoryCheck->setEnabled(it->corrected);

    m_warningLabel->setText(notes.join("\n"));
    m_warningLabel->setVisible(!notes.isEmpty());

    // Only offer the spectra that were actually computed
    const Spectra &sp = it->spectra;
    struct Entry
    {
        const char *name;
        const std::vector<double> *data;
    };
    const Entry entries[] = {{"TT", &sp.TT}, {"EE", &sp.EE}, {"BB", &sp.BB},
                             {"TE", &sp.TE}, {"TB", &sp.TB}, {"EB", &sp.EB}};

    const QString previous = m_spectrumCombo->currentText();
    {
        QSignalBlocker block(m_spectrumCombo);
        m_spectrumCombo->clear();
        for (const Entry &e : entries)
            if (!e.data->empty())
                m_spectrumCombo->addItem(e.name);
        const int keep = m_spectrumCombo->findText(previous);
        m_spectrumCombo->setCurrentIndex(keep >= 0 ? keep : 0);
    }
    m_spectrumCombo->setVisible(m_spectrumCombo->count() > 1);

    if (!m_plotLoaded)
    {
        m_chartView->setSource(QUrl("qrc:/src/qml/powerspectrumplot.qml"));
        m_plotLoaded = true;
        applyPlotColors();
    }
    updatePlot(sp);
}

void
PowerSpectrumDock::compute()
{
    View *view = m_view.data();
    if (!view || !view->glWidget()->healpixMap() || m_busy.contains(view))
        return;

    OpenGLWidget *gl = view->glWidget();
    PowerSpectrumInput input;
    input.path = view->filePath();
    input.hdu  = view->hdu();
    // A copy keeps the worker safe if the map is replaced meanwhile
    input.map  = std::make_shared<Healpix_Map<float>>(*gl->healpixMap());
    for (int i = 0; i < gl->maskCount(); ++i)
        input.maskActive |= gl->maskEnabled(i);
    input.correction = currentCorrection();
    if (input.correction.enabled && input.maskActive)
    {
        auto weights = std::make_shared<std::vector<float>>(gl->maskWeights());
        if (weights->empty())
            input.correction.enabled = false;
        else
            input.weights = std::move(weights);
    }
    else
        input.correction.enabled = false;
    const int column = view->column();

    const int iterations = m_accuracyCombo->currentData().toInt();
    const int lmaxMode   = m_lmaxCombo->currentData().toInt();
    auto job             = std::make_shared<PowerSpectrumJob>();
    job->iterations      = iterations;
    job->clock.start();
    m_jobs.insert(view, job);
    m_busy.insert(view);
    refresh();

    QPointer<View> guard(view);
    auto *watcher = new QFutureWatcher<CalcResult>(this);
    connect(watcher, &QFutureWatcher<CalcResult>::finished, this,
            [this, watcher, guard, input, column, iterations, lmaxMode,
             r_lmax = lmaxFor(int(input.map->Nside()), lmaxMode)]()
    {
        const CalcResult r = watcher->result();
        watcher->deleteLater();
        if (!guard) // the tab was closed meanwhile
            return;

        m_busy.remove(guard.data());
        m_jobs.remove(guard.data());
        if (r.cancelled)
        {
            if (m_view.data() == guard.data())
            {
                refresh();
                if (!m_results.contains(guard.data()))
                    m_statusLabel->setText("Cancelled.");
            }
            return;
        }
        if (r.spectra.TT.empty())
        {
            if (m_view.data() == guard.data())
            {
                refresh();
                m_statusLabel->setText(
                    r.error.isEmpty()
                        ? QString("Failed to compute the power spectrum.")
                        : QString("Failed to compute the power spectrum: %1")
                              .arg(r.error));
                m_statusLabel->show();
            }
            return;
        }

        Result res;
        res.spectra    = r.spectra;
        res.unseen     = r.unseen;
        res.maskActive = input.maskActive;
        res.hdu        = input.hdu;
        res.column     = column;
        res.iterations = iterations;
        res.lmaxMode   = lmaxMode;
        res.lmax       = r.lmax;
        res.lmaxWanted = r_lmax;
        res.nside      = int(input.map->Nside());
        res.corrected  = r.corrected;
        res.correction = input.correction;
        res.fsky       = r.fsky;
        res.condition  = r.condition;
        res.notes      = r.notes;
        res.master     = r.master;
        m_results.insert(guard.data(), res);
        if (m_view.data() == guard.data())
            refresh();
    });

    // The worker owns copies / shared data, so it stays valid even if the
    // tab is closed before it finishes
    watcher->setFuture(QtConcurrent::run([input, job, lmaxMode]() -> CalcResult
    {
        try
        {
            job->begin("Reading the map");
            Stokes st;
            if (!readStokes(input, st))
            {
                st.polarized = false;
                st.T         = toDouble(*input.map, st.unseen);
            }
            job->done();
            const int lmax = lmaxFor(st.T.Nside(), lmaxMode);
            if (input.correction.enabled && input.weights
                && !input.weights->empty())
                return computeMasked(input, st, lmax, *job);
            CalcResult r;
            r.spectra = fromPowSpec(computeCl(st, lmax, *job));
            r.unseen  = st.unseen;
            r.lmax    = lmax;
            return r;
        }
        catch (const PowerSpectrumJob::Cancelled &)
        {
            CalcResult r;
            r.cancelled = true;
            return r;
        }
        catch (const std::exception &e)
        {
            CalcResult r;
            r.error = QString::fromUtf8(e.what());
            return r;
        }
    }));
}

// Asks the running computation of the current map to stop. It does so between
// transforms, as a single transform cannot be interrupted.
void
PowerSpectrumDock::cancel()
{
    const auto it = m_jobs.constFind(m_view.data());
    if (it == m_jobs.constEnd())
        return;
    (*it)->cancelled = true;
    updateProgress();
}

void
PowerSpectrumDock::updateProgress()
{
    const auto it = m_jobs.constFind(m_view.data());
    if (it == m_jobs.constEnd())
        return;
    const PowerSpectrumJob &job = **it;

    const int secs = int(job.clock.elapsed() / 1000);
    const QString elapsed
        = QString("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));

    m_progressBar->setRange(0, job.totalSteps());
    m_progressBar->setValue(job.step);
    m_progressLabel->setText(
        job.cancelled ? QString("Cancelling after the current step\u2026 (%1)")
                            .arg(elapsed)
                      : QString("%1 \u2014 %2 elapsed")
                            .arg(job.stage.load())
                            .arg(elapsed));
}

void
PowerSpectrumDock::showColorOnButton(QPushButton *button, const QColor &color)
{
    button->setStyleSheet(
        QString("background-color: %1; border: 1px solid gray;")
            .arg(color.name()));
}

void
PowerSpectrumDock::applyPlotColors()
{
    m_chartView->setColor(m_plotBackground);
    if (QQuickItem *root = m_chartView->rootObject())
    {
        root->setProperty("bgColor", m_plotBackground);
        root->setProperty("fgColor", m_plotForeground);
        root->setProperty("lineColor", m_plotLine);
    }
    replot(true);
}

void
PowerSpectrumDock::setPlotColors(const QColor &background,
                                 const QColor &foreground, const QColor &line)
{
    m_plotBackground = background;
    m_plotForeground = foreground;
    m_plotLine       = line;
    showColorOnButton(m_bgButton, background);
    showColorOnButton(m_fgButton, foreground);
    showColorOnButton(m_lineButton, line);
    applyPlotColors();
}

// A color chosen in the dock is saved like the one in the settings dialog
void
PowerSpectrumDock::pickPlotColor(QPushButton *button, QColor &color,
                                 const QString &title)
{
    const QColor chosen = QColorDialog::getColor(color, this, title);
    if (!chosen.isValid())
        return;
    color = chosen;
    showColorOnButton(button, color);
    applyPlotColors();

    QSettings settings;
    settings.setValue("plot/background", m_plotBackground.name());
    settings.setValue("plot/foreground", m_plotForeground.name());
    settings.setValue("plot/line", m_plotLine.name());
    emit plotColorsChanged(m_plotBackground, m_plotForeground, m_plotLine);
}

// The theoretical spectrum chosen in the settings, loaded when it changes.
// Null (with a message box) when it cannot be read.
const Spectra *
PowerSpectrumDock::theorySpectra()
{
    const AppSettings s = AppSettings::load();
    TheorySource src;
    src.preset = TheoryPreset(s.theoryPreset);
    src.file   = s.theoryFile;
    src.unit   = TheoryUnit(s.theoryUnit); // the default: the settings'

    // The unit the map says it is in (FITS TUNIT) wins: the theory is drawn
    // in the unit of the map being analysed. Only CMB temperatures can be
    // scaled from the spectrum; any other unit leaves the setting.
    bool unitFromMap = false;
    if (m_view)
        if (const auto u = units::fromFits(m_view->unit());
            u && units::isCmbTemperature(*u))
        {
            src.unit = *u == units::Unit::K_CMB    ? TheoryUnit::Kelvin
                       : *u == units::Unit::mK_CMB ? TheoryUnit::MilliKelvin
                                                   : TheoryUnit::MicroKelvin;
            unitFromMap = true;
        }
    const QString key = QString("%1|%2|%3|%4")
                            .arg(s.theoryPreset)
                            .arg(s.theoryFile)
                            .arg(int(src.unit))
                            .arg(unitFromMap);
    if (!m_theoryLoaded || key != m_theoryKey)
    {
        m_theoryKey    = key;
        m_theoryLoaded = true;
        m_theory       = Spectra();
        try
        {
            m_theory     = loadTheorySpectra(src);
            m_theoryName = src.label();
            if (unitFromMap)
            {
                static const char *names[] = {"uK", "mK", "K"};
                m_theoryName += QString(" (%1, from the map)")
                                    .arg(names[int(src.unit)]);
            }
        }
        catch (const std::exception &e)
        {
            m_theoryOn = false;
            QMessageBox::warning(this, "Theory spectrum",
                                 QString("%1\n\nChoose another one in "
                                         "Settings > Theory.")
                                     .arg(e.what()));
            return nullptr;
        }
    }
    return m_theory.TT.empty() && m_theory.EE.empty() ? nullptr : &m_theory;
}

void
PowerSpectrumDock::refreshTheory()
{
    m_theoryLoaded = false;
    if (m_theoryOn)
        replot(true);
}

// The spectrum the combo selects, from a result
static const std::vector<double> *
spectrumByName(const Spectra &spectra, const QString &name)
{
    return name == "TT"   ? &spectra.TT
           : name == "EE" ? &spectra.EE
           : name == "BB" ? &spectra.BB
           : name == "TE" ? &spectra.TE
           : name == "TB" ? &spectra.TB
                          : &spectra.EB;
}

// The title the tab of a view carries
QString
PowerSpectrumDock::titleOf(const View *view) const
{
    if (m_openMaps)
        for (const OpenMap &m : m_openMaps())
            if (m.view.data() == view)
                return m.title;
    return view ? view->displayName() : QString("This map");
}

// Redraws from the results of the current map (when there is one)
void
PowerSpectrumDock::replot(bool keepRange)
{
    const auto it = m_results.constFind(m_view.data());
    if (it != m_results.constEnd())
        updatePlot(it->spectra, keepRange);
}

// A spectrum as the plot wants it: values at multipoles. Either a C_l for
// every multipole (ell null), or bandpowers at their effective multipoles.
struct Series
{
    const std::vector<double> *values = nullptr;
    const std::vector<double> *ell    = nullptr;

    bool binned() const
    {
        return ell && !ell->empty();
    }
    size_t size() const
    {
        return values ? values->size() : 0;
    }
    double ellAt(size_t i) const
    {
        return binned() ? (*ell)[i] : double(i);
    }
    double lastEll() const
    {
        return size() ? ellAt(size() - 1) : 0.0;
    }
    // The value at multipole l (interpolated between bandpowers); NaN outside
    double at(double l) const
    {
        const double nan = std::nan("");
        if (!values || values->empty())
            return nan;
        if (!binned())
        {
            const long i = std::lround(l);
            return (i >= 0 && size_t(i) < values->size()) ? (*values)[size_t(i)]
                                                          : nan;
        }
        const std::vector<double> &e = *ell;
        if (l < e.front() || l > e.back())
            return nan;
        const size_t j = size_t(std::upper_bound(e.begin(), e.end(), l)
                                - e.begin());
        if (j == 0)
            return (*values)[0];
        if (j >= e.size())
            return values->back();
        const double t = (l - e[j - 1]) / (e[j] - e[j - 1]);
        return (*values)[j - 1] * (1.0 - t) + (*values)[j] * t;
    }
};

// One curve of values for l = 2.. from C_l (or bandpowers), as the plot wants
// them. mode: 0 values, 1 ratio to `ref`, 2 difference from it.
static QVariantMap
makeCurve(const QString &name, const QColor &color, const Series &cur,
          const Series *ref, int mode, int quantity, double maxEll)
{
    QVariantList ell, y;
    for (size_t i = cur.binned() ? 0 : 2; i < cur.size(); ++i)
    {
        const double ll = cur.ellAt(i);
        if (ll > maxEll)
            break;
        const double c = (*cur.values)[i];
        double v;
        if (mode == 1) // ratio to the reference
        {
            const double r = ref->at(ll);
            v              = (std::isfinite(r) && r != 0.0) ? c / r
                                                            : std::nan("");
        }
        else
        {
            const double d = (mode == 2) ? c - ref->at(ll) : c;
            v              = quantity == 0 ? ll * (ll + 1.0) * d / (2.0 * M_PI)
                             : quantity == 1 ? d
                                             : ll * ll * d;
        }
        ell.append(ll);
        y.append(v);
    }
    QVariantMap curve;
    curve["name"]   = name;
    curve["color"]  = color.name();
    curve["ell"]    = ell;
    curve["values"] = y;
    return curve;
}

void
PowerSpectrumDock::updatePlot(const Spectra &spectra, bool keepRange)
{
    QQuickItem *root = m_chartView->rootObject();
    if (!root)
        return;

    const QString name = m_spectrumCombo->currentText();
    const int quantity = m_quantityCombo->currentData().toInt();
    int mode           = m_modeCombo->currentData().toInt();

    const Series current{spectrumByName(spectra, name),
                         spectra.ell.empty() ? nullptr : &spectra.ell};

    // The spectrum of every map / curve to compare, under the same name
    struct Other
    {
        QString name;
        Series data;
    };
    auto seriesOf = [&name](const Spectra &sp)
    {
        return Series{spectrumByName(sp, name),
                      sp.ell.empty() ? nullptr : &sp.ell};
    };
    QList<Other> others;
    for (const OpenMap &m : m_openMaps ? m_openMaps() : QList<OpenMap>())
    {
        if (!m.view || m.view.data() == m_view.data()
            || !m_compareViews.contains(m.view.data()))
            continue;
        const auto r = m_results.constFind(m.view.data());
        if (r == m_results.constEnd())
            continue;
        const Series d = seriesOf(r->spectra);
        if (!d.values->empty())
            others.append({m.title, d});
    }
    for (const External &e : m_external)
        if (e.enabled)
        {
            const Series d = seriesOf(e.spectra);
            if (!d.values->empty())
                others.append({e.name, d});
        }

    // The theory spectrum; for a mask-corrected spectrum optionally through
    // the bandpower windows, so that it is what the bandpowers estimate
    Spectra binnedTheory;
    if (m_theoryOn)
        if (const Spectra *t = theorySpectra())
        {
            const Spectra *use = t;
            QString label      = m_theoryName;
            const auto res     = m_results.constFind(m_view.data());
            if (m_binTheoryCheck->isChecked() && !spectra.ell.empty()
                && res != m_results.constEnd() && res->master)
            {
                auto finite = [](std::vector<double> v)
                {
                    for (double &x : v)
                        if (!std::isfinite(x))
                            x = 0.0;
                    return v;
                };
                master::TheoryCl th;
                th.TT = finite(t->TT);
                th.EE = finite(t->EE);
                th.BB = finite(t->BB);
                th.TE = finite(t->TE);
                const master::Result b = master::binTheory(*res->master, th);
                binnedTheory = fromMaster(b);
                use          = &binnedTheory;
                label += " (binned)";
            }
            const Series d = seriesOf(*use);
            if (!d.values->empty())
                others.append({label, d});
        }

    // Ratio and difference need something to compare with
    if (others.isEmpty())
        mode = 0;

    // Colors: the line color for this map, a color-blind friendly set for
    // the others
    static const QStringList palette
        = {"#e69f00", "#009e73", "#cc79a7", "#d55e00",
           "#f0e442", "#0072b2", "#999999", "#56b4e9"};
    QVariantList curves;
    const QString thisName = titleOf(m_view.data());

    // With "Common l range" every curve stops at the highest multipole all of
    // them have
    double maxEll = std::numeric_limits<double>::infinity();
    if (m_commonCheck->isChecked())
    {
        maxEll = current.lastEll();
        for (const Other &o : others)
            maxEll = std::min(maxEll, o.data.lastEll());
    }

    if (mode == 0)
        curves.append(makeCurve(thisName, m_plotLine, current, nullptr, 0,
                                quantity, maxEll));
    int next = 0;
    for (const Other &o : others)
    {
        QColor c(palette[next++ % palette.size()]);
        if (c == m_plotLine)
            c = QColor(palette[next++ % palette.size()]);
        curves.append(
            makeCurve(o.name, c, o.data, &current, mode, quantity, maxEll));
    }

    const QString symbol = quantity == 0   ? QString("D\u2113")
                           : quantity == 1 ? QString("C\u2113")
                                           : QString("\u2113\u00b2 C\u2113");
    QString title;
    if (mode == 1)
        title = QString("%1 spectrum / %2 (%3)").arg("C\u2113", thisName, name);
    else if (mode == 2)
        title = QString("%1 \u2212 %2 (%3)").arg(symbol, thisName, name);
    else if (quantity == 0)
        title = QString("D\u2113 (%1) = \u2113(\u2113+1) C\u2113 / 2\u03c0")
                    .arg(name);
    else
        title = QString("%1 (%2)").arg(symbol, name);

    QMetaObject::invokeMethod(root, "setScales",
                              Q_ARG(QVariant, m_logXCheck->isChecked()),
                              Q_ARG(QVariant, m_logYCheck->isChecked()));
    QMetaObject::invokeMethod(root, "setCurves", Q_ARG(QVariant, curves),
                              Q_ARG(QVariant, title),
                              Q_ARG(QVariant, keepRange));
}

// The Compare menu: the other open maps, the curves loaded from files, and
// loading one
void
PowerSpectrumDock::buildCompareMenu()
{
    auto *menu = qobject_cast<QMenu *>(m_compareButton->menu());
    menu->clear();

    int shown = 0;
    for (const OpenMap &m : m_openMaps ? m_openMaps() : QList<OpenMap>())
    {
        if (!m.view || m.view.data() == m_view.data())
            continue;
        const View *v   = m.view.data();
        const bool done = m_results.contains(v);
        QAction *a
            = menu->addAction(done ? m.title : m.title + "  (not computed)");
        a->setCheckable(true);
        a->setEnabled(done);
        a->setChecked(done && m_compareViews.contains(v));
        connect(a, &QAction::toggled, this, [this, v](bool on)
        {
            if (on)
                m_compareViews.insert(v);
            else
                m_compareViews.remove(v);
            replot();
        });
        ++shown;
    }
    if (shown == 0)
        menu->addAction("No other map is open")->setEnabled(false);

    if (!m_external.isEmpty())
    {
        menu->addSeparator();
        for (int i = 0; i < m_external.size(); ++i)
        {
            QAction *a = menu->addAction(m_external[i].name);
            a->setCheckable(true);
            a->setChecked(m_external[i].enabled);
            connect(a, &QAction::toggled, this, [this, i](bool on)
            {
                if (i < m_external.size())
                    m_external[i].enabled = on;
                replot();
            });
        }
    }

    // Cross-spectrum of the current map with another one (null tests)
    QMenu *cross = menu->addMenu("&Cross-spectrum (TT) with...");
    int crossCount = 0;
    for (const OpenMap &m : m_openMaps ? m_openMaps() : QList<OpenMap>())
    {
        if (!m.view || m.view.data() == m_view.data())
            continue;
        View *v = m.view.data();
        cross->addAction(m.title, this, [this, v]() { computeCrossSpectrum(v); });
        ++crossCount;
    }
    cross->setEnabled(m_view && crossCount > 0);

    menu->addSeparator();
    QAction *theory = menu->addAction("&Theory (Settings > Theory)");
    theory->setCheckable(true);
    theory->setChecked(m_theoryOn);
    connect(theory, &QAction::toggled, this, [this](bool on)
    {
        m_theoryOn = on;
        QSettings().setValue("powerspectrum/theory", on);
        replot();
    });
    menu->addAction("Load a &curve from a file...", this,
                    [this]() { loadExternalCurve(); });
    QAction *clear = menu->addAction("&Remove loaded curves", this, [this]()
    {
        m_external.clear();
        replot();
    });
    clear->setEnabled(!m_external.isEmpty());
}

// Reads a spectrum from a text file: l in the first column, then one column per
// spectrum. A header names them (TT, EE, BB, TE, TB, EB; "C_l_TT" / "D_l_TT"
// as in the CSV export); without one the second column is TT. Values are C_l or
// D_l = l(l+1)C_l/2pi, asked unless the header says which.
void
PowerSpectrumDock::loadExternalCurve(const QString &givenPath, int unit)
{
    QString path = givenPath;
    if (path.isEmpty())
    {
        const QString dir
            = QSettings().value("general/lastDirectory").toString();
        path = QFileDialog::getOpenFileName(
            this, "Load Spectrum", dir.isEmpty() ? QDir::homePath() : dir,
            "Text files (*.csv *.txt *.dat);;All Files (*)");
        if (path.isEmpty())
            return;
        QSettings().setValue("general/lastDirectory",
                             QFileInfo(path).absolutePath());
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, "Load Spectrum",
                              QString("Cannot read %1").arg(path));
        return;
    }

    static const QRegularExpression separator("[,;\\s]+");
    static const QStringList names = {"TT", "EE", "BB", "TE", "TB", "EB"};

    struct Col
    {
        int spectrum = 0; // index into `names`
        int kind     = 0; // 0 = generic, 1 = C_l, 2 = D_l
        int index    = 0; // column in the file
    };
    QList<Col> cols;
    QList<QList<double>> rows;
    bool haveHeader = false;

    while (!file.atEnd())
    {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty())
            continue;
        const QString body
            = line.startsWith('#') ? line.mid(1).trimmed() : line;
        const QStringList parts = body.split(separator, Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;

        bool numeric = true;
        QList<double> values;
        for (const QString &p : parts)
        {
            bool ok        = false;
            const double v = p.toDouble(&ok);
            numeric &= ok;
            values << v;
        }

        if (!numeric)
        {
            // A header: find the spectra in the column names
            if (haveHeader || !rows.isEmpty())
                continue;
            for (int i = 1; i < parts.size(); ++i)
            {
                // "C_l_TT@other map" is a column of another map in an export
                // that included a comparison: only the first map is read
                if (parts[i].contains('@'))
                    continue;
                const QString t = parts[i].toUpper();
                for (int s = 0; s < names.size(); ++s)
                    if (t.contains(names[s]))
                    {
                        Col c;
                        c.spectrum = s;
                        c.index    = i;
                        c.kind = t.startsWith("D_L") || t.startsWith("DL") ? 2
                                 : (t.startsWith("C_L") || t.startsWith("CL"))
                                     ? 1
                                     : 0;
                        cols << c;
                        break;
                    }
            }
            haveHeader = !cols.isEmpty();
            continue;
        }
        if (!line.startsWith('#'))
            rows << values;
    }

    if (rows.isEmpty())
    {
        QMessageBox::critical(this, "Load Spectrum",
                              "No numeric rows found (expected l in the first "
                              "column).");
        return;
    }
    if (cols.isEmpty())
        cols << Col{0, 0, 1};

    // Our own CSV has both C_l and D_l for a spectrum: use C_l
    for (int i = cols.size() - 1; i >= 0; --i)
        if (cols[i].kind == 2)
            for (const Col &o : cols)
                if (o.spectrum == cols[i].spectrum && o.kind == 1)
                {
                    cols.removeAt(i);
                    break;
                }

    bool needUnit = false;
    for (const Col &c : cols)
        needUnit |= c.kind == 0;
    bool dl = unit == 1;
    if (needUnit && unit < 0)
    {
        bool ok              = false;
        const QString choice = QInputDialog::getItem(
            this, "Load Spectrum", "The values in the file are:",
            {"D\u2113 = \u2113(\u2113+1) C\u2113 / 2\u03c0 (theory spectra "
             "usually)",
             "C\u2113"},
            0, false, &ok);
        if (!ok)
            return;
        dl = choice.startsWith("D");
    }

    External ext;
    ext.name  = QFileInfo(path).completeBaseName();
    long lmax = 0;
    for (const auto &r : rows)
        lmax = std::max(lmax, long(std::lround(r[0])));
    if (lmax < 2)
    {
        QMessageBox::critical(this, "Load Spectrum",
                              "The file has no multipoles above 1.");
        return;
    }

    auto target = [&ext](int spectrum) -> std::vector<double> &
    {
        switch (spectrum)
        {
            case 0:
                return ext.spectra.TT;
            case 1:
                return ext.spectra.EE;
            case 2:
                return ext.spectra.BB;
            case 3:
                return ext.spectra.TE;
            case 4:
                return ext.spectra.TB;
            default:
                return ext.spectra.EB;
        }
    };
    for (const Col &c : cols)
        target(c.spectrum).assign(size_t(lmax) + 1, std::nan(""));

    for (const auto &r : rows)
    {
        const long l = std::lround(r[0]);
        if (l < 0)
            continue;
        for (const Col &c : cols)
        {
            if (c.index >= r.size())
                continue;
            const bool isD = c.kind == 2 || (c.kind == 0 && dl);
            double v       = r[c.index];
            if (isD)
                v = (l >= 1) ? v * 2.0 * M_PI / (double(l) * double(l + 1))
                             : 0.0;
            target(c.spectrum)[size_t(l)] = v;
        }
    }

    m_external.append(ext);
    replot();
}

void
PowerSpectrumDock::computeCrossSpectrum(View *other)
{
    View *self = m_view.data();
    if (!self || !other || !self->glWidget()->healpixMap()
        || !other->glWidget()->healpixMap())
        return;

    // The worker gets its own copies: the tabs may change while it runs. The
    // mask of the current map is applied to both maps.
    auto a = std::make_shared<const Healpix_Map<float>>(
        *self->glWidget()->healpixMap());
    auto b = std::make_shared<const Healpix_Map<float>>(
        *other->glWidget()->healpixMap());
    auto w = std::make_shared<const std::vector<float>>(
        self->glWidget()->maskWeights());
    const int lmax = int(3 * a->Nside() - 1);
    const QString name
        = QString("%1 \u00d7 %2").arg(titleOf(self), titleOf(other));

    m_statusLabel->setText("Computing the cross-spectrum\u2026");
    auto *watcher = new QFutureWatcher<CrossSpectrumResult>(this);
    connect(watcher, &QFutureWatcher<CrossSpectrumResult>::finished, this,
            [this, watcher, name]()
    {
        CrossSpectrumResult r = watcher->result();
        watcher->deleteLater();
        if (!r.error.isEmpty())
        {
            QMessageBox::critical(this, "Cross-spectrum", r.error);
            refresh();
            return;
        }
        External ext;
        ext.name       = name;
        ext.spectra.TT = std::move(r.cl);
        m_external.append(ext);
        buildCompareMenu();
        refresh();
        replot();
        QString note = QString("Cross-spectrum added as \"%1\" (divided by "
                               "<w\u00b2> = %2).")
                           .arg(name)
                           .arg(r.fsky, 0, 'g', 3);
        if (r.unseen > 0)
            note += QString(" %1 pixels unseen in either map were left out.")
                        .arg(r.unseen);
        m_statusLabel->setText(note);
    });
    watcher->setFuture(QtConcurrent::run([a, b, w, lmax]()
    {
        return crossSpectrum(*a, *b, *w, lmax, 3);
    }));
}

static QString
exportStartDir()
{
    const QString dir = QSettings().value("general/lastDirectory").toString();
    return (!dir.isEmpty() && QDir(dir).exists()) ? dir : QDir::homePath();
}

// The plot as the QML shows it now: curves, visible range, scales, y title
static bool
currentPlot(QQuickItem *root, plotrenderer::Plot &plot)
{
    if (!root)
        return false;
    QVariant curves = root->property("curves");
    if (curves.canConvert<QJSValue>())
        curves = curves.value<QJSValue>().toVariant();
    for (const QVariant &cv : curves.toList())
    {
        const QVariantMap m = cv.toMap();
        plotrenderer::Curve c;
        c.name  = m.value("name").toString();
        c.color = QColor(m.value("color").toString());
        for (const QVariant &v : m.value("ell").toList())
            c.ell.append(v.toDouble());
        for (const QVariant &v : m.value("values").toList())
            c.values.append(v.toDouble());
        plot.curves.append(c);
    }
    plot.xMin   = root->property("xMin").toDouble();
    plot.xMax   = root->property("xMax").toDouble();
    plot.yMin   = root->property("yMin").toDouble();
    plot.yMax   = root->property("yMax").toDouble();
    plot.logX   = root->property("logX").toBool();
    plot.logY   = root->property("logY").toBool();
    plot.yTitle = root->property("yTitle").toString();
    plot.xTitle = QString::fromUtf8("Multipole \u2113");
    return !plot.curves.isEmpty();
}

void
PowerSpectrumDock::exportImage()
{
    QString filter;
    QString path = QFileDialog::getSaveFileName(
        this, "Export Plot", exportStartDir() + "/power_spectrum.png",
        "PNG Files (*.png);;SVG Files (*.svg);;PDF Files (*.pdf);;All Files "
        "(*)",
        &filter);
    if (path.isEmpty())
        return;
    QSettings().setValue("general/lastDirectory",
                         QFileInfo(path).absolutePath());

    // The chosen filter decides the format when the name has no extension
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty())
    {
        ext = filter.startsWith("SVG") ? "svg"
              : filter.startsWith("PDF") ? "pdf"
                                         : "png";
        path += "." + ext;
    }

    if (ext == "svg" || ext == "pdf")
    {
        plotrenderer::Plot plot;
        if (!currentPlot(m_chartView->rootObject(), plot))
        {
            QMessageBox::information(this, "Export Plot",
                                     "There is no spectrum to export.");
            return;
        }
        plot.screenColors = AppSettings::load().exportPlotColors;
        plot.background   = m_plotBackground;
        plot.foreground   = m_plotForeground;
        QString error;
        if (!plotrenderer::writeVector(path, plot, m_chartView->size(), &error))
            QMessageBox::critical(this, "Export Plot", error);
        return;
    }

    const QImage image = m_chartView->grabWindow();
    if (image.isNull() || !image.save(path))
        QMessageBox::critical(this, "Export Plot",
                              QString("Failed to save %1").arg(path));
}

// Every computed spectrum of this map, then those of the maps and curves
// compared with it (as shown in the plot): C_l and D_l = l(l+1) C_l / 2pi for
// l = 0..lmax. Columns of the others end in "@name"; where a curve has no
// value (it ends earlier) the field is empty.
void
PowerSpectrumDock::exportCsv(const QString &givenPath)
{
    const auto it = m_results.constFind(m_view.data());
    if (it == m_results.constEnd())
    {
        QMessageBox::information(this, "Export Spectra",
                                 "Compute a power spectrum first.");
        return;
    }

    QString path = givenPath;
    if (path.isEmpty())
    {
        path = QFileDialog::getSaveFileName(
            this, "Export Spectra", exportStartDir() + "/power_spectrum.csv",
            "CSV Files (*.csv);;All Files (*)");
        if (path.isEmpty())
            return;
        QSettings().setValue("general/lastDirectory",
                             QFileInfo(path).absolutePath());
    }

    // Bandpowers of a mask-corrected spectrum: one row per band, at its
    // effective multipole. Comparison spectra have other multipoles, so they
    // are not included.
    if (!it->spectra.ell.empty())
    {
        QFile bandFile(path);
        if (!bandFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QMessageBox::critical(this, "Export Spectra",
                                  QString("Cannot write %1").arg(path));
            return;
        }
        QTextStream out(&bandFile);
        const Spectra &sp = it->spectra;
        out << "# bandpowers corrected for the mask (MASTER), lmax = "
            << it->lmax << ", nside = " << it->nside << ", bandwidth = "
            << it->correction.bandwidth << ", f_sky = " << it->fsky
            << ", apodization = " << it->correction.apodization << " ("
            << it->correction.apodizationScale << " deg)"
            << ", purified E = "
            << (it->master && it->master->purifyE ? 1 : 0) << ", B = "
            << (it->master && it->master->purifyB ? 1 : 0) << "\n"
            << "# comparison spectra are not included\n";
        struct Col
        {
            const char *name;
            const std::vector<double> *data;
        };
        QList<Col> cols;
        for (const Col &c :
             {Col{"TT", &sp.TT}, Col{"EE", &sp.EE}, Col{"BB", &sp.BB},
              Col{"TE", &sp.TE}, Col{"TB", &sp.TB}, Col{"EB", &sp.EB}})
            if (!c.data->empty())
                cols << c;
        out << "ell_eff";
        for (const Col &c : cols)
            out << ",C_b_" << c.name;
        for (const Col &c : cols)
            out << ",D_b_" << c.name;
        out << "\n";
        out.setRealNumberPrecision(10);
        for (size_t b = 0; b < sp.ell.size(); ++b)
        {
            const double l = sp.ell[b];
            out << l;
            for (const Col &c : cols)
                out << ',' << (*c.data)[b];
            for (const Col &c : cols)
                out << ',' << (*c.data)[b] * l * (l + 1.0) / (2.0 * M_PI);
            out << "\n";
        }
        return;
    }

    struct Source
    {
        QString suffix; // "" for this map, "@name" for the others
        QString label;
        const Spectra *spectra;
    };
    auto clean = [](QString name)
    {
        name.replace(QRegularExpression("[,;\\s]+"), "_");
        return name;
    };
    QList<Source> sources;
    sources.append({QString(), titleOf(m_view.data()), &it->spectra});
    for (const OpenMap &m : m_openMaps ? m_openMaps() : QList<OpenMap>())
    {
        if (!m.view || m.view.data() == m_view.data()
            || !m_compareViews.contains(m.view.data()))
            continue;
        const auto r = m_results.constFind(m.view.data());
        if (r != m_results.constEnd())
            sources.append({"@" + clean(m.title), m.title, &r->spectra});
    }
    for (const External &e : m_external)
        if (e.enabled)
            sources.append({"@" + clean(e.name), e.name, &e.spectra});

    struct Column
    {
        QString name;
        const std::vector<double> *data;
    };
    QList<Column> columns;
    size_t n = 0;
    for (const Source &src : sources)
    {
        const Spectra &sp = *src.spectra;
        for (const Column &c :
             {Column{"TT", &sp.TT}, Column{"EE", &sp.EE}, Column{"BB", &sp.BB},
              Column{"TE", &sp.TE}, Column{"TB", &sp.TB}, Column{"EB", &sp.EB}})
            if (!c.data->empty())
            {
                columns << Column{c.name + src.suffix, c.data};
                n = std::max(n, c.data->size());
            }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, "Export Spectra",
                              QString("Cannot write %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out << "# lmax = " << it->lmax << ", nside = " << it->nside
        << ", refinement rounds = " << it->iterations << "\n";
    for (int i = 1; i < sources.size(); ++i)
        out << "# " << sources[i].suffix << " = " << sources[i].label << "\n";
    out << "l";
    for (const Column &c : columns)
        out << ",C_l_" << c.name;
    for (const Column &c : columns)
        out << ",D_l_" << c.name;
    out << "\n";

    out.setRealNumberPrecision(10);
    auto field = [&out](const std::vector<double> &d, size_t l, double factor)
    {
        out << ',';
        if (l < d.size() && std::isfinite(d[l]))
            out << factor * d[l];
    };
    for (size_t l = 0; l < n; ++l)
    {
        out << l;
        for (const Column &c : columns)
            field(*c.data, l, 1.0);
        const double f = double(l) * double(l + 1) / (2.0 * M_PI);
        for (const Column &c : columns)
            field(*c.data, l, f);
        out << "\n";
    }
}
