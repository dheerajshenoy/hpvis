#include "argparse.hpp"
#include "unseen.hpp"
#include "parallel.hpp"
#include "fitsinfo.hpp"
#include "hpvis.hpp"
#include "npyreader.hpp"
#include "openglwidget.hpp"

#include <QApplication>
#include <algorithm>
#include <cmath>
#include <fitshandle.h>
#include <healpix_map.h>
#include <healpix_map_fitsio.h>
#include <iostream>
#include <limits>

void
addArguments(argparse::ArgumentParser &parser)
{
    parser.add_argument("files").remaining().metavar("FILE").help(
        "FITS, .npy or .npz file(s) to open");

    // Data selection (1-based, as shown in the toolbar)
    parser.add_argument("-H", "--hdu")
        .metavar("HDU")
        .scan<'i', int>()
        .help("HDU to visualize (default: first map HDU; for a .npz, the "
               "array number shown by --info; ignored for .npy)");

    parser.add_argument("-c", "--column")
        .metavar("COLUMN")
        .help("Column to visualize, by number or name such as TT (default: "
              "first column; for a 2-D .npy / .npz array, the row number)");

    // Appearance
    parser.add_argument("-m", "--colormap")
        .metavar("COLORMAP")
        .help("Colormap, e.g. viridis, inferno, jet (default: from settings)");

    parser.add_argument("-r", "--reverse").flag().help("Reverse the colormap");

    parser.add_argument("-s", "--scale")
        .choices("linear", "log")
        .metavar("SCALE")
        .help("Colormap scale: linear, log or asinh");

    parser.add_argument("--min").metavar("VALUE").scan<'g', double>().help(
        "Lower end of the color range");

    parser.add_argument("--max").metavar("VALUE").scan<'g', double>().help(
        "Upper end of the color range");

    parser.add_argument("-p", "--projection")
        .choices("mollweide", "equirectangular", "orthographic",
                 "azimuthal_equidistant", "gnomonic", "mercator", "sinusoidal",
                 "lambert_cylindrical", "gall_peters", "stereographic",
                 "hammer")
        .metavar("PROJECTION")
        .help("Projection: mollweide, equirectangular, orthographic, "
              "azimuthal_equidistant, gnomonic, mercator, sinusoidal, "
              "lambert_cylindrical, gall_peters, stereographic or hammer");

    parser.add_argument("-o", "--orientation")
        .choices("free", "galactic", "ecliptic", "equatorial")
        .metavar("ORIENTATION")
        .help("Frame to show the sky in: galactic, ecliptic or equatorial "
              "(north stays up), or free (the map's own frame, default)");

    parser.add_argument("--view")
        .choices("inside", "outside")
        .metavar("VIEW")
        .help("Perspective: inside (the sky, longitude to the left) or "
              "outside (like a globe, mirrored)");

    parser.add_argument("-g", "--graticule")
        .flag()
        .help("Show the coordinate grid");

    parser.add_argument("--pixel-grid")
        .metavar("NSIDE")
        .scan<'i', int>()
        .help("Overlay the boundaries of the HEALPix pixels of this nside "
              "(rounded to a power of two)");

    parser.add_argument("--mask").metavar("FILE").append().help(
        "Mask file to apply (can be given more than once)");

    // Non-interactive modes
    parser.add_argument("-e", "--export")
        .metavar("FILENAME")
        .help("Save the view of the first file as an image and exit");

    parser.add_argument("-i", "--info")
        .flag()
        .help("Print the map HDUs and columns of the files and exit");

    parser.add_argument("--stats").flag().help(
        "Print min, max, mean, rms and std of the files and exit");
}

// --info: list the HDUs and columns that can be shown
static void
printInfo(const QString &path)
{
    if (isNumpyPath(path.toStdString()))
    {
        const FitsInfo info = scanNumpy(path.toStdString());
        const bool npz      = info.kind == MapFileKind::Npz;
        std::cout << path.toStdString() << ": "
                  << (npz ? ".npz file, " : ".npy file, ") << info.hdus.size()
                  << (npz ? " array(s) with a map\n" : " map array\n");
        for (const FitsHdu &h : info.hdus)
        {
            if (npz)
                std::cout << "  array " << h.index << " ("
                          << h.name.toStdString() << ")\n";
            std::cout << "    " << h.columns.size() << " row(s)"
                      << (npz ? ", use --hdu and --column to pick\n" : "\n");
        }
        return;
    }
    fitshandle file;
    file.open(path.toStdString());
    const FitsInfo info = scanFits(file);

    std::cout << path.toStdString() << ": " << info.hduCount << " HDU(s), "
              << info.hdus.size() << " with a map\n";
    for (const FitsHdu &h : info.hdus)
    {
        std::cout << "  HDU " << h.index;
        if (!h.name.isEmpty())
            std::cout << " (" << h.name.toStdString() << ")";
        std::cout << "\n";
        for (const FitsColumn &c : h.columns)
            std::cout << "    column " << c.index << ": "
                      << c.name.toStdString() << "\n";
    }
}

