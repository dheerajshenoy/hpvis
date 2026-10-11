#include "powerspectrumdialog.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "alm_powspec_tools.h"
#include "powspec.h"

#include <QLabel>
#include <QLineSeries>
#include <QPushButton>
#include <QQmlContext>
#include <QQuickView>
#include <QVBoxLayout>
#include <QValueAxis>
#include <QVariantList>
#include <cmath>

static PowSpec
computeCl(const Healpix_Map<float> &map, int lmax, int iter = 3)
{
    // Work in double, and treat UNSEEN / NaN pixels as zero
    Healpix_Map<double> m(map.Nside(), map.Scheme(), SET_NSIDE);
    for (long i = 0; i < map.Npix(); ++i)
        m[i] = (std::isfinite(map[i]) && map[i] > -1e30f) ? map[i] : 0.0;

    // map2alm expects RING ordering, so swap if necessary
    if (m.Scheme() == NEST)
        m.swap_scheme();

    Alm<xcomplex<double>> alm(lmax, lmax);
    arr<double> weights(2 * m.Nside());
    weights.fill(1.0);                   // or load HEALPix ring weights
    map2alm_iter(m, alm, iter, weights); // iter = 0 is the plain transform

    PowSpec ps(1, lmax);
    extract_powspec(alm, ps); // C_l = mean over m of |a_lm|^2
    return ps;                // ps.tt(l)
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

PowerSpectrumDialog::PowerSpectrumDialog(const Healpix_Map<float> *map,
                                         QWidget *parent)
    : QDialog(parent)
{
    (void)map;
    initUI();
    computePowerSpectrum(map);
}

void
PowerSpectrumDialog::initUI()
{
    setWindowTitle("Power Spectrum");
    setModal(true);
    resize(400, 300);

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_chartView = new QQuickView();
    m_chartView->setResizeMode(QQuickView::SizeRootObjectToView);

    QPushButton *closeButton = new QPushButton("Close", this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    layout->addWidget(QWidget::createWindowContainer(m_chartView));
    layout->addWidget(closeButton);
}

void
PowerSpectrumDialog::computePowerSpectrum(const Healpix_Map<float> *map)
{
    PowSpec cls     = computeCl(*map, map->Nside() * 3 - 1);
    Spectra spectra = fromPowSpec(cls);

    // D_l = l(l+1) C_l / 2pi, as flat lists QML can read (l = 2 onwards)
    QVariantList ell, dl;
    for (size_t l = 2; l < spectra.TT.size(); ++l)
    {
        ell.append(double(l));
        dl.append(double(l) * double(l + 1) * spectra.TT[l] / (2.0 * M_PI));
    }

    // Context properties must exist before the QML is loaded
    m_chartView->rootContext()->setContextProperty("plotEll", ell);
    m_chartView->rootContext()->setContextProperty("plotDl", dl);
    m_chartView->setSource(QUrl("qrc:/src/qml/powerspectrumplot.qml"));
}
