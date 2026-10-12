#include "histogram.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "npyreader.hpp"
#include "openglwidget.hpp"
#include "healpix_cxx/healpix_map_fitsio.h"

#include <QDateTime>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
using MapPtr = std::shared_ptr<const Healpix_Map<float>>;

// The most recently read columns, so changing the bin count does not read
// the file again
struct CacheEntry
{
    QString key;
    MapPtr map;
};
QMutex g_cacheMutex;
std::vector<CacheEntry> g_cache;
constexpr size_t kCacheSize = 3;

MapPtr
readColumn(const QString &path, int hdu, int column)
{
    const QFileInfo info(path);
    const QString key = QString("%1|%2|%3|%4|%5")
                            .arg(info.absoluteFilePath())
                            .arg(hdu)
                            .arg(column)
                            .arg(info.size())
                            .arg(info.lastModified().toMSecsSinceEpoch());
    {
        QMutexLocker lock(&g_cacheMutex);
        for (const CacheEntry &e : g_cache)
            if (e.key == key)
                return e.map;
    }

    if (!info.exists())
        throw PlanckError(QString("%1 does not exist")
                              .arg(info.fileName())
                              .toStdString()
                              .c_str());
    std::shared_ptr<Healpix_Map<float>> map;
    if (isNpyPath(path.toStdString()))
        map = readNpyMap(path.toStdString(), column, kMaxSupportedNside);
    else
    {
        fitshandle file;
        file.open(path.toStdString());
        file.goto_hdu(hdu > 0 ? hdu : 2);
        map = std::make_shared<Healpix_Map<float>>();
        read_Healpix_map_from_fits(file, *map, column > 0 ? column : 1);
    }

    MapPtr result = map;
    QMutexLocker lock(&g_cacheMutex);
    g_cache.push_back({key, result});
    if (g_cache.size() > kCacheSize)
        g_cache.erase(g_cache.begin());
    return result;
}

inline bool
isValid(float v)
{
    return std::isfinite(v) && v > -1.0e30f;
}

struct Range
{
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
    double sum = 0.0, sumSq = 0.0;
    long n = 0;
};

Range
valueRange(const Healpix_Map<float> &map, const std::vector<uint8_t> *keep)
{
    Range total;
    const long npix = map.Npix();
#pragma omp parallel
    {
        Range r;
#pragma omp for nowait schedule(static)
        for (long i = 0; i < npix; ++i)
        {
            const float v = map[i];
            if (!isValid(v) || (keep && !(*keep)[i]))
                continue;
            r.min = std::min<double>(r.min, v);
            r.max = std::max<double>(r.max, v);
            r.sum += v;
            r.sumSq += double(v) * v;
            ++r.n;
        }
#pragma omp critical
        {
            total.min = std::min(total.min, r.min);
            total.max = std::max(total.max, r.max);
            total.sum += r.sum;
            total.sumSq += r.sumSq;
            total.n += r.n;
        }
    }
    return total;
}

// Counts of the kept pixels in `bins` equal bins over [lo, hi]
std::vector<double>
binCounts(const Healpix_Map<float> &map, const std::vector<uint8_t> *keep,
          double lo, double hi, int bins)
{
    std::vector<double> total(bins, 0.0);
    const long npix    = map.Npix();
    const double scale = bins / (hi - lo);
#pragma omp parallel
    {
        std::vector<double> local(bins, 0.0);
#pragma omp for nowait schedule(static)
        for (long i = 0; i < npix; ++i)
        {
            const float v = map[i];
            if (!isValid(v) || (keep && !(*keep)[i]) || v < lo || v > hi)
                continue;
            int b = int((v - lo) * scale);
            if (b >= bins)
                b = bins - 1; // v == hi
            local[b] += 1.0;
        }
#pragma omp critical
        for (int b = 0; b < bins; ++b)
            total[b] += local[b];
    }
    return total;
}
} // namespace

