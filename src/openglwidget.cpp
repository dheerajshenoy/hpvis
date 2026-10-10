#include "openglwidget.hpp"

#include "healpix_map.h"

OpenGLWidget::OpenGLWidget(QWidget *parent) : QOpenGLWidget(parent) {}

OpenGLWidget::~OpenGLWidget() {}

void
OpenGLWidget::setMap(const Healpix_Map<float> &map)
{
    m_nside = map.Nside();
    m_npix  = map.Npix();

    if (m_npix > 0)
        m_map_data = &map.Map()[0];
    else
        m_map_data = nullptr;

    update();
}

void
OpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    // Set a clean, dark background color (e.g., dark slate gray)
    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
}

void
OpenGLWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void
OpenGLWidget::paintGL()
{
    // Clear the screen with our background color
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Later, this is where we will draw our map!
    // For now, if you selected a map, we could print a tiny debug message or
    // draw a placeholder.
}
