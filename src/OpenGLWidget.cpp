#include "OpenGLWidget.hpp"

OpenGLWidget::OpenGLWidget(QWidget *parent) : QOpenGLWidget(parent) {}

template <typename T>
void
OpenGLWidget::setMap(const Healpix_Map<T> &map)
{
    m_nside = map.Nside();
    m_npix  = map.Npix();

    // Generate a texture for the map
    glGenTextures(1, &m_mapTexture);
    glBindTexture(GL_TEXTURE_2D, m_mapTexture);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload the map data to the texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_nside, m_nside, 0, GL_RED,
                 GL_DOUBLE, map.data());
}

void
OpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_TEXTURE_2D);
}

void
OpenGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void
OpenGLWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, m_mapTexture);
    // Render the texture here (e.g., using a quad)
}
