#include "harmonicmaps.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "healpix_cxx/error_handling.h"
#include "parallel.hpp"
#include "smoothing.hpp"
#include "unseen.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

HarmonicResult
harmonicMap(const Healpix_Map<float> &in, const HarmonicOptions &o)
{
    HarmonicResult r;
    try
    {
        const int nside = in.Nside();
        const int lmax
            = o.lmax > 0 ? std::min(o.lmax, 3 * nside - 1) : 3 * nside - 1;
        const int iter = std::max(0, o.iterations);
        // Ring weights of the quadrature: 1 everywhere (unweighted)
        arr<double> weights(2 * nside);
        weights.fill(1.0);

        long unseen = 0;
        const bool keep = o.keepUnseen;

        if (o.op == HarmonicOp::EB)
        {
            Healpix_Map<float> maps[3];
            readStokesMaps(in, o.path, o.hdu, maps);

            Healpix_Map<double> mT = toDouble(maps[0], unseen),
                                mQ = toDouble(maps[1], unseen),
                                mU = toDouble(maps[2], unseen);
            Alm<xcomplex<double>> aT(lmax, lmax), aE(lmax, lmax),
                aB(lmax, lmax);
            map2alm_pol_iter(mT, mQ, mU, aT, aE, aB, iter, weights);

            // E and B as scalar fields: sum of a_lm Y_lm of each part alone
            Healpix_Map<double> mE(nside, RING, SET_NSIDE),
                mBm(nside, RING, SET_NSIDE);
            alm2map(aE, mE);
            alm2map(aB, mBm);
            r.map  = toFloat(mE, maps[0], keep && unseen > 0);
            r.mapB = toFloat(mBm, maps[0], keep && unseen > 0);
            r.notes << "E and B are the scalar maps of the gradient-like and "
                       "curl-like parts of (Q, U). On a cut sky some E leaks "
                       "into B near the edge of the mask.";
        }
        else
        {
            Healpix_Map<double> m = toDouble(in, unseen);
            Alm<xcomplex<double>> a(lmax, lmax);
            map2alm_iter(m, a, iter, weights);

            if (o.op == HarmonicOp::Laplacian)
            {
                // Y_lm is an eigenfunction of nabla^2 with eigenvalue
                // -l (l + 1) on the unit sphere
                std::vector<double> factor(size_t(lmax) + 1);
                for (int l = 0; l <= lmax; ++l)
                    factor[size_t(l)] = -double(l) * (l + 1.0);
                a.ScaleL(factor);
                alm2map(a, m);
                r.notes << "Units: map units per steradian (radian^-2). The Laplacian "
                           "multiplies small scales by l(l+1), so noise grows "
                           "fast: lower the analysis l max to suppress it.";
            }
            else
            {
                // d/dtheta and (1 / sin theta) d/dphi of the map
                Healpix_Map<double> dth(nside, RING, SET_NSIDE),
                    dph(nside, RING, SET_NSIDE);
                alm2map_der1(a, m, dth, dph);
                parallel::forRange(m.Npix(), [&](long begin, long end)
                {
                    for (long i = begin; i < end; ++i)
                        m[i] = std::sqrt(dth[i] * dth[i] + dph[i] * dph[i]);
                });
                r.notes << "Units: map units per radian.";
            }
            r.map = toFloat(m, in, keep && unseen > 0);
        }

        r.notes << QString("Analysed up to l = %1 (%2 iterations).")
                       .arg(lmax)
                       .arg(iter);
        if (unseen > 0)
            r.notes << QString("%1 UNSEEN / NaN pixels were taken as zero in "
                               "the transform; derivatives ring near the "
                               "edge of a mask.")
                           .arg(unseen);
    }
    catch (const PlanckError &e) // not derived from std::exception
    {
        r       = HarmonicResult();
        r.error = e.what();
    }
    catch (const std::exception &e)
    {
        r       = HarmonicResult();
        r.error = e.what();
    }
    return r;
}
