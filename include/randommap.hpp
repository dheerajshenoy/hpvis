#pragma once

#include "healpix_map.h"
#include "theoryspectrum.hpp"

#include <QString>
#include <memory>

// Random maps: pixel noise, or a Gaussian realization of the CMB spectrum
// (like HEALPix's synfast).
enum class RandomKind
{
    GaussianNoise = 0, // mean + sigma * N(0, 1) in every pixel
    UniformNoise  = 1, // uniform in [a, b)
    CmbTemperature = 2, // T from the TT spectrum
    CmbPolarization = 3 // T, Q and U from TT, EE, BB and TE
};

struct RandomOptions
{
    RandomKind kind = RandomKind::GaussianNoise;
    long nside      = 256;
    unsigned seed   = 1;
    double a        = 0.0; // gaussian: mean, uniform: minimum
    double b        = 1.0; // gaussian: sigma, uniform: maximum
    // CMB: the theory to draw from (its unit is the unit of the map) and the
    // Gaussian beam to apply (arcminutes FWHM, 0 = none)
    TheorySource theory;
    double fwhmArcmin = 0.0;
};

struct RandomResult
{
    std::shared_ptr<Healpix_Map<float>> map;  // the T map (or the noise)
    std::shared_ptr<Healpix_Map<float>> mapQ; // polarization only
    std::shared_ptr<Healpix_Map<float>> mapU;
    QString error;
    int lmax = 0; // CMB: the highest multipole drawn
};

// Blocking, uses all cores: run it on a worker thread. The same options (the
// seed in particular) always give the same map, on any number of threads.
RandomResult
generateRandomMap(const RandomOptions &options);
