#include "pixelwindow.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"

#include <QFile>
#include <QTemporaryFile>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace
{
PixelWindow
readWindow(int nside)
{
    const QString name = QString("pixel_window_n%1.fits")
                             .arg(nside, 4, 10, QLatin1Char('0'));
    QFile resource(":/resources/pixwin/" + name);
    if (!resource.open(QIODevice::ReadOnly))
        throw std::runtime_error(
            QString("there is no pixel window for nside %1 (HEALPix has "
                    "them for powers of two from 2 to 8192)")
                .arg(nside)
                .toStdString());

    // fitshandle reads files: write the (small) resource out first
    QTemporaryFile tmp;
    if (!tmp.open() || tmp.write(resource.readAll()) < 0 || !tmp.flush())
        throw std::runtime_error("cannot write a temporary file");

    PixelWindow w;
    try
    {
        fitshandle f;
        f.open(tmp.fileName().toStdString());
        for (int hdu = 1; hdu <= f.num_hdus(); ++hdu)
        {
            f.goto_hdu(hdu);
            if (!f.key_present("TFIELDS") || f.ncols() < 2)
                continue;
            f.read_entire_column(1, w.T); // TEMPERATURE
            f.read_entire_column(2, w.P); // POLARIZATION
            break;
        }
    }
    catch (const PlanckError &e)
    {
        throw std::runtime_error(std::string("cannot read ")
                                 + name.toStdString() + ": " + e.what());
    }
    if (w.T.empty() || w.P.empty())
        throw std::runtime_error(name.toStdString() + " has no windows");
    return w;
}
} // namespace

PixelWindow
pixwin(int nside, int lmax)
{
    static std::mutex mutex;
    static std::map<int, std::shared_ptr<const PixelWindow>> cache;
    std::shared_ptr<const PixelWindow> full;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = cache.find(nside);
        if (it == cache.end())
            it = cache
                     .emplace(nside, std::make_shared<const PixelWindow>(
                                         readWindow(nside)))
                     .first;
        full = it->second;
    }
    if (lmax < 0)
        return *full;
    if (lmax >= int(full->T.size()))
        throw std::runtime_error(
            QString("the pixel window of nside %1 ends at l = %2, l = %3 is "
                    "needed")
                .arg(nside)
                .arg(full->T.size() - 1)
                .arg(lmax)
                .toStdString());
    PixelWindow w;
    w.T.assign(full->T.begin(), full->T.begin() + lmax + 1);
    w.P.assign(full->P.begin(), full->P.begin() + lmax + 1);
    return w;
}

void
applyPixelWindow(Spectra &s, const PixelWindow &w, bool multiply, int bandwidth)
{
    const int size = int(std::min(w.T.size(), w.P.size()));
    enum Kind { TT, PP, TP };
    auto factor = [&](Kind k, int l)
    {
        const double t = w.T[size_t(l)], p = w.P[size_t(l)];
        return k == TT ? t * t : k == PP ? p * p : t * p;
    };
    // The mean factor over the multipoles a value stands for
    auto at = [&](Kind k, size_t i)
    {
        if (s.ell.empty())
            return int(i) < size ? factor(k, int(i)) : std::nan("");
        const double half = 0.5 * std::max(1, bandwidth);
        const int lo      = std::max(0, int(std::ceil(s.ell[i] - half)));
        const int hi      = std::min(size - 1, int(std::floor(s.ell[i] + half)));
        if (hi < lo)
            return std::nan("");
        double sum = 0.0;
        for (int l = lo; l <= hi; ++l)
            sum += factor(k, l);
        return sum / (hi - lo + 1);
    };
    auto scale = [&](std::vector<double> &v, Kind k)
    {
        for (size_t i = 0; i < v.size(); ++i)
        {
            const double f = at(k, i);
            // The polarization window is zero at l = 0 and 1
            v[i] = !(f > 0.0) ? std::nan("") : multiply ? v[i] * f : v[i] / f;
        }
    };
    scale(s.TT, TT);
    scale(s.EE, PP);
    scale(s.BB, PP);
    scale(s.EB, PP);
    scale(s.TE, TP);
    scale(s.TB, TP);
}
