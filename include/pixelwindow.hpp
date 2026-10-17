#pragma once

#include "powerspectrumdock.hpp"

#include <vector>

// The HEALPix pixel window functions of one nside, as healpy's
// sphtfunc.pixwin(nside, pol=True) gives them: w_l for l = 0..size()-1
struct PixelWindow
{
    std::vector<double> T; // temperature
    std::vector<double> P; // polarization (E and B)
};

// The windows for `nside` (a power of two from 2 to 8192), from the files that
// come with HEALPix (bundled in hpvis). With lmax >= 0 they are cut to
// l = 0..lmax. Throws std::runtime_error when there is no file for the nside or
// it is shorter than lmax. Safe to call from several threads.
PixelWindow
pixwin(int nside, int lmax = -1);

// Multiplies (or divides) the spectra by the window they get from the pixels:
//   TT       w_T^2
//   EE BB EB w_P^2
//   TE TB    w_T w_P
// A spectrum of C_l for every multipole is scaled multipole by multipole;
// bandpowers (spectra.ell not empty, `bandwidth` multipoles wide) by the mean
// factor of their band. Multipoles beyond the window become NaN.
void
applyPixelWindow(Spectra &spectra, const PixelWindow &window, bool multiply,
                 int bandwidth = 1);
