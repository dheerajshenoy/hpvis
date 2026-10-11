#include "mapoperation.hpp"

#include "fitsinfo.hpp"
#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "mapexpression.hpp"

#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int kMaxSupportedNside = 8192; // as in View

struct Loaded
{
    std::shared_ptr<const Healpix_Map<float>> map;
    SkyFrame frame = SkyFrame::Galactic;
    QString label; // for messages
};

Loaded
load(const OperandSource &op)
{
    Loaded out;
    if (op.map)
    {
        out.map   = op.map;
        out.frame = op.frame;
        out.label = op.name;
        return out;
    }

    const QString file = QFileInfo(op.path).fileName();
    if (!QFileInfo::exists(op.path))
        throw PlanckError(
            QString("%1: the file %2 does not exist").arg(op.name, file)
                .toStdString().c_str());
    fitshandle fits;
    try
    {
        fits.open(op.path.toStdString());
    }
    catch (const PlanckError &)
    {
        throw PlanckError(QString("%1: cannot open %2 as a FITS file")
                              .arg(op.name, file)
                              .toStdString()
                              .c_str());
    }

    int hdu = op.hdu, column = op.column;
    if (hdu <= 0)
    {
        const FitsInfo info = scanFits(fits);
        if (info.hdus.isEmpty())
            throw PlanckError(
                ("no HEALPix map found in " + file.toStdString()).c_str());
        hdu    = info.hdus.first().index;
        column = column > 0 ? column : info.hdus.first().columns.first().index;
    }
    if (column <= 0)
        column = 1;

    fits.goto_hdu(hdu);
    if (fits.key_present("COORDSYS"))
        out.frame = skyframe::fromCoordSys(
            QString::fromStdString(fits.get_key<std::string>("COORDSYS")));

    const int nside = fits.get_key<int>("NSIDE");
    if (nside > kMaxSupportedNside)
        throw PlanckError(
            ("nside " + std::to_string(nside) + " is not supported").c_str());

    auto map = std::make_shared<Healpix_Map<float>>();
    read_Healpix_map_from_fits(fits, *map, column);
    out.map   = std::move(map);
    out.label = QString("%1 (%2, HDU %3, column %4)")
                    .arg(op.name, file)
                    .arg(hdu)
                    .arg(column);
    return out;
}

inline bool
isValid(float v)
{
    return std::isfinite(v) && v > -1.0e30f;
}
} // namespace

OperationResult
runOperation(const std::vector<OperandSource> &operands,
             const QString &expression, ResolutionPolicy policy)
{
    OperationResult result;
    try
    {
        if (operands.empty())
            throw PlanckError("add at least one map");

        QStringList names;
        for (const OperandSource &op : operands)
            names << op.name;

        MapExpression expr;
        QString parseError;
        if (!expr.compile(expression, names, &parseError))
            throw PlanckError(parseError.toStdString().c_str());

        // Read the inputs the expression actually uses (the others are not
        // needed, and may not even match)
        std::vector<Loaded> loaded(operands.size());
        for (int index : expr.usedVariables())
            loaded[index] = load(operands[index]);

        std::vector<int> used = expr.usedVariables();
        if (used.empty())
            throw PlanckError("the expression uses no map");

        // Output: the first used map's ordering; the resolution by the policy
        const Loaded &first = loaded[used.front()];
        const Healpix_Ordering_Scheme scheme = first.map->Scheme();
        int nside                            = int(first.map->Nside());
        for (int index : used)
        {
            const int n = int(loaded[index].map->Nside());
            if (policy == ResolutionPolicy::Highest)
                nside = std::max(nside, n);
            else if (policy == ResolutionPolicy::Lowest)
                nside = std::min(nside, n);
        }

        // Bring every used map to that nside and ordering
        std::vector<std::shared_ptr<const Healpix_Map<float>>> aligned(
            operands.size());
        for (int index : used)
        {
            const auto &src = loaded[index].map;
            if (int(src->Nside()) == nside && src->Scheme() == scheme)
            {
                aligned[index] = src;
                continue;
            }
            auto copy = std::make_shared<Healpix_Map<float>>(nside, scheme,
                                                             SET_NSIDE);
            copy->Import(*src);
            aligned[index] = copy;
            if (int(src->Nside()) != nside)
                result.notes << QString("%1: nside %2 %3 to %4")
                                    .arg(operands[index].name)
                                    .arg(src->Nside())
                                    .arg(int(src->Nside()) < nside
                                             ? "upgraded"
                                             : "degraded")
                                    .arg(nside);
        }

        for (int index : used)
            if (loaded[index].frame != first.frame)
                result.notes << QString("%1 is in %2 coordinates but %3 is in "
                                        "%4; the pixels are combined as they "
                                        "are")
                                    .arg(operands[index].name,
                                         skyframe::name(loaded[index].frame),
                                         operands[used.front()].name,
                                         skyframe::name(first.frame));

        // Evaluate
        auto out = std::make_shared<Healpix_Map<float>>(nside, scheme, SET_NSIDE);
        const long npix = out->Npix();
        const float undef = float(Healpix_undef);
        long valid        = 0;

#pragma omp parallel for schedule(static) reduction(+ : valid)
        for (long p = 0; p < npix; ++p)
        {
            double vars[32] = {};
            bool ok         = true;
            for (int index : used)
            {
                const float v = (*aligned[index])[p];
                if (!isValid(v))
                {
                    ok = false;
                    break;
                }
                vars[index] = v;
            }
            double value = 0.0;
            if (ok && expr.evaluate(vars, value))
            {
                (*out)[p] = float(value);
                ++valid;
            }
            else
                (*out)[p] = undef;
        }

        result.map         = std::move(out);
        result.frame       = first.frame;
        result.validPixels = valid;
    }
    catch (const PlanckError &e)
    {
        result.error = e.what();
    }
    catch (const std::exception &e)
    {
        result.error = e.what();
    }
    return result;
}
