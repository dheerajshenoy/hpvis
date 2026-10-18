#pragma once

#include "healpix_map.h"
#include "skyframe.hpp"

#include <QString>
#include <memory>
#include <vector>

namespace maskbuilder
{
enum class RuleType
{
    Threshold,   // keep lo <= value <= hi
    GalacticCut, // keep |b| >= latitude
    Disc         // exclude (or keep) the pixels within a radius of a centre
};

// How a rule joins what the rules above it kept
enum class Combine
{
    And,      // keep only what both keep
    Or,       // keep what either keeps
    Subtract  // drop what this rule keeps
};

struct Rule
{
    RuleType type   = RuleType::Threshold;
    Combine combine = Combine::And; // ignored for the first rule
    bool enabled    = true;

    double lo = 0.0, hi = 0.0; // Threshold
    double latitude = 20.0;    // GalacticCut: degrees
    // Disc: centre in the map's own frame (degrees), radius in degrees
    double lon = 0.0, lat = 0.0, radius = 10.0;
    bool keepInside = false; // false: the disc is cut out

    QString summary(SkyFrame frame) const;
};

struct Result
{
    std::shared_ptr<Healpix_Map<float>> mask; // weights in [0, 1]; null on error
    // Sky fractions: sum of w^k over the pixels / Npix, for k = 1, 2, 4
    double fsky = 0.0, fsky2 = 0.0, fsky4 = 0.0;
    QString error;
};

// Builds the mask of `map` (its ordering and nside) from the rules, in order.
// With apodFwhmDeg > 0 the edges get a smooth Gaussian taper of that width.
// Blocking: run it on a worker thread.
Result build(const Healpix_Map<float> &map, SkyFrame frame,
             const std::vector<Rule> &rules, double apodFwhmDeg);
} // namespace maskbuilder
