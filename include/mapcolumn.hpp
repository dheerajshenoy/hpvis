#pragma once

#include "healpix_map.h"

#include <QString>
#include <memory>

// One column of a map that was computed (not read from a file): the tab shows
// one at a time, like the columns of a FITS HDU
struct ComputedColumn
{
    QString name;
    QString unit;
    std::shared_ptr<const Healpix_Map<float>> map;
};
