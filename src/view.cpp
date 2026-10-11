#include "view.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"

#include <QFutureWatcher>
#include <QtConcurrent>
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
    QString error;
};
} // namespace

// Reads the file on a worker thread; the (large) map is handed to the GL
// widget on the GUI thread once it is done.
void
View::loadMap(const QString &filename)
{
    m_filePath = filename;

    auto *watcher = new QFutureWatcher<LoadResult>(this);
    connect(watcher, &QFutureWatcher<LoadResult>::finished, this,
            [this, watcher]()
    {
        LoadResult result = watcher->result();
        watcher->deleteLater();

        if (!result.map)
        {
            emit loadFinished(false, result.error);
            return;
        }
        m_glWidget->setMap(std::move(*result.map));
        emit loadFinished(true, QString());
    });

    watcher->setFuture(QtConcurrent::run([filename]()
    {
        LoadResult result;
        try
        {
            auto map = std::make_shared<Healpix_Map<float>>();
            read_Healpix_map_from_fits(filename.toStdString(), *map, 1, 2);
            result.map = std::move(map);
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
View::initConnections()
{
    connect(m_glWidget, &OpenGLWidget::zoomChanged, this, &View::zoomChanged);
    connect(m_glWidget, &OpenGLWidget::masksChanged, this,
            &View::masksChanged);
    connect(m_glWidget, &OpenGLWidget::colormapRangeChanged, this,
            &View::colormapRangeChanged);
    connect(m_glWidget, &OpenGLWidget::cursorPosChanged, this,
            &View::cursorPosChanged);
}