// --stats: statistics over the valid pixels of the selected HDU / column
static void
printStats(const QString &path, int hdu, int column, const QString &columnName)
{
    Healpix_Map<float> map;
    if (isNumpyPath(path.toStdString()))
    {
        if (!columnName.isEmpty())
            throw std::runtime_error("rows of a NumPy array have no names, "
                                     "give a number");
        column = std::max(column, 1);
        // The user asked for this file on the command line: no size prompt
        if (isNpzPath(path.toStdString()))
            npzApproveLarge(path.toStdString());
        map = std::move(*readNpyMap(path.toStdString(), column,
                                    kMaxSupportedNside, nullptr, hdu));
    }
    else
    {
        fitshandle file;
        file.open(path.toStdString());
        const FitsInfo info = scanFits(file);
        if (info.hdus.isEmpty())
            throw std::runtime_error("no HEALPix map found in "
                                     + path.toStdString());

        if (hdu <= 0)
            hdu = info.hdus.first().index;
        if (!columnName.isEmpty())
        {
            const FitsHdu *h = info.find(hdu);
            if (!h)
                throw std::runtime_error("HDU " + std::to_string(hdu)
                                         + " does not hold a map");
            column = findColumn(*h, columnName);
            if (!column)
                throw std::runtime_error(
                    "no column named " + columnName.toStdString()
                    + " (available: " + columnNames(*h).toStdString() + ")");
        }
        if (column <= 0)
            column = 1;
        file.goto_hdu(hdu);
        read_Healpix_map_from_fits(file, map, column);
    }

    struct Acc
    {
        double sum = 0, sum2 = 0;
        double min = std::numeric_limits<double>::max();
        double max = std::numeric_limits<double>::lowest();
        long n     = 0;
    };
    const Acc acc = parallel::reduceRange<Acc>(
        map.Npix(),
        [&](long begin, long end)
    {
        Acc a;
        for (long i = begin; i < end; ++i)
        {
            const float v = map[i];
            if (!isSeen(v))
                continue;
            a.min = std::min<double>(a.min, v);
            a.max = std::max<double>(a.max, v);
            a.sum += v;
            a.sum2 += double(v) * v;
            ++a.n;
        }
        return a;
    }, [](Acc &t, const Acc &p)
    {
        t.min = std::min(t.min, p.min);
        t.max = std::max(t.max, p.max);
        t.sum += p.sum;
        t.sum2 += p.sum2;
        t.n += p.n;
    });
    const double sum = acc.sum, sum2 = acc.sum2;
    const double min = acc.min, max = acc.max;
    const long n     = acc.n;
    if (n == 0)
        throw std::runtime_error("map has no valid pixels");

    const double mean = sum / n;
    const double rms  = std::sqrt(sum2 / n);
    const double std  = std::sqrt(std::max(0.0, sum2 / n - mean * mean));

    std::cout << path.toStdString();
    if (isNpyPath(path.toStdString()))
        std::cout << " (column " << column << ")\n";
    else if (isNpzPath(path.toStdString()))
        std::cout << " (array " << (hdu > 0 ? std::to_string(hdu) : "1st")
                  << ", column " << column << ")\n";
    else
        std::cout << " (HDU " << hdu << ", column " << column << ")\n";
    std::cout << "  nside   " << map.Nside() << "\n"
              << "  npix    " << map.Npix() << "\n"
              << "  valid   " << n << "\n"
              << "  min     " << min << "\n"
              << "  max     " << max << "\n"
              << "  mean    " << mean << "\n"
              << "  rms     " << rms << "\n"
              << "  std     " << std << "\n";
}

int
main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    argparse::ArgumentParser parser(PROJECT_NAME, PROJECT_VERSION,
                                    argparse::default_arguments::all);
    addArguments(parser);
    try
    {
        parser.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        exit(1);
    }

    // --info / --stats print and exit without opening a window
    if (parser.get<bool>("--info") || parser.get<bool>("--stats"))
    {
        const auto files = parser.get<std::vector<std::string>>("files");
        if (files.empty())
        {
            std::cerr << "No input files given\n";
            return 1;
        }
        int status = 0;
        for (const auto &f : files)
        {
            try
            {
                const QString path = QString::fromStdString(f);
                if (parser.get<bool>("--info"))
                    printInfo(path);
                if (parser.get<bool>("--stats"))
                {
                    int column = 0;
                    QString columnName;
                    if (auto c = parser.present<std::string>("--column"))
                    {
                        bool isNumber = false;
                        column = QString::fromStdString(*c).toInt(&isNumber);
                        if (!isNumber)
                        {
                            column     = 0;
                            columnName = QString::fromStdString(*c);
                        }
                    }
                    printStats(path, parser.present<int>("--hdu").value_or(0),
                               column, columnName);
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << f << ": " << e.what() << "\n";
                status = 1;
            }
        }
        return status;
    }

    QApplication app(argc, argv);
    // Names the per-user data directory (e.g. ~/.local/share/hpvis)
    QCoreApplication::setApplicationName(PROJECT_NAME);
    // QSettings needs both names (config goes to ~/.config/hpvis/)
    QCoreApplication::setOrganizationName(PROJECT_NAME);
    QCoreApplication::setOrganizationDomain(
        "dheerajshenoy.github.io/" PROJECT_NAME);
    QCoreApplication::setApplicationName("hpvis");

    HpVis view(parser);

    app.exec();
}
