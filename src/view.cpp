#include "view.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"

#include <QMessageBox>

View::View(QWidget *parent) : QWidget(parent)
{
    m_layout   = new QVBoxLayout(this);
    m_glWidget = new OpenGLWidget(this);
    m_layout->addWidget(m_glWidget);

    m_layout->setContentsMargins(0, 0, 0, 0);
    m_glWidget->setContentsMargins(0, 0, 0, 0);
    this->setContentsMargins(0, 0, 0, 0);

    setLayout(m_layout);
}

View::~View() {}

void
View::loadMap(const std::string &filename)
{
    try
    {
        read_Healpix_map_from_fits(filename, m_healpix_map, 1, 2);
        m_glWidget->setMap(m_healpix_map);
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
}

void
View::setPixelOrdering(PixelOrdering ordering)
{
    m_glWidget->setPixelOrdering(ordering);
}

void
View::setColormap(Colormap colormap)
{
    m_glWidget->setColormap(colormap);
}
