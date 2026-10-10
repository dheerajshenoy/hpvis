#include "openglwidget.hpp"

#include "healpix_map.h"
#include "shaderloader.hpp"

#include <QEnterEvent>
#include <QFile>
#include <QNativeGestureEvent>
#include <QMouseEvent>
#include <QVector3D>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <qcoreapplication.h>
#include <vector>

OpenGLWidget::OpenGLWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    m_camera = new MapCamera();
    setMouseTracking(true);
    m_cursorPos = QPointF(width() / 2.0, height() / 2.0);
}

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
    // w/h are logical sizes; the framebuffer is devicePixelRatio times larger
    // on scaled (HiDPI / fractional) displays
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, qRound(w * dpr), qRound(h * dpr));
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

    m_camera->uploadUniforms(m_shaderProgram);

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
    m_shaderProgram.setUniformValue(
        "bg_color",
        QVector3D(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF()));
    m_shaderProgram.setUniformValue(
        "unseen_color", QVector3D(m_unseenColor.redF(), m_unseenColor.greenF(),
                                  m_unseenColor.blueF()));
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

QVector2D
OpenGLWidget::toNdc(const QPointF &pos) const
{
    return QVector2D(2.0f * float(pos.x()) / width() - 1.0f,
                     1.0f - 2.0f * float(pos.y()) / height());
}

bool
OpenGLWidget::event(QEvent *e)
{
    // Touchpad pinch (Wayland / macOS native gestures)
    if (e->type() == QEvent::NativeGesture)
    {
        auto *g = static_cast<QNativeGestureEvent *>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture)
        {
            m_camera->zoomAt(1.0f + float(g->value()), toNdc(m_cursorPos));
            update();
            return true;
        }
        if (g->gestureType() == Qt::SmartZoomNativeGesture)
        {
            m_camera->reset();
            update();
            return true;
        }
    }
    return QOpenGLWidget::event(e);
}

void
OpenGLWidget::wheelEvent(QWheelEvent *e)
{
    const QPoint px = e->pixelDelta();

    // Two-finger touchpad scroll: pan (pixelDelta is only set by touchpads)
    if (!px.isNull() && !(e->modifiers() & Qt::ControlModifier))
    {
        m_camera->panBy(QVector2D(2.0f * px.x() / float(width()),
                                  -2.0f * px.y() / float(height())));
        update();
        e->accept();
        return;
    }

    // Mouse wheel, or pinch reported as Ctrl+wheel: zoom about the cursor
    const double steps = (px.isNull() ? e->angleDelta().y() / 120.0
                                      : px.y() / 50.0);
    m_camera->zoomAt(float(std::pow(1.15, steps)), toNdc(e->position()));
    m_cursorPos = e->position();
    update();
    e->accept();
}

void
OpenGLWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
    {
        m_isPanning    = true;
        m_lastMousePos = e->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void
OpenGLWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
    {
        m_isPanning = false;
        unsetCursor();
    }
}

void
OpenGLWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
    {
        m_camera->reset();
        update();
    }
}

void
OpenGLWidget::enterEvent(QEnterEvent *e)
{
    m_cursorPos = e->position();
    QOpenGLWidget::enterEvent(e);
}

void
OpenGLWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_cursorPos = e->position();
    if (!m_isPanning)
        return;

    const QPoint delta = e->pos() - m_lastMousePos;
    m_lastMousePos     = e->pos();

    m_camera->panBy(QVector2D(2.0f * delta.x() / float(width()),
                              -2.0f * delta.y() / float(height())));
    update();
}