HistogramResult
computeHistograms(const std::vector<HistogramSource> &sources, int bins,
                  bool clipOutliers)
{
    HistogramResult result;
    result.bins = std::max(1, bins);

    try
    {
        std::vector<MapPtr> maps;
        std::vector<std::shared_ptr<const std::vector<uint8_t>>> keeps;
        std::vector<Range> ranges;
        std::vector<std::pair<double, double>> spans; // what each contributes
        for (const HistogramSource &src : sources)
        {
            MapPtr map
                = src.map ? src.map : readColumn(src.path, src.hdu, src.column);
            std::shared_ptr<const std::vector<uint8_t>> keepOwner = src.keep;
            if (src.makeKeep)
                keepOwner = std::make_shared<const std::vector<uint8_t>>(
                    src.makeKeep());
            const std::vector<uint8_t> *keep
                = (keepOwner && keepOwner->size() == size_t(map->Npix()))
                      ? keepOwner.get()
                      : nullptr;
            keeps.push_back(keepOwner);
            const Range r = valueRange(*map, keep);
            if (r.n == 0 && src.optional)
            {
                maps.push_back(map);
                ranges.push_back(r);
                spans.push_back({std::numeric_limits<double>::max(),
                                 std::numeric_limits<double>::lowest()});
                continue;
            }
            if (r.n == 0)
                throw PlanckError(QString("%1 has no valid pixels")
                                      .arg(src.name)
                                      .toStdString()
                                      .c_str());
            maps.push_back(map);
            ranges.push_back(r);

            double lo = r.min, hi = r.max;
            if (clipOutliers && hi > lo)
            {
                // 0.1 % and 99.9 % from a fine histogram
                constexpr int kFine = 4096;
                const auto fine = binCounts(*map, keep, r.min, r.max, kFine);
                const double w  = (r.max - r.min) / kFine;
                double cum      = 0.0;
                bool haveLo     = false;
                for (int b = 0; b < kFine; ++b)
                {
                    cum += fine[b];
                    if (!haveLo && cum >= 0.001 * r.n)
                    {
                        lo     = r.min + b * w;
                        haveLo = true;
                    }
                    if (cum >= 0.999 * r.n)
                    {
                        hi = r.min + (b + 1) * w;
                        break;
                    }
                }
            }
            spans.push_back({lo, hi});
        }

        double lo = std::numeric_limits<double>::max();
        double hi = std::numeric_limits<double>::lowest();
        for (const auto &s : spans)
        {
            lo = std::min(lo, s.first);
            hi = std::max(hi, s.second);
        }
        if (!(hi > lo)) // a constant map
        {
            lo -= 0.5;
            hi += 0.5;
        }
        result.lo = lo;
        result.hi = hi;

        for (size_t i = 0; i < sources.size(); ++i)
        {
            const HistogramSource &src = sources[i];
            const std::vector<uint8_t> *keep
                = (keeps[i] && keeps[i]->size() == size_t(maps[i]->Npix()))
                      ? keeps[i].get()
                      : nullptr;
            HistogramCurve curve;
            curve.name   = src.name;
            curve.counts = binCounts(*maps[i], keep, lo, hi, result.bins);
            curve.valid  = ranges[i].n;
            if (ranges[i].n > 0)
            {
                curve.mean = ranges[i].sum / ranges[i].n;
                curve.std
                    = std::sqrt(std::max(0.0, ranges[i].sumSq / ranges[i].n
                                                  - curve.mean * curve.mean));
            }
            result.curves.push_back(std::move(curve));
        }
    }
    catch (const PlanckError &e)
    {
        result       = HistogramResult();
        result.error = e.what();
        if (result.error.isEmpty() || result.error == "FITS error")
            result.error = "could not read the map";
    }
    catch (const std::exception &e)
    {
        result       = HistogramResult();
        result.error = e.what();
    }
    return result;
}
