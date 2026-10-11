#include "fitsinfo.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"

const FitsHdu *
FitsInfo::find(int hduIndex) const
{
    for (const FitsHdu &h : hdus)
        if (h.index == hduIndex)
            return &h;
    return nullptr;
}

FitsInfo
scanFits(fitshandle &file)
{
    FitsInfo info;
    info.hduCount = file.num_hdus();

    for (int hdu = 1; hdu <= info.hduCount; ++hdu)
    {
        file.goto_hdu(hdu);

        FitsHdu entry;
        entry.index = hdu;
        try
        {
            // The HEALPix readers need these keywords; anything else (an
            // index table, an image, ...) cannot be shown as a map
            if (!file.key_present("NSIDE") || !file.key_present("ORDERING"))
                continue;
            const int64 nside = file.get_key<int>("NSIDE");
            if (nside < 1)
                continue;
            const int64 npix = 12 * nside * nside;

            // Throws for images (the primary HDU is usually empty)
            const int ncols = file.ncols();
            for (int c = 1; c <= ncols; ++c)
            {
                const PDT type = file.coltype(c);
                if (type == PLANCK_STRING || type == PLANCK_BOOL)
                    continue;
                // A map column holds exactly one value per pixel
                if (file.nelems(c) != npix)
                    continue;
                entry.columns.append(
                    {c, QString::fromStdString(file.colname(c))});
            }
        }
        catch (const PlanckError &)
        {
            continue;
        }
        if (entry.columns.isEmpty())
            continue;

        try
        {
            // Check first: a failed get_key() prints an error to stderr even
            // when it is caught
            if (file.key_present("EXTNAME"))
                entry.name = QString::fromStdString(
                    file.get_key<std::string>("EXTNAME"));
        }
        catch (const PlanckError &)
        {
            // no EXTNAME: leave it empty
        }
        info.hdus.append(entry);
    }
    return info;
}
