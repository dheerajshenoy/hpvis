#include "smoothing.hpp"

#include "alm.h"
#include "alm_healpix_tools.h"
#include "alm_powspec_tools.h"
#include "fitsinfo.hpp"
#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "healpix_cxx/healpix_map_fitsio.h"

#include <algorithm>
#include <cmath>

namespace
{
bool
isGood(float v)
{
    return std::isfinite(v) && v > -1e30f;
}

// Copies to double in RING order; UNSEEN / NaN pixels become zero
Healpix_Map<double>
toDouble(const Healpix_Map<float> &in, long &unseen)
{
    Healpix_Map<double> m(in.Nside(), in.Scheme(), SET_NSIDE);
    for (long i = 0; i < in.Npix(); ++i)
    {
        const bool good = isGood(in[i]);
        unseen += !good;
        m[i] = good ? double(in[i]) : 0.0;
    }
    if (m.Scheme() == NEST)
        m.swap_scheme();
    return m;
}

std::shared_ptr<Healpix_Map<float>>
toFloat(const Healpix_Map<double> &m, const Healpix_Map<float> &in,
        bool keepUnseen)
{
    auto out = std::make_shared<Healpix_Map<float>>(m.Nside(), RING, SET_NSIDE);
    for (long i = 0; i < out->Npix(); ++i)
        (*out)[i] = float(m[i]);
    if (in.Scheme() == NEST)
        out->swap_scheme();
    if (keepUnseen)
        for (long i = 0; i < out->Npix(); ++i)
            if (!isGood(in[i]))
                (*out)[i] = float(Healpix_undef);
    return out;
}

// Columns of the intensity, Q and U maps of an HDU; false when it has no Q / U
bool
stokesColumns(const FitsHdu &hdu, int cols[3])
{
    int q = 0, u = 0, t = 0;
    for (const FitsColumn &col : hdu.columns)
    {
        const QString n = col.name.trimmed().toUpper();
        if (!q && n.startsWith('Q'))
            q = col.index;
        else if (!u && n.startsWith('U'))
            u = col.index;
    }
    if (!q || !u)
        return false;
    for (const FitsColumn &col : hdu.columns)
        if (col.index != q && col.index != u)
        {
            t = col.index;
            break;
        }
    if (!t)
        return false;
    cols[0] = t;
    cols[1] = q;
    cols[2] = u;
    return true;
}
} // namespace

bool
hasPolarization(const QString &path, int hdu)
{
    if (path.isEmpty() || hdu <= 0)
        return false;
    try
    {
        fitshandle file;
        file.open(path.toStdString());
        const FitsInfo info = scanFits(file);
        const FitsHdu *h    = info.find(hdu);
        int cols[3];
        return h && stokesColumns(*h, cols);
    }
    catch (const PlanckError &)
    {
        return false;
    }
}

SmoothResult
smoothMap(const Healpix_Map<float> &in, const SmoothOptions &o)
{
    SmoothResult r;
    try
    {
        const int nside = in.Nside();
        int lmax
            = o.lmax > 0 ? std::min(o.lmax, 3 * nside - 1) : 3 * nside - 1;
        if (!o.beam.empty() && int(o.beam.size()) <= lmax)
        {
            lmax = int(o.beam.size()) - 1;
            r.notes << QString("The beam window ends at l = %1, so the "
                               "analysis stops there.")
                           .arg(lmax);
        }
        if (lmax < 1)
            throw PlanckError("the beam window needs at least two values");

        // The T, Q and U maps; only T unless polarization was asked for
        Healpix_Map<float> maps[3];
        const Healpix_Map<float> *src[3] = {&in, nullptr, nullptr};
        if (o.polarization)
        {
            fitshandle file;
            file.open(o.path.toStdString());
            const FitsInfo info = scanFits(file);
            const FitsHdu *hdu  = info.find(o.hdu);
            int cols[3];
            if (!hdu || !stokesColumns(*hdu, cols))
                throw PlanckError("the map has no Q and U columns");
            for (int k = 0; k < 3; ++k)
            {
                file.goto_hdu(o.hdu);
                read_Healpix_map_from_fits(file, maps[k], cols[k]);
                // The view may show the map with another pixel ordering than
                // the file says: read the values the same way
                if (maps[k].Scheme() != in.Scheme())
                    maps[k].Healpix_Base::SetNside(maps[k].Nside(),
                                                   in.Scheme());
                src[k] = &maps[k];
            }
            if (maps[1].Nside() != maps[0].Nside()
                || maps[2].Nside() != maps[0].Nside()
                || maps[0].Nside() != nside)
                throw PlanckError("T, Q and U differ in resolution");
        }

        long unseen = 0;
        arr<double> weights(2 * nside);
        weights.fill(1.0);
        Alm<xcomplex<double>> aT(lmax, lmax), aE(lmax, lmax), aB(lmax, lmax);

        std::vector<double> window = o.beam;
        window.resize(size_t(lmax) + 1);
        auto apply = [&](Alm<xcomplex<double>> &a)
        {
            if (!o.beam.empty())
                a.ScaleL(window);
        };

        if (o.polarization)
        {
            Healpix_Map<double> mT = toDouble(*src[0], unseen),
                                mQ = toDouble(*src[1], unseen),
                                mU = toDouble(*src[2], unseen);
            map2alm_pol_iter(mT, mQ, mU, aT, aE, aB,
                             std::max(0, o.iterations), weights);
            if (o.beam.empty())
                smoothWithGauss(aT, aE, aB, o.fwhmRad);
            else
            {
                apply(aT);
                apply(aE);
                apply(aB);
            }
            alm2map_pol(aT, aE, aB, mT, mQ, mU);
            r.map  = toFloat(mT, *src[0], o.keepUnseen && unseen > 0);
            r.mapQ = toFloat(mQ, *src[1], o.keepUnseen && unseen > 0);
            r.mapU = toFloat(mU, *src[2], o.keepUnseen && unseen > 0);
        }
        else
        {
            Healpix_Map<double> m = toDouble(in, unseen);
            map2alm_iter(m, aT, std::max(0, o.iterations), weights);
            if (o.beam.empty())
                smoothWithGauss(aT, o.fwhmRad);
            else
                apply(aT);
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
    catch (const std::exception &e)
    {
        r       = SmoothResult();
        r.error = e.what();
    }
    return r;
}
