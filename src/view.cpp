#include "view.hpp"

#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"

View::View(QWidget *parent) : QWidget(parent)
{
    m_layout   = new QVBoxLayout(this);
    m_glWidget = new OpenGLWidget(this);
    m_layout->addWidget(m_glWidget);
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
    catch (const std::exception &e)
    {
        std::cerr << "Error loading FITS map: " << e.what() << std::endl;
    }
}
