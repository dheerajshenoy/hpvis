#include "resolution.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "healpix_cxx/error_handling.h"

#include <algorithm>
#include <cmath>

namespace
{
bool
isGood(float v)
{
    return std::isfinite(v) && v > -1.0e30f;
}

bool
isPowerOfTwo(long n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

// Pixel average (healpy's ud_grade). Import_degrade handles the orderings.
DowngradeResult
pixelDowngrade(const Healpix_Map<float> &in, const DowngradeOptions &o)
{
    DowngradeResult r;
    auto out = std::make_shared<Healpix_Map<float>>(o.nsideOut, in.Scheme(),
                                                    SET_NSIDE);
    // Non-finite values count as UNSEEN
    const Healpix_Map<float> *src = &in;
    Healpix_Map<float> cleaned;
    bool bad = false;
    for (long i = 0; i < in.Npix() && !bad; ++i)
        bad = !std::isfinite(in[i]);
    if (bad)
    {
        cleaned = in;
        for (long i = 0; i < cleaned.Npix(); ++i)
            if (!std::isfinite(cleaned[i]))
                cleaned[i] = float(Healpix_undef);
        src = &cleaned;
    }
    out->Import_degrade(*src, o.pessimistic);
    r.map = std::move(out);
    r.notes << QString("Averaged the %1 sub-pixels of each pixel. Power above "
                       "l = %2 is aliased into the result; use the harmonic "
                       "method for a clean low-pass.")
                   .arg((in.Nside() / o.nsideOut) * (in.Nside() / o.nsideOut))
                   .arg(3 * o.nsideOut - 1);
    return r;
}

DowngradeResult
harmonicDowngrade(const Healpix_Map<float> &in, const DowngradeOptions &o)
{
    DowngradeResult r;
    const int nsideIn = in.Nside();
    const int lmaxIn
        = o.lmaxIn > 0 ? std::min(o.lmaxIn, 3 * nsideIn - 1) : 3 * nsideIn - 1;
    const int lmaxOut = std::min(3 * o.nsideOut - 1, lmaxIn);

    // double precision RING copy; UNSEEN / NaN pixels become zero
    Healpix_Map<double> map(nsideIn, in.Scheme(), SET_NSIDE);
    long unseen = 0;
    for (long i = 0; i < in.Npix(); ++i)
    {
        const bool good = isGood(in[i]);
        unseen += !good;
        map[i] = good ? double(in[i]) : 0.0;
    }
    if (map.Scheme() == NEST)
        map.swap_scheme();

    arr<double> weights(2 * nsideIn);
    weights.fill(1.0);
    Alm<xcomplex<double>> alm(lmaxIn, lmaxIn);
    map2alm_iter(map, alm, std::max(0, o.iterations), weights);
    map = Healpix_Map<double>(); // free the input copy before the synthesis

    // Drop everything above the new resolution
    Alm<xcomplex<double>> low(lmaxOut, lmaxOut);
    for (int m = 0; m <= lmaxOut; ++m)
        for (int l = m; l <= lmaxOut; ++l)
            low(l, m) = alm(l, m);

    Healpix_Map<double> down(o.nsideOut, RING, SET_NSIDE);
    alm2map(low, down);

    auto out = std::make_shared<Healpix_Map<float>>(o.nsideOut, RING,
                                                    SET_NSIDE);
    for (long i = 0; i < out->Npix(); ++i)
        (*out)[i] = float(down[i]);

    // Pixels that were mostly UNSEEN stay UNSEEN: the fraction of valid
    // sub-pixels of each output pixel decides
    if (unseen > 0 && o.keepUnseen)
    {
        Healpix_Map<float> valid(nsideIn, in.Scheme(), SET_NSIDE);
        for (long i = 0; i < in.Npix(); ++i)
            valid[i] = isGood(in[i]) ? 1.0f : 0.0f;
        Healpix_Map<float> frac(o.nsideOut, in.Scheme(), SET_NSIDE);
        frac.Import_degrade(valid, false); // mean of 1 / 0 = valid fraction
        if (frac.Scheme() == NEST)
            frac.swap_scheme();
        for (long i = 0; i < out->Npix(); ++i)
            if (frac[i] < 0.5f)
                (*out)[i] = float(Healpix_undef);
    }

    if (in.Scheme() == NEST)
        out->swap_scheme();

    r.map = std::move(out);
    r.notes << QString("Analysed up to l = %1 (%2 iterations), kept l <= %3.")
                   .arg(lmaxIn)
                   .arg(o.iterations)
                   .arg(lmaxOut);
    if (unseen > 0)
        r.notes << QString("%1 UNSEEN / NaN pixels were taken as zero in the "
                           "transform; on a cut sky this leaks power across "
                           "the edge of the mask.")
                       .arg(unseen);
    r.notes << "The pixel window functions are not corrected for.";
    return r;
}
} // namespace

DowngradeResult
downgradeMap(const Healpix_Map<float> &in, const DowngradeOptions &o)
{
    DowngradeResult r;
    try
    {
        if (in.Npix() == 0)
            throw PlanckError("there is no map");
        if (!isPowerOfTwo(o.nsideOut) || o.nsideOut >= in.Nside())
            throw PlanckError("the new nside must be a power of two smaller "
                              "than the map's");
        r = o.method == DowngradeMethod::Pixel ? pixelDowngrade(in, o)
                                               : harmonicDowngrade(in, o);
    }
    catch (const PlanckError &e)
    {
        r       = DowngradeResult();
        r.error = e.what();
        if (r.error.isEmpty())
            r.error = "the operation failed";
    }
    catch (const std::exception &e)
    {
        r       = DowngradeResult();
        r.error = e.what();
    }
    return r;
}

double
harmonicMemoryEstimate(int nsideIn, int lmaxIn)
{
    const double npix = 12.0 * nsideIn * nsideIn;
    const double alm  = double(lmaxIn + 1) * (lmaxIn + 2) / 2.0 * 16.0;
    // the double copy, the float input, and the a_lm (plus libsharp's work
    // arrays, about the size of the map again)
    return npix * (8.0 + 4.0 + 8.0) + alm;
}
