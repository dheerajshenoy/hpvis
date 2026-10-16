#include "view.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "npyreader.hpp"
#include "openglwidget.hpp"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QtConcurrent>
#include <cmath>
#include <memory>

View::View(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    m_glWidget          = new OpenGLWidget(this);
    layout->addWidget(m_glWidget);

    layout->setContentsMargins(0, 0, 0, 0);
    m_glWidget->setContentsMargins(0, 0, 0, 0);
    this->setContentsMargins(0, 0, 0, 0);

    setLayout(layout);
    initConnections();
}

View::~View() {}

namespace
{
struct LoadResult
{
    std::shared_ptr<Healpix_Map<float>> map; // null on failure
    FitsInfo info;
    int hdu    = 0;
    int column = 0;
    QString coordsys; // FITS COORDSYS, empty when absent
    QString error;
    uint64_t tooLargeBytes = 0; // set when a .npz array needs approval
};
} // namespace

// A binary 0/1 map is a mask: show its 0 pixels as unseen
static void
markBinaryMaskUnseen(Healpix_Map<float> &map)
{
    float *d     = const_cast<float *>(map.Map().begin());
    const long n = map.Npix();
    bool binary  = true;
    bool hasZero = false;
    bool hasOne  = false;
    for (long i = 0; i < n && binary; ++i)
    {
        if (d[i] == 0.0f)
            hasZero = true;
        else if (d[i] == 1.0f)
            hasOne = true;
        else if (std::isfinite(d[i]) && d[i] > -1e30f)
            binary = false;
    }
    if (binary && hasZero && hasOne)
        for (long i = 0; i < n; ++i)
            if (d[i] == 0.0f)
                d[i] = float(Healpix_undef);
}

// Reads the file on a worker thread; the (large) map is handed to the GL
// widget on the GUI thread once it is done. hdu <= 0 means "scan the file and
// use the first table with a numeric column".
void
View::startLoad(int hdu, int column, const QString &columnName,
                bool initial)
{
    m_loading              = true;
    const QString filename = m_filePath;

    auto *watcher = new QFutureWatcher<LoadResult>(this);
    connect(watcher, &QFutureWatcher<LoadResult>::finished, this,
            [this, watcher, initial, hdu, column, columnName]()
    {
        LoadResult result = watcher->result();
        watcher->deleteLater();
        m_loading = false;

        if (!result.map && result.tooLargeBytes)
        {
            // A big .npz array: load it only if the user agrees
            const double gib = double(result.tooLargeBytes) / (1ull << 30);
            const auto answer = QMessageBox::question(
                this, "Large array",
                QString("Loading this array needs about %1 GiB of memory "
                        "(more than the %2 GiB limit for .npz files).\n\n"
                        "Load it anyway?")
                    .arg(gib, 0, 'f', 1)
                    .arg(kNpzSizeCap >> 30),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer == QMessageBox::Yes)
            {
                npzApproveLarge(m_filePath.toStdString());
                startLoad(hdu, column, columnName, initial);
                return;
            }
            result.error = "Loading was cancelled (the array is too large).";
        }

        if (!result.map)
        {
            if (initial)
                emit loadFinished(false, result.error);
            else
                emit hduLoaded(false, result.error);
            return;
        }

        if (initial)
            m_fitsInfo = result.info;
        m_hdu    = result.hdu;
        m_column = result.column;
        m_glWidget->setMapFrame(skyframe::fromCoordSys(result.coordsys));
        m_glWidget->setMap(std::move(*result.map));

        if (initial)
            emit loadFinished(true, QString());
        else
            emit hduLoaded(true, QString());
    });

    watcher->setFuture(QtConcurrent::run([filename, hdu, column, columnName]()
    {
        LoadResult result;
        try
        {
            if (isNumpyPath(filename.toStdString()))
            {
                if (!columnName.isEmpty())
                    throw PlanckError("rows of a NumPy array have no names, "
                                      "give a number");
                const std::string path = filename.toStdString();
                result.info            = scanNumpy(path);
                // Without a choice: the first array that holds a map (or let
                // readNpyMap say why there is none)
                int useHdu = hdu;
                if (useHdu <= 0 && !result.info.hdus.isEmpty())
                    useHdu = result.info.hdus.first().index;
                auto map = readNpyMap(path, column, kMaxSupportedNside,
                                      nullptr, useHdu);
                markBinaryMaskUnseen(*map);
                result.map    = std::move(map);
                result.hdu    = useHdu > 0 ? useHdu : 1;
                result.column = column <= 0 ? 1 : column;
                return result;
            }

            fitshandle file;
            file.open(filename.toStdString());

            int useHdu = hdu, useCol = column;
            if (hdu <= 0 || !columnName.isEmpty())
            {
                result.info = scanFits(file);
                if (result.info.hdus.isEmpty())
                    throw PlanckError(
                        "No HEALPix map found (needs a table with NSIDE and "
                        "ORDERING\n"
                        "keywords and a column of 12*nside^2 values)");
                if (hdu <= 0)
                    useHdu = result.info.hdus.first().index;
                const FitsHdu *h = result.info.find(useHdu);
                if (!h)
                    throw PlanckError(("HDU " + std::to_string(useHdu)
                                       + " does not hold a map")
                                          .c_str());
                if (!columnName.isEmpty())
                {
                    useCol = findColumn(*h, columnName);
                    if (!useCol)
                        throw PlanckError(
                            ("no column named " + columnName.toStdString()
                             + " (available: " + columnNames(*h).toStdString()
                             + ")")
                                .c_str());
                }
                else if (useCol <= 0)
                    useCol = h->columns.first().index;
            }

            file.goto_hdu(useHdu);
            if (file.key_present("COORDSYS")) // else assume Galactic
                result.coordsys = QString::fromStdString(
                    file.get_key<std::string>("COORDSYS"));

            // Check the header before reading gigabytes of pixels
            const int nside = file.get_key<int>("NSIDE");
            if (nside > kMaxSupportedNside)
                throw PlanckError(("nside " + std::to_string(nside)
                                   + " is currently not supported (maximum is "
                                   + std::to_string(kMaxSupportedNside) + ").")
                                      .c_str());

            auto map = std::make_shared<Healpix_Map<float>>();
            read_Healpix_map_from_fits(file, *map, useCol);

            markBinaryMaskUnseen(*map);
            result.map    = std::move(map);
            result.hdu    = useHdu;
            result.column = useCol;
        }
        catch (const PlanckError &e)
        {
            result.error = e.what();
        }
        catch (const NpzTooLargeError &e)
        {
            result.error         = e.what();
            result.tooLargeBytes = e.bytes;
        }
        catch (const std::exception &e)
        {
            result.error = e.what();
        }
        return result;
    }));
}

