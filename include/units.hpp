#pragma once

#include <QString>
#include <optional>

// Units a CMB map can be in, and conversions between them. A temperature is
// either thermodynamic (CMB: the temperature of a blackbody at T_CMB with the
// same brightness change) or Rayleigh-Jeans (RJ); Jy/sr is the brightness.
namespace units
{
enum class Unit
{
    K_CMB,
    mK_CMB,
    uK_CMB,
    K_RJ,
    mK_RJ,
    uK_RJ,
    Jy_sr,
    MJy_sr,
    Count_
};

constexpr double kTcmb = 2.7255; // K (Fixsen 2009)

// "uK_CMB", "MJy/sr", ... as shown in the interface and written to FITS
QString label(Unit unit);

// The unit a FITS TUNITn value stands for ("K", "uK", "muK", "mK_CMB",
// "K_RJ", "MJy/sr", "MJy sr-1", ...). A plain K, mK or uK is taken to be a
// CMB (thermodynamic) temperature, as in the Planck files.
std::optional<Unit> fromFits(const QString &tunit);

// True for K_CMB, mK_CMB and uK_CMB
bool isCmbTemperature(Unit unit);

// Whether converting needs the frequency (anything involving RJ or Jy/sr)
bool needsFrequency(Unit from, Unit to);

// value_to = value_from * factor, at `freqGHz`. NaN when a frequency is
// needed and freqGHz is not positive.
double factor(Unit from, Unit to, double freqGHz);
} // namespace units
