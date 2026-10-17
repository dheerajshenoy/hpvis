#pragma once

#include "smoothing.hpp"

#include <vector>

enum class FilterType
{
    LowPass,  // keeps l <= lmax
    HighPass, // keeps l >= lmin
    BandPass, // keeps lmin <= l <= lmax
    BandStop  // removes lmin <= l <= lmax
};

enum class FilterTaper
{
    Sharp,   // a step: rings in the map
    Cosine,  // raised-cosine ramp across `taperWidth` multipoles
    Gaussian // error-function ramp, sigma = taperWidth / 2
};

struct FilterOptions
{
    FilterType type   = FilterType::LowPass;
    FilterTaper taper = FilterTaper::Cosine;
    int lmin          = 2;
    int lmax          = 100;
    int taperWidth    = 10;   // multipoles; the ramp is centred on the cut
    int iterations    = 3;    // refinements of the map2alm quadrature
    int analysisLmax  = 0;    // 0 = 3 * nside - 1
    bool keepUnseen   = true; // UNSEEN input pixels stay UNSEEN
    // Filter Q and U as well (T, E and B get the same window). The input is
    // then read from `path` / `hdu`.
    bool polarization = false;
    QString path;
    int hdu = 0;
};

using FilterResult = SmoothResult;

// The window f(l) for l = 0..analysisLmax (size analysisLmax + 1), in [0, 1]
std::vector<double>
filterWindow(const FilterOptions &options, int analysisLmax);

// map2alm, a_lm *= f(l), alm2map. Results have the input's nside and
// ordering. UNSEEN / NaN pixels are taken as zero in the transform.
// Blocking: run it on a worker thread.
FilterResult
filterMap(const Healpix_Map<float> &in, const FilterOptions &options);
