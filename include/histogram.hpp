#pragma once

#include "healpix_map.h"

#include <QString>
#include <functional>
#include <memory>
#include <vector>

// One map to histogram: values of a map in memory (a computed map), or of a
// column read from a FITS file
struct HistogramSource
{
    QString name;
    std::shared_ptr<const Healpix_Map<float>> map;
    QString path;
    int hdu    = 0; // 1-based
    int column = 0; // 1-based
    // 1 = use the pixel; empty = use every valid pixel (no masks)
    std::shared_ptr<const std::vector<uint8_t>> keep;
    // Alternative to keep, called on the worker thread (e.g. to select the
    // pixels of a region without blocking the GUI); wins over keep
    std::function<std::vector<uint8_t>()> makeKeep;
    // An empty source (no valid pixels) gives an empty curve instead of an
    // error, and does not affect the range
    bool optional = false;
};

struct HistogramCurve
{
    QString name;
    std::vector<double> counts; // per bin
    long valid  = 0;            // pixels used (valid and not masked)
    double mean = 0.0;
    double std  = 0.0;
};

struct HistogramResult
{
    double lo = 0.0; // the bins share one range
    double hi = 1.0;
    int bins  = 0;
    std::vector<HistogramCurve> curves;
    QString error;
};

// Histograms of the sources over one range: the union of their value ranges
// (or, with clipOutliers, of their 0.1 % to 99.9 % ranges, with the pixels
// outside left out). Blocking, uses all cores: run it on a worker thread.
HistogramResult
computeHistograms(const std::vector<HistogramSource> &sources, int bins,
                  bool clipOutliers);
