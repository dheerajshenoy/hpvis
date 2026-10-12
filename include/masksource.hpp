#pragma once

#include "healpix_map.h"

#include <QString>
#include <memory>

// Where a mask comes from: a column of a FITS file (hdu / column are 1-based,
// 0 = the first map HDU / column), a row of a .npy file (column), or a map in
// memory (e.g. the map of another tab)
struct MaskSource
{
    QString path;
    int hdu    = 0;
    int column = 0;
    QString label; // shown in the Masks menu; empty = derived from the file
    std::shared_ptr<const Healpix_Map<float>> map;

    // Identifies the source, so the same one is not loaded twice
    QString key() const;
};
