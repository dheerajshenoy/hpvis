#include "view.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"

#include <QFutureWatcher>
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
    QString error;
};
} // namespace

// Reads the file on a worker thread; the (large) map is handed to the GL
// widget on the GUI thread once it is done. hdu <= 0 means "scan the file and
// use the first table with a numeric column".
void
View::startLoad(int hdu, int column, bool initial)
{
    m_loading              = true;
    const QString filename = m_filePath;

    auto *watcher = new QFutureWatcher<LoadResult>(this);
    connect(watcher, &QFutureWatcher<LoadResult>::finished, this,
            [this, watcher, initial]()
    {
        LoadResult result = watcher->result();
        watcher->deleteLater();
        m_loading = false;

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
        m_glWidget->setMap(std::move(*result.map));

        if (initial)
            emit loadFinished(true, QString());
        else
            emit hduLoaded(true, QString());
    });

    watcher->setFuture(QtConcurrent::run([filename, hdu, column]()
    {
        LoadResult result;
        try
        {
            fitshandle file;
            file.open(filename.toStdString());

            int useHdu = hdu, useCol = column;
            if (hdu <= 0)
            {
                result.info = scanFits(file);
                if (result.info.hdus.isEmpty())
                    throw PlanckError(
                        "No HEALPix map found (needs a table with NSIDE and "
                        "ORDERING\n"
                        "keywords and a column of 12*nside^2 values)");
                useHdu = result.info.hdus.first().index;
                useCol = result.info.hdus.first().columns.first().index;
            }

            file.goto_hdu(useHdu);

            // Check the header before reading gigabytes of pixels
            const int nside = file.get_key<int>("NSIDE");
            if (nside > kMaxSupportedNside)
                throw PlanckError(("nside " + std::to_string(nside)
                                   + " is currently not supported (maximum is "
                                   + std::to_string(kMaxSupportedNside) + ").")
                                      .c_str());

            auto map = std::make_shared<Healpix_Map<float>>();
            read_Healpix_map_from_fits(file, *map, useCol);

            // A binary 0/1 map is a mask: show its 0 pixels as unseen
            {
                float *d     = const_cast<float *>(map->Map().begin());
                const long n = map->Npix();
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
            result.map    = std::move(map);
            result.hdu    = useHdu;
            result.column = useCol;
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
    }));
}

void
View::loadMap(const QString &filename)
{
    m_filePath = filename;
    startLoad(0, 0, true);
}

// Switch to another HDU / column of the same file; ignored while a load is
// already running
void
View::loadHdu(int hdu, int column)
{
    if (!m_loading)
        startLoad(hdu, column, false);
}

void
View::initConnections()
{
    connect(m_glWidget, &OpenGLWidget::zoomChanged, this, &View::zoomChanged);
    connect(m_glWidget, &OpenGLWidget::masksChanged, this, &View::masksChanged);
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
