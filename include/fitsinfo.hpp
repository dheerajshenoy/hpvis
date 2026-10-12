#pragma once

#include <QList>
#include <QString>

class fitshandle;

// One numeric column of a FITS table (index is 1-based, as the HEALPix
// readers expect)
struct FitsColumn
{
    int index = 1;
    QString name;
};

// One HDU of a FITS file (index is 1-based, as fitshandle::goto_hdu expects).
// Only HDUs that can hold a map are listed: tables with the NSIDE and
// ORDERING keywords and at least one numeric column with 12 * nside^2 values.
struct FitsHdu
{
    int index = 1;
    QString name; // EXTNAME, may be empty
    QList<FitsColumn> columns;
};

struct FitsInfo
{
    int hduCount = 0; // all HDUs in the file, including the primary one
    QList<FitsHdu> hdus;

    const FitsHdu *find(int hduIndex) const;
};

// Walks every HDU of the (already open) file. Leaves the handle on the last
// HDU, so call goto_hdu() afterwards.
FitsInfo
scanFits(fitshandle &file);