void
View::loadMap(const QString &filename, int hdu, int column,
              const QString &columnName)
{
    m_filePath = filename;
    startLoad(hdu, column, columnName, true);
}

// Switch to another HDU / column of the same file; ignored while a load is
// already running
void
View::loadHdu(int hdu, int column)
{
    if (!m_loading)
        startLoad(hdu, column, QString(), false);
}

void
View::initConnections()
{
    connect(m_glWidget, &OpenGLWidget::zoomChanged, this, &View::zoomChanged);
    connect(m_glWidget, &OpenGLWidget::masksChanged, this, &View::masksChanged);
    connect(m_glWidget, &OpenGLWidget::regionChanged, this,
            &View::regionChanged);
    connect(m_glWidget, &OpenGLWidget::colormapRangeChanged, this,
            &View::colormapRangeChanged);
    connect(m_glWidget, &OpenGLWidget::cursorPosChanged, this,
            &View::cursorPosChanged);
}

void
View::setHduIndex(int index)
{
    m_hduIndex = index;
}

void
View::setComputedMap(Healpix_Map<float> &&map, const QString &title,
                     SkyFrame frame)
{
    m_computed = true;
    m_title    = title;
    m_filePath.clear();
    m_hdu      = 0;
    m_column   = 0;
    m_fitsInfo = FitsInfo();
    m_glWidget->setMapFrame(frame);
    m_glWidget->setMap(std::move(map));
}

QString
View::displayName() const
{
    return m_computed ? m_title : QFileInfo(m_filePath).fileName();
}

bool
View::saveMap(const QString &path, QString *error) const
{
    const Healpix_Map<float> *map = m_glWidget->healpixMap();
    if (!map)
    {
        if (error)
            *error = "there is no map";
        return false;
    }
    try
    {
        fitshandle out;
        out.create("!" + path.toStdString()); // "!" overwrites
        write_Healpix_map_to_fits(out, *map, PLANCK_FLOAT32);
        out.set_key("COORDSYS",
                    skyframe::fitsCode(m_glWidget->mapFrame()).toStdString(),
                    "Coordinate system");
    }
    catch (const std::exception &e)
    {
        if (error)
            *error = e.what();
        return false;
    }
    return true;
}
