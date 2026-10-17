#pragma once

#include "pixelwindow.hpp"

#include <QString>

// A beam window function B_l (the beam's transform, an amplitude as in
// HEALPix's gaussian_beam: a map smoothed by it has a_lm * B_l, its spectrum
// B_l^2 C_l). The temperature and polarization beams differ. Held in a
// PixelWindow, whose factors applyPixelWindow() applies the same way.
using BeamWindow = PixelWindow;

// Gaussian beam of `fwhmArcmin` for l = 0..lmax, as HEALPix's smoothWithGauss
// applies it: T gets exp(-l(l+1) sigma^2 / 2), E and B the same times
// exp(2 sigma^2) (the spin-2 beam). Throws std::runtime_error for a FWHM <= 0.
BeamWindow
gaussianBeam(double fwhmArcmin, int lmax);

// Reads B_l from a file. FITS tables: the temperature beam from the column
// named TEMPERATURE / TT / T (else the first one) and the polarization beam from
// POLARIZATION / EE / E / GRAD / ELECTRIC, when there is one; without it the
// polarization beam is the temperature one. .npy and text files (see
// readPixelWindow): one beam, used for both. Throws std::runtime_error.
BeamWindow
readBeamWindow(const QString &path);
