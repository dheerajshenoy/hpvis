#pragma once

#include "healpix_map.h"

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

struct SmoothOptions
{
    double fwhmRad = 0.0; // Gaussian beam FWHM; a negative value deconvolves
    // Custom beam window b_l (index = l, from l = 0); when not empty it is used
    // instead of the Gaussian. Applied to T, E and B alike.
    std::vector<double> beam;
    int iterations  = 3;    // refinements of the map2alm quadrature
    int lmax        = 0;    // 0 = 3 * nside - 1
    bool keepUnseen = true; // UNSEEN input pixels stay UNSEEN
    // Smooth Q and U as well (spin-2 transform of T, Q, U together). The
    // input is then read from `path` / `hdu`.
    bool polarization = false;
    QString path;
    int hdu = 0;
};

struct SmoothResult
{
    std::shared_ptr<Healpix_Map<float>> map;  // intensity; null on failure
    std::shared_ptr<Healpix_Map<float>> mapQ; // only when polarization
    std::shared_ptr<Healpix_Map<float>> mapU;
    QStringList notes;
    QString error;
};

// True when HDU `hdu` of the FITS file has a Q and a U column (and a third one
// for the intensity), i.e. a polarized map can be read from it
bool
hasPolarization(const QString &path, int hdu);

// Convolves `in` with a beam through the spherical harmonics (map2alm,
// multiply by the beam window, alm2map). With options.polarization the T, Q
// and U columns are read from the file and transformed together. Results
// have the input's nside and ordering. UNSEEN / NaN pixels are taken as zero
// in the transform. Blocking: run it on a worker thread.
SmoothResult
smoothMap(const Healpix_Map<float> &in, const SmoothOptions &options);
