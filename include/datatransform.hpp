#pragma once

#include "healpix_map.h"

#include <QString>

namespace datatransform
{
enum class Kind
{
    Log,
    Asinh,
    Atan,
    Tanh,
    Power,
    Exp,
    HistogramEqualize,
    Standardize, // (x - mean) / std
    MinMax,      // (x - min) / (max - min)
    Count_
};

struct Params
{
    double mu    = 0.0; // centre of the transform
    double sigma = 1.0; // scale: the transform acts on (x - mu) / sigma
    double power = 2.0; // exponent of Power
};

QString label(Kind kind);
// One-line formula, for tooltips and the dialog
QString formula(Kind kind);
bool usesMu(Kind kind);
bool usesSigma(Kind kind);
bool usesPower(Kind kind);

// Transforms the valid pixels of `map` in place. A pixel becomes UNSEEN when
// the transform has no finite value there (e.g. the log of a negative number);
// UNSEEN pixels stay UNSEEN. Returns how many valid pixels were lost that way.
// Blocking: run it on a worker thread.
long apply(Healpix_Map<float> &map, Kind kind, const Params &params);

// Mean and standard deviation of the valid pixels (for sensible defaults)
void meanStd(const Healpix_Map<float> &map, double &mean, double &std);
} // namespace datatransform
