#include "resolution.hpp"
#include "unseen.hpp"
#include "parallel.hpp"

#include "alm.h"
#include "healpix_cxx/fitshandle.h"
#include "npyreader.hpp"
#include "alm_healpix_tools.h"
#include "healpix_cxx/error_handling.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace
{
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
    const bool bad = parallel::reduceRange<bool>(
        in.Npix(),
        [&](long begin, long end)
    {
        for (long i = begin; i < end; ++i)
            if (!std::isfinite(in[i]))
                return true;
        return false;
    }, [](bool &total, const bool &part) { total = total || part; });
    if (bad)
    {
        cleaned = in;
        parallel::forRange(cleaned.Npix(), [&](long begin, long end)
        {
            for (long i = begin; i < end; ++i)
                if (!std::isfinite(cleaned[i]))
                    cleaned[i] = UNSEEN;
        });
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
    const long unseen = parallel::reduceRange<long>(
        in.Npix(),
        [&](long begin, long end)
    {
        long bad = 0;
        for (long i = begin; i < end; ++i)
        {
            const bool good = isSeen(in[i]);
            bad += !good;
            map[i] = good ? double(in[i]) : 0.0;
        }
        return bad;
    }, [](long &total, const long &part) { total += part; });
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

    // Swap the pixel window of the input for that of the output
    std::vector<double> factor(size_t(lmaxOut) + 1, 1.0);
    auto need = [lmaxOut](const std::vector<double> &w, const char *which)
    {
        if (int(w.size()) <= lmaxOut)
            throw PlanckError((std::string("the ") + which
                               + " pixel window has " + std::to_string(w.size())
                               + " values, l up to " + std::to_string(lmaxOut)
                               + " is needed")
                                  .c_str());
    };
    if (!o.windowIn.empty())
    {
        need(o.windowIn, "input");
        for (int l = 0; l <= lmaxOut; ++l)
        {
            if (!(std::abs(o.windowIn[size_t(l)]) > 1e-12))
                throw PlanckError(("the input pixel window is zero at l = "
                                   + std::to_string(l))
                                      .c_str());
            factor[size_t(l)] /= o.windowIn[size_t(l)];
        }
    }
    if (!o.windowOut.empty())
    {
        need(o.windowOut, "output");
        for (int l = 0; l <= lmaxOut; ++l)
            factor[size_t(l)] *= o.windowOut[size_t(l)];
    }
    if (!o.windowIn.empty() || !o.windowOut.empty())
        low.ScaleL(factor);

    Healpix_Map<double> down(o.nsideOut, RING, SET_NSIDE);
    alm2map(low, down);

    auto out = std::make_shared<Healpix_Map<float>>(o.nsideOut, RING,
                                                    SET_NSIDE);
    parallel::forRange(out->Npix(), [&](long begin, long end)
    {
        for (long i = begin; i < end; ++i)
            (*out)[i] = float(down[i]);
    });

    // Pixels that were mostly UNSEEN stay UNSEEN: the fraction of valid
    // sub-pixels of each output pixel decides
    if (unseen > 0 && o.keepUnseen)
    {
        Healpix_Map<float> valid(nsideIn, in.Scheme(), SET_NSIDE);
        parallel::forRange(in.Npix(), [&](long begin, long end)
        {
            for (long i = begin; i < end; ++i)
                valid[i] = isSeen(in[i]) ? 1.0f : 0.0f;
        });
        Healpix_Map<float> frac(o.nsideOut, in.Scheme(), SET_NSIDE);
        frac.Import_degrade(valid, false); // mean of 1 / 0 = valid fraction
        if (frac.Scheme() == NEST)
            frac.swap_scheme();
        parallel::forRange(out->Npix(), [&](long begin, long end)
        {
            for (long i = begin; i < end; ++i)
                if (frac[i] < 0.5f)
                    (*out)[i] = UNSEEN;
        });
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
    if (o.windowIn.empty() && o.windowOut.empty())
        r.notes << "No pixel window function was applied.";
    else
        r.notes << QString("Pixel window: %1%2.")
                       .arg(o.windowIn.empty() ? ""
                                               : "input divided out")
                       .arg(o.windowOut.empty()
                                ? ""
                                : (o.windowIn.empty() ? "output applied"
                                                      : ", output applied"));
    return r;
}
} // namespace

std::vector<double>
readPixelWindow(const QString &path)
{
    const std::string file = path.toStdString();
    std::vector<double> w;
    const QString lower = path.toLower();

    if (lower.endsWith(".fits") || lower.endsWith(".fit")
        || lower.endsWith(".fts"))
    {
        try
        {
            fitshandle f;
            f.open(file);
            // HEALPix's files keep the windows in the first table
            for (int hdu = 1; hdu <= f.num_hdus(); ++hdu)
            {
                f.goto_hdu(hdu);
                if (!f.key_present("TFIELDS"))
                    continue; // not a table, e.g. the empty primary HDU
                const int ncols = f.ncols();
                if (ncols < 1)
                    continue;
                int col = 1;
                for (int c = 1; c <= ncols; ++c)
                    if (QString::fromStdString(f.colname(c))
                            .trimmed()
                            .toUpper()
                            .startsWith("TEMP"))
                        col = c;
                f.read_entire_column(col, w);
                break;
            }
        }
        catch (const PlanckError &e)
        {
            throw std::runtime_error(std::string("cannot read ") + file + ": "
                                     + e.what());
        }
        if (w.empty())
            throw std::runtime_error(file + " has no table column");
        return w;
    }

    if (lower.endsWith(".npy"))
    {
        const NpyArray a = readNpy(file);
        const size_t n   = a.shape.back();
        w.assign(a.data.begin(), a.data.begin() + long(n));
        return w;
    }

    // Text
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        throw std::runtime_error("cannot open " + file);
    QTextStream in(&f);
    std::vector<std::pair<long, double>> rows;
    bool twoColumns = false;
    int lineNo      = 0;
    while (!in.atEnd())
    {
        QString line = in.readLine();
        ++lineNo;
        const int hash = line.indexOf('#');
        if (hash >= 0)
            line.truncate(hash);
        line.replace(',', ' ').replace(';', ' ');
        const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;
        bool ok1 = false, ok2 = true;
        const double a = parts[0].toDouble(&ok1);
        double b       = 0.0;
        if (parts.size() >= 2)
            b = parts[1].toDouble(&ok2);
        if (!ok1 || !ok2)
        {
            if (rows.empty()) // a header line
                continue;
            throw std::runtime_error("line " + std::to_string(lineNo)
                                     + " of " + file + " is not numeric");
        }
        if (parts.size() >= 2)
        {
            twoColumns = true;
            rows.push_back({std::lround(a), b});
        }
        else
            rows.push_back({long(rows.size()), a});
    }
    if (rows.empty())
        throw std::runtime_error(file + " has no values");
    if (twoColumns)
    {
        if (rows.front().first != 0)
            throw std::runtime_error(file + " must start at l = 0");
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].first != long(i))
                throw std::runtime_error(
                    file + ": l must go 0, 1, 2, ... without gaps (line "
                    + std::to_string(i + 1) + ")");
    }
    for (const auto &r : rows)
        w.push_back(r.second);
    return w;
}

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
