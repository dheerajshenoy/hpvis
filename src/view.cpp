#include "view.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"

#include <QMessageBox>

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

bool
View::loadMap(const std::string &filename)
{
    try
    {
        Healpix_Map<float> healpix_map;

        read_Healpix_map_from_fits(filename, healpix_map, 1, 2);
        m_glWidget->setMap(healpix_map);
        m_filePath = QString::fromStdString(filename);
        return true;
    }
    catch (const PlanckError &e)
    {
        QMessageBox::critical(this, "Error",
                              QString("Failed to load map: %1").arg(e.what()));
    }
    catch (const std::exception &e)
    {
        QMessageBox::critical(this, "Error",
                              QString("Failed to load map: %1").arg(e.what()));
    }

    return false;
}

void
View::initConnections()
{
    connect(m_glWidget, &OpenGLWidget::zoomChanged, this, &View::zoomChanged);
    connect(m_glWidget, &OpenGLWidget::cursorPosChanged, this,
            &View::cursorPosChanged);
}
