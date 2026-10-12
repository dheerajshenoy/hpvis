#pragma once

#include "powerspectrumdock.hpp"

#include <QString>

// Where the theoretical CMB spectrum comes from (Settings > Theory)
enum class TheoryPreset
{
    Planck2018 = 0, // bundled best-fit table
    CustomFile = 1  // a CAMB / CLASS style text file chosen by the user
};

// Units the maps are in, to scale the theory (given in uK^2)
enum class TheoryUnit
{
    MicroKelvin = 0,
    MilliKelvin = 1,
    Kelvin      = 2
};

struct TheorySource
{
    TheoryPreset preset = TheoryPreset::Planck2018;
    QString file;
    TheoryUnit unit = TheoryUnit::MicroKelvin;

    bool operator==(const TheorySource &) const = default;
    QString label() const; // for the plot legend
};

// Reads the spectra as C_l (in the maps' unit) for l = 0..lmax. Files hold
// D_l = l(l+1) C_l / 2pi in uK^2, with columns named in a "#" header line
// (L, TT, TE, EE, BB, ...; Planck / CAMB style). Without a header the order is
// L TT TE EE BB. Throws std::runtime_error with a readable message.
Spectra
loadTheorySpectra(const TheorySource &source);
