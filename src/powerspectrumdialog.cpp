#include "powerspectrumdialog.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "alm_powspec_tools.h"
#include "fitsinfo.hpp"
#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "powspec.h"

#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineSeries>
#include <QPushButton>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QVBoxLayout>
#include <QValueAxis>
#include <QVariantList>
#include <QtConcurrent>
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

static PowSpec
computeCl(const Stokes &s, int lmax, int iter = 3)
{
    const int nside = s.T.Nside();
    arr<double> weights(2 * nside);
    weights.fill(1.0); // or load HEALPix ring weights

    Alm<xcomplex<double>> aT(lmax, lmax);
    if (!s.polarized)
    {
        map2alm_iter(s.T, aT, iter, weights); // iter = 0 is the plain transform
        PowSpec ps(1, lmax);
        extract_powspec(aT, ps); // C_l = mean over m of |a_lm|^2
        return ps;
    }

    Alm<xcomplex<double>> aE(lmax, lmax), aB(lmax, lmax);
    map2alm_pol_iter(s.T, s.Q, s.U, aT, aE, aB, iter, weights);
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

PowerSpectrumDialog::PowerSpectrumDialog(const PowerSpectrumInput &input,
                                         QWidget *parent)
    : QDialog(parent)
{
    initUI();
    if (input.maskActive)
        m_warningLabel->setText(
            "\u26a0 A mask is applied to this map. The spectra below are "
            "computed from the unmasked map and are not corrected for the "
            "mask, so they may be wrong.");
    m_warningLabel->setVisible(input.maskActive);
    computePowerSpectrum(input);
}

void
PowerSpectrumDialog::initUI()
{
    setWindowTitle("Power Spectrum");
    setModal(true);
    resize(800, 520);

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_chartView = new QQuickView();
    m_chartView->setColor(QColor("#121217"));
    m_chartView->setResizeMode(QQuickView::SizeRootObjectToView);

    QPushButton *closeButton = new QPushButton("Close", this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    m_statusLabel = new QLabel("Computing power spectrum\u2026", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    m_spectrumCombo = new QComboBox(this);
    m_spectrumCombo->setVisible(false); // filled once the spectra are known
    connect(m_spectrumCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { updatePlot(); });

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(
        "background: #5a4300; color: #ffe9a8; padding: 6px; "
        "border-radius: 4px;");
    m_warningLabel->setVisible(false);

    layout->addWidget(m_warningLabel);
    layout->addWidget(m_spectrumCombo);
    layout->addWidget(m_statusLabel);
    layout->addWidget(QWidget::createWindowContainer(m_chartView), 1);
    layout->addWidget(closeButton);
}

struct CalcResult
{
    Spectra spectra;
    bool unseen = false;
};

void
PowerSpectrumDialog::computePowerSpectrum(const PowerSpectrumInput &input)
{
    if (!input.map)
    {
        m_statusLabel->setText("No map loaded.");
        return;
    }

    auto *watcher = new QFutureWatcher<CalcResult>(this);
    connect(watcher, &QFutureWatcher<CalcResult>::finished, this,
            [this, watcher]()
    {
        const CalcResult r = watcher->result();
        watcher->deleteLater();
        if (r.spectra.TT.empty())
        {
            m_statusLabel->setText("Failed to compute the power spectrum.");
            return;
        }
        if (r.unseen)
        {
            // Zeroed pixels act as a mask too, whether or not one is loaded
            QString text = m_warningLabel->text();
            if (!text.isEmpty())
                text += "\n";
            m_warningLabel->setText(
                text
                + "\u26a0 The map has unseen pixels, which are set to "
                  "zero. The spectra are not corrected for them and may "
                  "be wrong.");
            m_warningLabel->show();
        }
        showSpectra(r.spectra);
    });

    // The worker owns copies / shared data, so it stays valid even if the
    // dialog is closed before it finishes
    watcher->setFuture(QtConcurrent::run([input]() -> CalcResult
    {
        try
        {
            Stokes st;
            if (!readStokes(input, st))
            {
                st.polarized = false;
                st.T         = toDouble(*input.map, st.unseen);
            }
            const int lmax = st.T.Nside() * 3 - 1;
            CalcResult r;
            r.spectra = fromPowSpec(computeCl(st, lmax));
            r.unseen  = st.unseen;
            return r;
        }
        catch (const std::exception &)
        {
            return {};
        }
    }));
}

void
PowerSpectrumDialog::showSpectra(const Spectra &spectra)
{
    m_statusLabel->hide();
    m_spectra = spectra;

    // Only offer the spectra that were actually computed
    struct Entry
    {
        const char *name;
        const std::vector<double> *data;
    };
    const Entry entries[]
        = {{"TT", &m_spectra.TT}, {"EE", &m_spectra.EE}, {"BB", &m_spectra.BB},
           {"TE", &m_spectra.TE}, {"TB", &m_spectra.TB}, {"EB", &m_spectra.EB}};

    QSignalBlocker block(m_spectrumCombo);
    m_spectrumCombo->clear();
    for (const Entry &e : entries)
        if (!e.data->empty())
            m_spectrumCombo->addItem(e.name);
    m_spectrumCombo->setVisible(m_spectrumCombo->count() > 1);

    m_chartView->setSource(QUrl("qrc:/src/qml/powerspectrumplot.qml"));
    updatePlot();
}

void
PowerSpectrumDialog::updatePlot()
{
    QQuickItem *root = m_chartView->rootObject();
    if (!root)
        return;

    const QString name              = m_spectrumCombo->currentText();
    const std::vector<double> *data = name == "TT"   ? &m_spectra.TT
                                      : name == "EE" ? &m_spectra.EE
                                      : name == "BB" ? &m_spectra.BB
                                      : name == "TE" ? &m_spectra.TE
                                      : name == "TB" ? &m_spectra.TB
                                                     : &m_spectra.EB;

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
