#pragma once

#include "cosmoparams.hpp"

#include <vector>

// Lensed CMB spectra from CLASS, D_l = l(l+1) C_l / 2pi in uK^2, l = 0..lmax
struct ClassSpectra
{
    int lmax = 0;
    std::vector<double> TT, TE, EE, BB;
};

// Runs CLASS (base LCDM, one massive neutrino of 0.06 eV as in Planck 2018)
// for l up to lmax. Takes tens of seconds: run it on a worker thread. Throws
// std::runtime_error with CLASS's message.
ClassSpectra
computeClassSpectra(const CosmoParams &params, int lmax = 2508);
