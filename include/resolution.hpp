#pragma once

#include "healpix_map.h"

#include <QString>
#include <QStringList>
#include <memory>

enum class DowngradeMethod
{
    // Average of the sub-pixels, like healpy's ud_grade. Fast, but power above
    // the new resolution is aliased into the map.
    Pixel,
    // map2alm, drop the a_lm above the new resolution's l_max, alm2map. Acts
    // as a low-pass filter, so there is no aliasing, but it is much slower.
    Harmonic
};

struct DowngradeOptions
{
    int nsideOut = 0;
    DowngradeMethod method = DowngradeMethod::Pixel;

    // Pixel: set a pixel to UNSEEN when any of its sub-pixels is UNSEEN (else
    // only the valid ones are averaged)
    bool pessimistic = false;

    // Harmonic: iterations of map2alm (refinement of the quadrature), the
    // l_max of the analysis (<= 3 * nside - 1 of the input), and whether the
    // output pixels that were mostly UNSEEN in the input stay UNSEEN (the
    // transform itself fills them, with the UNSEEN pixels taken as zero)
    int iterations = 3;
    int lmaxIn     = 0; // 0 = 3 * nside_in - 1
    bool keepUnseen = true;
};

struct DowngradeResult
{
    std::shared_ptr<Healpix_Map<float>> map; // null on failure
    QStringList notes;                       // what the user should know
    QString error;
};

// Lowers the resolution of `in` to options.nsideOut (a power of two, smaller
// than the input's). The result has the input's pixel ordering. Blocking
// (the harmonic method can take minutes for large maps): run it on a worker
// thread.
DowngradeResult
downgradeMap(const Healpix_Map<float> &in, const DowngradeOptions &options);

// Rough memory the harmonic method needs, in bytes (a double copy of the map
// plus the a_lm), for the dialog's warning
double
harmonicMemoryEstimate(int nsideIn, int lmaxIn);
