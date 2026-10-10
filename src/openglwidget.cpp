#include "openglwidget.hpp"

#include "healpix_map.h"
#include "shaderloader.hpp"

#include <QFile>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <qcoreapplication.h>
#include <vector>

OpenGLWidget::OpenGLWidget(QWidget *parent) : QOpenGLWidget(parent) {}

OpenGLWidget::~OpenGLWidget()
{
    if (m_mapTexture)
    {
        makeCurrent();
        glDeleteTextures(1, &m_mapTexture);
        m_mapTexture = 0;
        doneCurrent();
    }
}

void
OpenGLWidget::setMap(const Healpix_Map<float> &map)
{
    m_pendingMap    = map;
    m_hasPendingMap = true;

    // Data range (ignoring non-finite and HEALPix UNSEEN pixels)
    m_dataMin         = std::numeric_limits<float>::max();
    m_dataMax         = std::numeric_limits<float>::lowest();
    const float *data = map.Map().begin();
    for (long i = 0; i < map.Npix(); ++i)
    {
        float v = data[i];
        if (!std::isfinite(v) || v < -1e30f)
            continue;
        m_dataMin = std::min(m_dataMin, v);
        m_dataMax = std::max(m_dataMax, v);
    }
    if (m_dataMin >= m_dataMax)
        m_dataMax = m_dataMin + 1.0f;
    m_vmin = m_dataMin;
    m_vmax = m_dataMax;

    update();
}

void
OpenGLWidget::initializeGL()
{
    initializeOpenGLFunctions();
    // Set a clean, dark background color (e.g., dark slate gray)
    glClearColor(0.08f, 0.08f, 0.12f, 1.0f);

    setupGeometry();

    const QString vertSrc = loadShaderSource(":/shaders/vertex.vert");
    const QString fragSrc = loadShaderSource(":/shaders/main.frag");
    if (vertSrc.isEmpty() || fragSrc.isEmpty())
        return;

    if (!m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                                 vertSrc))
        std::cerr << "[Vertex shader error]\n"
                  << m_shaderProgram.log().toStdString() << std::endl;
    if (!m_shaderProgram.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                 fragSrc))
        std::cerr << "[Fragment shader error]\n"
                  << m_shaderProgram.log().toStdString() << std::endl;
    if (!m_shaderProgram.link())
        std::cerr << "[Shader error] " << m_shaderProgram.log().toStdString()
                  << std::endl;
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

    if (m_hasPendingMap)
    {
        uploadMapToGPU(m_pendingMap);
        m_hasPendingMap = false;
    }

    m_shaderProgram.bind();

    // 1. Bind the 1D HEALPix map texture to texture unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_mapTexture);
    m_shaderProgram.setUniformValue("mapTexture", 0);

    // 2. Pass map metadata uniforms to the shader
    m_shaderProgram.setUniformValue("nside", static_cast<int>(m_nside));
    m_shaderProgram.setUniformValue("npix", static_cast<int>(m_npix));
    m_shaderProgram.setUniformValue("vmin", m_vmin);
    m_shaderProgram.setUniformValue("vmax", m_vmax);
    m_shaderProgram.setUniformValue("colormap", static_cast<int>(m_colormap));
    m_shaderProgram.setUniformValue("colormap_scale",
                                    static_cast<int>(m_colormapScale));
    m_shaderProgram.setUniformValue("pixel_ordering",
                                    static_cast<int>(m_pixelOrdering));
    m_shaderProgram.setUniformValue("invert_colormap",
                                    m_invertColormap ? 1 : 0);

    m_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_vao.release();

    glBindTexture(GL_TEXTURE_2D, 0);
    m_shaderProgram.release();
}

void
OpenGLWidget::uploadMapToGPU(const Healpix_Map<float> &map)
{
    m_nside = map.Nside();
    m_npix  = map.Npix();

    const float *mapDataPtr = map.Map().begin();

    if (!mapDataPtr || m_npix <= 0)
    {
        return;
    }

    makeCurrent();

    if (m_mapTexture)
    {
        glDeleteTextures(1, &m_mapTexture);
        m_mapTexture = 0;
    }

    glGenTextures(1, &m_mapTexture);
    glBindTexture(GL_TEXTURE_2D, m_mapTexture);

    // 1D textures are limited (~16k texels); pack the map into a 2D texture
    const int W = 4096;
    const int H = (m_npix + W - 1) / W;
    std::vector<float> buf((size_t)W * H, 0.0f);
    std::copy(mapDataPtr, mapDataPtr + m_npix, buf.begin());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, W, H, 0, GL_RED, GL_FLOAT,
                 buf.data());

    // 5. Set basic texture filtering parameters (NEAREST keeps crisp pixel
    // edges)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Unbind
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Call this inside initializeGL() after initializeOpenGLFunctions():
void
OpenGLWidget::setupGeometry()
{
    float vertices[] = {// Position (X, Y)  // Texture Coords (U, V)
                        -1.0f, 1.0f, 0.0f, 1.0f,  -1.0f, -1.0f,
                        0.0f,  0.0f, 1.0f, -1.0f, 1.0f,  0.0f,

                        -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  -1.0f,
                        1.0f,  0.0f, 1.0f, 1.0f,  1.0f,  1.0f};

    // Create and bind the VAO using Qt
    m_vao.create();
    m_vao.bind();

    // Create and bind the VBO using Qt
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices, sizeof(vertices));

    // Setup attribute pointers
    // Position attribute (location = 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)0);

    // Texture coordinate attribute (location = 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));

    // Release bindings
    m_vbo.release();
    m_vao.release();
}
