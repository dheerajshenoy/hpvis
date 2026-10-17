#include "randommap.hpp"
#include "pixelwindow.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "alm_powspec_tools.h"
#include "healpix_cxx/error_handling.h"
#include "planck_rng.h"
#include "powspec.h"
#include "unseen.hpp"

#include <QList>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <random>

namespace
{
// Pixels per independently seeded block: fixed, so that the map does not
// depend on how many threads there are
constexpr long kBlock = 1L << 16;

// Seed of block `i`: mix the seed and the index (splitmix64)
std::uint64_t
blockSeed(unsigned seed, long i)
{
    std::uint64_t z = (std::uint64_t(seed) << 32) + std::uint64_t(i)
                      + 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::shared_ptr<Healpix_Map<float>>
noiseMap(const RandomOptions &o)
{
    auto map = std::make_shared<Healpix_Map<float>>(int(o.nside), RING,
                                                    SET_NSIDE);
    const long npix = map->Npix();
    QList<long> blocks;
    for (long b = 0; b * kBlock < npix; ++b)
        blocks.append(b);

    const bool gaussian = o.kind == RandomKind::GaussianNoise;
    float *data         = &(*map)[0];
    QtConcurrent::blockingMap(blocks, [=](long &b)
    {
        std::mt19937_64 gen(blockSeed(o.seed, b));
        const long begin = b * kBlock, end = std::min(npix, begin + kBlock);
        if (gaussian)
        {
            std::normal_distribution<double> dist(o.a, o.b);
            for (long i = begin; i < end; ++i)
                data[i] = float(dist(gen));
        }
        else
        {
            std::uniform_real_distribution<double> dist(o.a, o.b);
            for (long i = begin; i < end; ++i)
                data[i] = float(dist(gen));
        }
    });
    return map;
}

std::shared_ptr<Healpix_Map<float>>
newMap(long nside)
{
    return std::make_shared<Healpix_Map<float>>(int(nside), RING, SET_NSIDE);
}

double
finiteOrZero(double v)
{
    return std::isfinite(v) ? v : 0.0;
}

void
cmbMaps(const RandomOptions &o, RandomResult &r)
{
    const Spectra spectra = loadTheorySpectra(o.theory);
    const bool pol        = o.kind == RandomKind::CmbPolarization;
    if (spectra.TT.size() < 3)
        throw PlanckError("the theory spectrum has no TT column up to l = 2");
    if (pol && spectra.EE.size() < 3)
        throw PlanckError("the theory spectrum has no EE column");

    const int lmax = int(std::min<long>(3 * o.nside - 1,
                                        long(spectra.TT.size()) - 1));
    r.lmax         = lmax;
    const double fwhm = o.fwhmArcmin / 60.0 * M_PI / 180.0;
    planck_rng rng(int(o.seed & 0x7fffffff));

    auto at = [](const std::vector<double> &v, int l)
    { return l < int(v.size()) ? finiteOrZero(v[size_t(l)]) : 0.0; };

    if (!pol)
    {
        PowSpec ps(1, lmax);
        for (int l = 0; l <= lmax; ++l)
            ps.tt(l) = std::max(0.0, at(spectra.TT, l));
        Alm<xcomplex<float>> alm(lmax, lmax);
        create_alm(ps, alm, rng);
        if (o.pixelWindow)
            alm.ScaleL(pixwin(int(o.nside), lmax).T);
        if (fwhm > 0.0)
            smoothWithGauss(alm, fwhm);
        r.map = newMap(o.nside);
        alm2map(alm, *r.map);
        return;
    }

    // TT, GG (= EE), CC (= BB) and TG (= TE)
    PowSpec ps(4, lmax);
    for (int l = 0; l <= lmax; ++l)
    {
        ps.tt(l) = std::max(0.0, at(spectra.TT, l));
        ps.gg(l) = std::max(0.0, at(spectra.EE, l));
        ps.cc(l) = std::max(0.0, at(spectra.BB, l));
        ps.tg(l) = at(spectra.TE, l);
        // A correlation cannot exceed sqrt(TT * EE)
        const double limit = std::sqrt(ps.tt(l) * ps.gg(l));
        ps.tg(l) = std::clamp(ps.tg(l), -limit, limit);
    }
    Alm<xcomplex<float>> aT(lmax, lmax), aE(lmax, lmax), aB(lmax, lmax);
    create_alm_pol(ps, aT, aE, aB, rng);
    if (o.pixelWindow)
    {
        const PixelWindow w = pixwin(int(o.nside), lmax);
        aT.ScaleL(w.T); // the polarization window is not the temperature one
        aE.ScaleL(w.P);
        aB.ScaleL(w.P);
    }
    if (fwhm > 0.0)
        smoothWithGauss(aT, aE, aB, fwhm);
    r.map  = newMap(o.nside);
    r.mapQ = newMap(o.nside);
    r.mapU = newMap(o.nside);
    alm2map_pol(aT, aE, aB, *r.map, *r.mapQ, *r.mapU);
}
} // namespace

RandomResult
generateRandomMap(const RandomOptions &options)
{
    RandomResult r;
    try
    {
        if (options.nside < 1 || (options.nside & (options.nside - 1)) != 0
            || options.nside > 8192)
            throw PlanckError("nside must be a power of two up to 8192");
        switch (options.kind)
        {
            case RandomKind::GaussianNoise:
                if (!(options.b >= 0.0))
                    throw PlanckError("sigma cannot be negative");
                r.map = noiseMap(options);
                break;
            case RandomKind::UniformNoise:
                if (!(options.b > options.a))
                    throw PlanckError("the maximum must be above the minimum");
                r.map = noiseMap(options);
                break;
            case RandomKind::CmbTemperature:
            case RandomKind::CmbPolarization:
                cmbMaps(options, r);
                break;
        }
    }
    catch (const PlanckError &e) // not derived from std::exception
    {
        r = RandomResult();
        r.error = QString::fromStdString(e.what());
    }
    catch (const std::exception &e)
    {
        r = RandomResult();
        r.error = QString::fromUtf8(e.what());
    }
    return r;
}
