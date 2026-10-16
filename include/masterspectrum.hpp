#pragma once

#include "healpix_cxx/healpix_map.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Power spectra of a masked sky, corrected for the mask with the MASTER
// method (Hivon et al. 2002) as implemented by NaMaster: the pseudo-C_l of the
// mask-weighted field, the mode-coupling matrix of the mask, binned into
// bandpowers and inverted. E and B modes can be purified (Smith & Zaldarriaga
// 2007) so that the mask does not leak E into B.
//
// NaMaster 3's C library provides the coupling coefficients and the mask
// apodization; the rest of the workflow (fields, purification, binning,
// decoupling) lives here, following pymaster.
namespace master
{
enum class Apodization
{
    None,
    C1,
    C2,
    Smooth
};

struct Options
{
    int lmax       = 0;
    int bandwidth  = 16; // multipoles per bandpower, from l = 2
    Apodization apodization = Apodization::None;
    double apodizationScale = 1.0; // degrees
    bool purifyE    = false; // needs an apodized mask
    bool purifyB    = false;
    int iterations  = 1;    // refinement rounds of the field transforms
};

// What the computation needs: RING-ordered maps with UNSEEN / NaN pixels
// already set to zero, and the weight (mask) of every pixel
struct Input
{
    Healpix_Map<double> T, Q, U;
    bool polarized = false;
    Healpix_Map<double> weights;
};

// The response of the bandpowers to the true spectra: bandpowers = windows x
// C_l, with the C_l of the spectra of the group stacked per multipole
// (index l * ncls + i)
struct Windows
{
    int ncls   = 0; // spectra in the group
    int nbands = 0;
    int lmax   = 0;
    std::vector<double> data; // [nbands * ncls] rows of [(lmax + 1) * ncls]
};

struct Result
{
    std::vector<double> ell; // effective multipole of every bandpower
    // Bandpowers C_b (empty for spectra that were not computed)
    std::vector<double> TT, EE, BB, TE, TB, EB;

    double fsky  = 0.0; // mean of the squared weights
    int lmax     = 0;
    int bandwidth = 0;
    bool purifyE = false; // purification that was really applied
    bool purifyB = false;
    // Largest condition number of the binned coupling matrices; large values
    // mean the inversion amplifies noise (mask too small, bands too narrow)
    double conditionNumber = 1.0;

    Windows windowsTT, windowsTE, windowsEE;
};

// Called while computing. stage() is called before each stage and may throw
// to abort; done() after it. total() gives the number of stages up front.
struct Hooks
{
    std::function<void(int)> total = [](int) {};
    std::function<void(const char *)> stage = [](const char *) {};
    std::function<void()> done = [] {};
};

// Number of stages compute() reports
int
stageCount(const Options &options, bool polarized);

// Throws std::runtime_error with a readable message on failure
Result
compute(const Input &input, const Options &options, const Hooks &hooks);

// Spectra per multipole, C_l for l = 0..size-1 (missing ones count as zero)
struct TheoryCl
{
    std::vector<double> TT, EE, BB, TE;
};

// The bandpowers the true spectra `theory` would give, through the windows of
// the result (what the decoupled spectrum should match on average)
Result
binTheory(const Result &computed, const TheoryCl &theory);
} // namespace master
