#pragma once

#include "healpix_map.h"

#include <QString>
#include <QStringList>
#include <memory>

enum class HarmonicOp
{
    Laplacian, // nabla^2 T
    Gradient,  // |nabla T|
    EB         // E and B maps from the Q and U maps
};

struct HarmonicOptions
{
    HarmonicOp op   = HarmonicOp::Laplacian;
    int iterations  = 3;    // refinements of the map2alm quadrature
    int lmax        = 0;    // 0 = 3 * nside - 1
    bool keepUnseen = true; // UNSEEN input pixels stay UNSEEN
    // EB only: the T, Q and U columns are read from this file and HDU
    QString path;
    int hdu = 0;
};

struct HarmonicResult
{
    std::shared_ptr<Healpix_Map<float>> map;  // Laplacian, gradient or E
    std::shared_ptr<Healpix_Map<float>> mapB; // only for EB
    QStringList notes;
    QString error;
};

// Works in the spherical harmonics (map2alm, a change of the a_lm, alm2map).
// Results have the input's nside and ordering; UNSEEN / NaN pixels are taken as
// zero in the transform. Blocking: run it on a worker thread.
HarmonicResult
harmonicMap(const Healpix_Map<float> &in, const HarmonicOptions &options);
