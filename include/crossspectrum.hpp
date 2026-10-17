#pragma once

#include "healpix_map.h"

#include <QString>
#include <vector>

struct CrossSpectrumResult
{
    std::vector<double> cl; // C_l of the cross spectrum, l = 0 .. lmax
    double fsky   = 1.0;    // mean of the squared weights
    long unseen   = 0;      // pixels left out (UNSEEN / NaN in either map)
    QString error;          // empty on success
};

// Pseudo cross-spectrum of the intensity of two maps of the same nside, as in
// the null test of two independent splits: noise that is independent in the
// two maps averages out of it. Each pixel is weighted by `weights` (empty = 1)
// and left out when either map is UNSEEN / NaN; the result is divided by the
// mean squared weight, which is exact only for l well above the size of the
// mask (it does not remove the mode coupling the way MASTER does).
// Blocking: run it on a worker thread.
CrossSpectrumResult
crossSpectrum(const Healpix_Map<float> &a, const Healpix_Map<float> &b,
              const std::vector<float> &weights, int lmax, int iterations);
