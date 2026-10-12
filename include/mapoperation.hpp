#pragma once

#include "healpix_map.h"
#include "skyframe.hpp"

#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

// One input of an operation, known by `name` in the expression
struct OperandSource
{
    QString name; // "A", "B", ...
    // Either a FITS file (hdu / column are 1-based, 0 = the first map HDU /
    // column)...
    QString path;
    int hdu    = 0;
    int column = 0;
    // ...or a map that only exists in memory (a result of an earlier operation)
    std::shared_ptr<const Healpix_Map<float>> map;
    SkyFrame frame = SkyFrame::Galactic; // of `map`
};

// When the inputs have different nsides they are brought to one of them
enum class ResolutionPolicy
{
    Highest,
    Lowest,
    First
};

struct OperationResult
{
    std::shared_ptr<Healpix_Map<float>> map; // null on failure
    SkyFrame frame   = SkyFrame::Galactic;   // of the first input
    long validPixels = 0;
    QStringList notes; // things the user should know (resampling, frames)
    QString error;
};

// Reads the inputs, matches their resolution and ordering (the result uses the
// first input's ordering), and evaluates `expression` for every pixel. A pixel
// is UNSEEN in the result when an input it uses is UNSEEN or the expression
// has no finite value there. Blocking: run it on a worker thread.
OperationResult
runOperation(const std::vector<OperandSource> &operands,
             const QString &expression, ResolutionPolicy policy);
