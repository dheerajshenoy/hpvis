#include "argparse.hpp"
#include "fitsinfo.hpp"
#include "hpvis.hpp"

#include <QApplication>
#include <algorithm>
#include <cmath>
#include <fitshandle.h>
#include <healpix_map.h>
#include <healpix_map_fitsio.h>
#include <iostream>

void
addArguments(argparse::ArgumentParser &parser)
{
    parser.add_argument("files").remaining().metavar("FILE").help(
        "FITS file(s) to open");

    // Data selection (1-based, as shown in the toolbar)
    parser.add_argument("-H", "--hdu")
        .metavar("HDU")
        .scan<'i', int>()
        .help("HDU to visualize (default: first map HDU)");

    parser.add_argument("-c", "--column")
        .metavar("COLUMN")
        .scan<'i', int>()
        .help("Column to visualize (default: first column)");

    // Appearance
    parser.add_argument("-m", "--colormap")
        .metavar("COLORMAP")
        .help("Colormap, e.g. viridis, inferno, jet (default: from settings)");

    parser.add_argument("-r", "--reverse").flag().help("Reverse the colormap");

    parser.add_argument("-s", "--scale")
        .choices("linear", "log")
        .metavar("SCALE")
        .help("Colormap scale: linear or log");

    parser.add_argument("--min").metavar("VALUE").scan<'g', double>().help(
        "Lower end of the color range");

    parser.add_argument("--max").metavar("VALUE").scan<'g', double>().help(
        "Upper end of the color range");

    parser.add_argument("-p", "--projection")
        .choices("mollweide", "plate_carree", "orthographic",
                 "azimuthal_equidistant", "gnomonic")
        .metavar("PROJECTION")
        .help("Projection: mollweide, plate_carree, orthographic, "
              "azimuthal_equidistant or gnomonic");

    parser.add_argument("--view")
        .choices("inside", "outside")
        .metavar("VIEW")
        .help("Perspective: inside (the sky, longitude to the left) or "
              "outside (like a globe, mirrored)");

    parser.add_argument("-g", "--graticule")
        .flag()
        .help("Show the coordinate grid");

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
printStats(const QString &path, int hdu, int column)
{
    fitshandle file;
    file.open(path.toStdString());
    const FitsInfo info = scanFits(file);
    if (info.hdus.isEmpty())
        throw std::runtime_error("no HEALPix map found in "
                                 + path.toStdString());

    if (hdu <= 0)
        hdu = info.hdus.first().index;
    if (column <= 0)
        column = 1;
    file.goto_hdu(hdu);

    Healpix_Map<float> map;
    read_Healpix_map_from_fits(file, map, column);

    double sum = 0, sum2 = 0, min = 0, max = 0;
    long n = 0;
    for (long i = 0; i < map.Npix(); ++i)
    {
        const float v = map[i];
        if (!std::isfinite(v) || v <= -1e30f)
            continue;
        if (n == 0)
            min = max = v;
        min = std::min<double>(min, v);
        max = std::max<double>(max, v);
        sum += v;
        sum2 += double(v) * v;
        ++n;
    }
    if (n == 0)
        throw std::runtime_error("map has no valid pixels");

    const double mean = sum / n;
    const double rms  = std::sqrt(sum2 / n);
    const double std  = std::sqrt(std::max(0.0, sum2 / n - mean * mean));

    std::cout << path.toStdString() << " (HDU " << hdu << ", column " << column
              << ")\n"
              << "  nside   " << map.Nside() << "\n"
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
                    printStats(path, parser.present<int>("--hdu").value_or(0),
                               parser.present<int>("--column").value_or(0));
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
