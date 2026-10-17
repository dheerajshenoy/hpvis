#include "multipolefilter.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "healpix_cxx/error_handling.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
// 0 well below `x` = 0, 1 well above it, 0.5 at it
double
edge(double x, FilterTaper taper, int width)
{
    if (taper == FilterTaper::Sharp || width <= 0)
        return x >= 0.0 ? 1.0 : 0.0;
    if (taper == FilterTaper::Cosine)
    {
        if (x <= -0.5 * width)
            return 0.0;
        if (x >= 0.5 * width)
            return 1.0;
        return 0.5 * (1.0 + std::sin(std::numbers::pi * x / width));
    }
    const double sigma = 0.5 * width;
    return 0.5 * (1.0 + std::erf(x / (sigma * std::numbers::sqrt2)));
}
} // namespace

std::vector<double>
filterWindow(const FilterOptions &o, int analysisLmax)
{
    std::vector<double> w(size_t(std::max(0, analysisLmax)) + 1, 1.0);
    for (int l = 0; l < int(w.size()); ++l)
    {
        // The edges sit between l - 1 and l, so a sharp cut keeps exactly
        // lmin <= l <= lmax
        const double rise = edge(l - o.lmin, o.taper, o.taperWidth);
        const double fall = 1.0 - edge(l - (o.lmax + 1), o.taper, o.taperWidth);
        switch (o.type)
        {
        case FilterType::LowPass: w[l] = fall; break;
        case FilterType::HighPass: w[l] = rise; break;
        case FilterType::BandPass: w[l] = rise * fall; break;
        case FilterType::BandStop: w[l] = 1.0 - rise * fall; break;
        }
    }
    return w;
}

FilterResult
filterMap(const Healpix_Map<float> &in, const FilterOptions &o)
{
    FilterResult r;
    try
    {
        const int nside = in.Nside();
        const int lmax  = o.analysisLmax > 0
                              ? std::min(o.analysisLmax, 3 * nside - 1)
                              : 3 * nside - 1;
        if (lmax < 1)
            throw PlanckError("l max of the analysis must be at least 1");
        if (o.type != FilterType::LowPass && o.type != FilterType::HighPass
            && o.lmin > o.lmax)
            throw PlanckError("l min is above l max");

        Healpix_Map<float> maps[3];
        const Healpix_Map<float> *src[3] = {&in, nullptr, nullptr};
        if (o.polarization)
        {
            readStokesMaps(in, o.path, o.hdu, maps);
            for (int k = 0; k < 3; ++k)
                src[k] = &maps[k];
        }

        const std::vector<double> window = filterWindow(o, lmax);
        long unseen = 0;
        arr<double> weights(2 * nside);
        weights.fill(1.0);
        Alm<xcomplex<double>> aT(lmax, lmax), aE(lmax, lmax), aB(lmax, lmax);

        if (o.polarization)
        {
            Healpix_Map<double> mT = toDouble(*src[0], unseen),
                                mQ = toDouble(*src[1], unseen),
                                mU = toDouble(*src[2], unseen);
            map2alm_pol_iter(mT, mQ, mU, aT, aE, aB,
                             std::max(0, o.iterations), weights);
            aT.ScaleL(window);
            aE.ScaleL(window);
            aB.ScaleL(window);
            alm2map_pol(aT, aE, aB, mT, mQ, mU);
            r.map  = toFloat(mT, *src[0], o.keepUnseen && unseen > 0);
            r.mapQ = toFloat(mQ, *src[1], o.keepUnseen && unseen > 0);
            r.mapU = toFloat(mU, *src[2], o.keepUnseen && unseen > 0);
        }
        else
        {
            Healpix_Map<double> m = toDouble(in, unseen);
            map2alm_iter(m, aT, std::max(0, o.iterations), weights);
            aT.ScaleL(window);
            alm2map(aT, m);
            r.map = toFloat(m, in, o.keepUnseen && unseen > 0);
        }

        r.notes << QString("Analysed up to l = %1 (%2 iterations)%3.")
                       .arg(lmax)
                       .arg(o.iterations)
                       .arg(o.polarization ? ", T, Q and U" : "");
        if (unseen > 0)
            r.notes << QString("%1 UNSEEN / NaN pixels were taken as zero in "
                               "the transform; on a cut sky this leaks power "
                               "across the edge of the mask.")
                           .arg(unseen);
    }
    catch (const PlanckError &e) // not derived from std::exception
    {
        r       = FilterResult();
        r.error = e.what();
    }
    catch (const std::exception &e)
    {
        r       = FilterResult();
        r.error = e.what();
    }
    return r;
}
