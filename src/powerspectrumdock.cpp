#include "powerspectrumdock.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "alm_powspec_tools.h"
#include "fitsinfo.hpp"
#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "powspec.h"
#include "view.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineSeries>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QElapsedTimer>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QPointer>
#include <QVBoxLayout>
#include <QValueAxis>
#include <QVariantList>
#include <QtConcurrent>
#include <atomic>
#include <cmath>
#include <memory>

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
    for (long i = 0; i < map.Npix(); ++i)
    {
        const bool good = std::isfinite(map[i]) && map[i] > -1e30f;
        unseen |= !good;
        m[i] = good ? map[i] : 0.0;
    }
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
    static constexpr int kIterations = 3;
    static constexpr int kSteps      = 1 + 1 + 2 * kIterations;

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
    const int iter = PowerSpectrumJob::kIterations;

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
};

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
    m_progressBar->setRange(0, PowerSpectrumJob::kSteps);
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
            [this](int)
    {
        const auto it = m_results.constFind(m_view.data());
        if (it != m_results.constEnd())
            updatePlot(it->spectra);
    });

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

    auto *top = new QHBoxLayout();
    top->addWidget(m_spectrumCombo, 1);
    top->addStretch();
    top->addWidget(m_computeButton);

    layout->addLayout(top);
    layout->addWidget(m_warningLabel);
    layout->addWidget(m_progressBar);
    layout->addWidget(m_progressLabel);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_chartContainer, 1);
    setWidget(content);
}

void
PowerSpectrumDock::setView(View *view)
{
    m_view = view;
    refresh();
}

void
PowerSpectrumDock::forget(const View *view)
{
    m_results.remove(view);
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

    m_computeButton->setEnabled(have);
    m_computeButton->setText(busy ? "Cancel" : done ? "Recompute" : "Compute");

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
    if (it->maskActive)
        notes << "\u26a0 A mask was applied to this map. The spectra are "
                 "computed from the unmasked map and are not corrected for "
                 "the mask, so they may be wrong.";
    if (it->unseen)
        notes << "\u26a0 The map has unseen pixels, which are set to zero. "
                 "The spectra are not corrected for them and may be wrong.";

    bool masksNow = false;
    for (int i = 0; i < view->glWidget()->maskCount(); ++i)
        masksNow |= view->glWidget()->maskEnabled(i);
    if (it->hdu != view->hdu() || it->column != view->column()
        || it->maskActive != masksNow)
        notes << "The map's HDU, column or masks changed since this was "
                 "computed. Press Recompute to update it.";

    m_warningLabel->setText(notes.join("\n"));
    m_warningLabel->setVisible(!notes.isEmpty());

    // Only offer the spectra that were actually computed
    const Spectra &sp = it->spectra;
    struct Entry
    {
        const char *name;
        const std::vector<double> *data;
    };
    const Entry entries[]
        = {{"TT", &sp.TT}, {"EE", &sp.EE}, {"BB", &sp.BB},
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
    const int column = view->column();

    auto job = std::make_shared<PowerSpectrumJob>();
    job->clock.start();
    m_jobs.insert(view, job);
    m_busy.insert(view);
    refresh();

    QPointer<View> guard(view);
    auto *watcher = new QFutureWatcher<CalcResult>(this);
    connect(watcher, &QFutureWatcher<CalcResult>::finished, this,
            [this, watcher, guard, input, column]()
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
                m_statusLabel->setText("Failed to compute the power "
                                       "spectrum.");
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
        m_results.insert(guard.data(), res);
        if (m_view.data() == guard.data())
            refresh();
    });

    // The worker owns copies / shared data, so it stays valid even if the
    // tab is closed before it finishes
    watcher->setFuture(QtConcurrent::run([input, job]() -> CalcResult
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
            const int lmax = st.T.Nside() * 3 - 1;
            CalcResult r;
            r.spectra = fromPowSpec(computeCl(st, lmax, *job));
            r.unseen  = st.unseen;
            return r;
        }
        catch (const PowerSpectrumJob::Cancelled &)
        {
            CalcResult r;
            r.cancelled = true;
            return r;
        }
        catch (const std::exception &)
        {
            return {};
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

    m_progressBar->setValue(job.step);
    m_progressLabel->setText(
        job.cancelled
            ? QString("Cancelling after the current step\u2026 (%1)")
                  .arg(elapsed)
            : QString("%1 \u2014 %2 elapsed").arg(job.stage.load()).arg(elapsed));
}

void
PowerSpectrumDock::updatePlot(const Spectra &spectra)
{
    QQuickItem *root = m_chartView->rootObject();
    if (!root)
        return;

    const QString name              = m_spectrumCombo->currentText();
    const std::vector<double> *data = name == "TT"   ? &spectra.TT
                                      : name == "EE" ? &spectra.EE
                                      : name == "BB" ? &spectra.BB
                                      : name == "TE" ? &spectra.TE
                                      : name == "TB" ? &spectra.TB
                                                     : &spectra.EB;

    // D_l = l(l+1) C_l / 2pi, as flat lists QML can read (l = 2 onwards)
    QVariantList ell, dl;
    for (size_t l = 2; l < data->size(); ++l)
    {
        ell.append(double(l));
        dl.append(double(l) * double(l + 1) * (*data)[l] / (2.0 * M_PI));
    }

    QMetaObject::invokeMethod(
        root, "setData", Q_ARG(QVariant, ell), Q_ARG(QVariant, dl),
        Q_ARG(QVariant, QString("D_l (%1) = l(l+1)C_l / 2\u03c0").arg(name)));
}
