#pragma once

#include "healpix_map.h"

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

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

    // Harmonic: pixel window functions w_l (index = l), either may be empty
    // (none). The input map carries the window of its own nside: windowIn is
    // divided out, and windowOut (of the new nside) multiplied in, so the
    // result looks like a map observed at the lower resolution.
    std::vector<double> windowIn;
    std::vector<double> windowOut;
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

// Reads a pixel window function w_l (index = l, starting at l = 0) from
//  - a FITS table (HEALPix's pixel_window_n*.fits: the TEMPERATURE column, or
//    the first one),
//  - a .npy array (1-D, or the first row of a 2-D one),
//  - a text file: one value per line, or "l w_l" per line; blank lines and
//    lines starting with # are ignored, "," and ";" count as spaces.
// Throws std::runtime_error with a readable message.
std::vector<double>
readPixelWindow(const QString &path);

// Rough memory the harmonic method needs, in bytes (a double copy of the map
// plus the a_lm), for the dialog's warning
double
harmonicMemoryEstimate(int nsideIn, int lmaxIn);
