#include "beamwindow.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "resolution.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>

BeamWindow
gaussianBeam(double fwhmArcmin, int lmax)
{
    if (!(fwhmArcmin > 0.0))
        throw std::runtime_error("the beam FWHM must be above zero");
    const double fwhm  = fwhmArcmin / 60.0 * std::numbers::pi / 180.0;
    const double sigma = fwhm / (2.0 * std::sqrt(2.0 * std::log(2.0)));
    const double pol   = std::exp(2.0 * sigma * sigma);
    BeamWindow b;
    b.T.resize(size_t(lmax) + 1);
    b.P.resize(size_t(lmax) + 1);
    for (int l = 0; l <= lmax; ++l)
    {
        b.T[size_t(l)] = std::exp(-0.5 * double(l) * (l + 1) * sigma * sigma);
        b.P[size_t(l)] = b.T[size_t(l)] * pol;
    }
    return b;
}

BeamWindow
readBeamWindow(const QString &path)
{
    const QString lower = path.toLower();
    BeamWindow b;
    if (lower.endsWith(".fits") || lower.endsWith(".fit")
        || lower.endsWith(".fts"))
    {
        try
        {
            fitshandle f;
            f.open(path.toStdString());
            for (int hdu = 1; hdu <= f.num_hdus(); ++hdu)
            {
                f.goto_hdu(hdu);
                if (!f.key_present("TFIELDS") || f.ncols() < 1)
                    continue;
                int tCol = 1, pCol = 0;
                bool tNamed = false;
                for (int c = 1; c <= f.ncols(); ++c)
                {
                    const QString n
                        = QString::fromStdString(f.colname(c)).trimmed().toUpper();
                    if (!tNamed && (n.startsWith("TEMP") || n == "TT" || n == "T"))
                    {
                        tCol   = c;
                        tNamed = true;
                    }
                    else if (!pCol
                             && (n.startsWith("POL") || n == "EE" || n == "E"
                                 || n.startsWith("GRAD") || n.startsWith("ELEC")))
                        pCol = c;
                }
                f.read_entire_column(tCol, b.T);
                if (pCol && pCol != tCol)
                    f.read_entire_column(pCol, b.P);
                break;
            }
        }
        catch (const PlanckError &e)
        {
            throw std::runtime_error(std::string("cannot read ")
                                     + path.toStdString() + ": " + e.what());
        }
        if (b.T.empty())
            throw std::runtime_error(path.toStdString()
                                     + " has no table column");
    }
    else
        b.T = readPixelWindow(path);
    if (b.P.empty())
        b.P = b.T;
    return b;
}
