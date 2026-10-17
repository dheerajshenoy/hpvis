#include "crossspectrum.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "healpix_cxx/error_handling.h"
#include "parallel.hpp"
#include "unseen.hpp"

#include <algorithm>
#include <cmath>

namespace
{
// weights * map as doubles, in RING order; pixels with weight 0 are zero
Healpix_Map<double>
weighted(const Healpix_Map<float> &map, const std::vector<double> &w)
{
    Healpix_Map<double> m(map.Nside(), map.Scheme(), SET_NSIDE);
    parallel::forRange(map.Npix(), [&](long begin, long end)
    {
        for (long i = begin; i < end; ++i)
            m[i] = w[size_t(i)] > 0.0 ? w[size_t(i)] * double(map[i]) : 0.0;
    });
    if (m.Scheme() == NEST)
        m.swap_scheme(); // map2alm expects RING ordering
    return m;
}
} // namespace

CrossSpectrumResult
crossSpectrum(const Healpix_Map<float> &a, const Healpix_Map<float> &b,
              const std::vector<float> &weights, int lmax, int iterations)
{
    CrossSpectrumResult r;
    try
    {
        if (a.Nside() != b.Nside())
            throw PlanckError("the maps have different nside (use Tools > "
                              "Change Resolution first)");

        // Put the second map in the ordering of the first
        Healpix_Map<float> bb = b;
        if (bb.Scheme() != a.Scheme())
            bb.swap_scheme();

        const long npix = a.Npix();
        if (!weights.empty() && weights.size() != size_t(npix))
            throw PlanckError("the mask does not match the map");

        // One weight per pixel: the mask, and zero where either map is unseen
        std::vector<double> w(static_cast<size_t>(npix), 0.0);
        const long bad = parallel::reduceRange<long>(
            npix,
            [&](long begin, long end)
        {
            long n = 0;
            for (long i = begin; i < end; ++i)
            {
                const bool good = isSeen(a[i]) && isSeen(bb[i]);
                n += !good;
                w[size_t(i)] = good ? (weights.empty() ? 1.0
                                                       : double(weights[size_t(i)]))
                                    : 0.0;
            }
            return n;
        }, [](long &total, const long &part) { total += part; });
        r.unseen = bad;

        const double w2sum = parallel::reduceRange<double>(
            npix,
            [&](long begin, long end)
        {
            double s = 0.0;
            for (long i = begin; i < end; ++i)
                s += w[size_t(i)] * w[size_t(i)];
            return s;
        }, [](double &total, const double &part) { total += part; });
        r.fsky = w2sum / double(npix);
        if (r.fsky <= 0.0)
            throw PlanckError("the mask removes the whole sky");

        const int nside = a.Nside();
        lmax            = std::clamp(lmax, 2, 3 * nside - 1);
        arr<double> ringWeights(2 * nside);
        ringWeights.fill(1.0);
        const int iter = std::max(0, iterations);

        Alm<xcomplex<double>> aA(lmax, lmax), aB(lmax, lmax);
        {
            Healpix_Map<double> m = weighted(a, w);
            map2alm_iter(m, aA, iter, ringWeights);
        }
        {
            Healpix_Map<double> m = weighted(bb, w);
            map2alm_iter(m, aB, iter, ringWeights);
        }

        // C_l = (1 / (2l + 1)) sum over m of Re(a_lm b*_lm); only m >= 0 is
        // stored, and the m > 0 terms count twice (m and -m). A cross
        // spectrum can be negative, so PowSpec (which refuses that) is not used.
        r.cl.assign(size_t(lmax) + 1, 0.0);
        for (int l = 0; l <= lmax; ++l)
        {
            double sum = aA(l, 0).real() * aB(l, 0).real();
            for (int m = 1; m <= l; ++m)
                sum += 2.0 * (aA(l, m).real() * aB(l, m).real()
                              + aA(l, m).imag() * aB(l, m).imag());
            r.cl[size_t(l)] = sum / (2.0 * l + 1.0);
        }
        for (double &c : r.cl)
            c /= r.fsky;
    }
    catch (const PlanckError &e) // not derived from std::exception
    {
        r.cl.clear();
        r.error = e.what();
    }
    catch (const std::exception &e)
    {
        r.cl.clear();
        r.error = e.what();
    }
    return r;
}
