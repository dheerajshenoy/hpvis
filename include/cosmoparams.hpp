#pragma once

// The parameters CLASS needs for the theory spectrum. The defaults are the
// Planck 2018 best fit (TT,TE,EE+lowE+lensing, Planck 2018 results VI, table 2)
// of base LCDM with one 0.06 eV neutrino, no tensors, curvature or evolving
// dark energy.
struct CosmoParams
{
    // Base LCDM
    double H0    = 67.36;   // km/s/Mpc
    double ombh2 = 0.02237; // physical baryon density
    double omch2 = 0.1200;  // physical cold dark matter density
    double tau   = 0.0544;  // optical depth to reionization
    double lnAs  = 3.044;   // ln(10^10 A_s)
    double ns    = 0.9649;  // scalar spectral index
    // Primordial
    double alphaS = 0.0;    // running of the tilt, dn_s / dln k
    double r      = 0.0;    // tensor-to-scalar ratio
    double nt     = 0.0;    // tensor tilt; ignored when ntConsistency
    bool ntConsistency = true; // n_t = -r / 8
    // Neutrinos
    double neff = 3.046;    // effective number of neutrino species
    double mnu  = 0.06;     // sum of the neutrino masses, eV (one species)
    // Geometry and dark energy
    double omk = 0.0;       // curvature density
    double w0  = -1.0;      // dark energy equation of state, w = w0 + wa (1-a)
    double wa  = 0.0;
    // Thermal history
    double tcmb = 2.7255;   // K
    double yhe  = 0.245;    // helium mass fraction; ignored when yheFromBbn
    bool yheFromBbn = true; // taken from BBN, given the baryon density

    bool operator==(const CosmoParams &) const = default;
    bool isDefault() const { return *this == CosmoParams(); }
};

// The real-valued parameters by name: the settings keys and the cache key
struct CosmoField
{
    const char *key;
    double CosmoParams::*member;
};

inline constexpr CosmoField cosmoFields[] = {
    {"H0", &CosmoParams::H0},         {"ombh2", &CosmoParams::ombh2},
    {"omch2", &CosmoParams::omch2},   {"tau", &CosmoParams::tau},
    {"lnAs", &CosmoParams::lnAs},     {"ns", &CosmoParams::ns},
    {"alphaS", &CosmoParams::alphaS}, {"r", &CosmoParams::r},
    {"nt", &CosmoParams::nt},         {"neff", &CosmoParams::neff},
    {"mnu", &CosmoParams::mnu},       {"omk", &CosmoParams::omk},
    {"w0", &CosmoParams::w0},         {"wa", &CosmoParams::wa},
    {"tcmb", &CosmoParams::tcmb},     {"yhe", &CosmoParams::yhe},
};
