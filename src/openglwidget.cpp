#include "openglwidget.hpp"

#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "healpix_map.h"
#include "shaderloader.hpp"

#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QVector3D>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <healpix_base.h>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <pointing.h>
#include <qcoreapplication.h>
#include <vector>

static QVector<double>
niceTicks(double lo, double hi, int count)
{
    if (!(hi > lo) || count < 1)
        return {};
    double raw  = (hi - lo) / count;
    double mag  = std::pow(10.0, std::floor(std::log10(raw)));
    double norm = raw / mag; // in [1, 10)
    double step = (norm < 1.5 ? 1 : norm < 3.5 ? 2 : norm < 7.5 ? 5 : 10) * mag;
    QVector<double> v;
    // Integer multiples of the step: zero is exactly 0, not 5e-20
    const long long k0 = static_cast<long long>(std::ceil(lo / step - 1e-9));
    const long long k1 = static_cast<long long>(std::floor(hi / step + 1e-9));
    for (long long k = k0; k <= k1 && v.size() < 4 * count; ++k)
        v << k * step;
    return v;
}

// Compact tick label. `step` is the spacing between ticks, `maxAbs` the
// largest magnitude on the bar; both decide how many digits are needed.
static QString
formatTick(double v, double step, double maxAbs)
{
    if (v == 0.0)
        return "0";
    const int stepExp = int(std::floor(std::log10(step) + 1e-9));
    if (maxAbs < 1e-3 || maxAbs >= 1e5)
    {
        // Scientific with one shared exponent: -4e-4, -2e-4, 0, 2e-4, ...
        const int E      = int(std::floor(std::log10(maxAbs)));
        const int digits = std::clamp(E - stepExp, 0, 6);
        return QString::number(v / std::pow(10.0, E), 'f', digits) + "e"
               + QString::number(E);
    }
    return QString::number(v, 'f', std::clamp(-stepExp, 0, 8));
}

OpenGLWidget::OpenGLWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    m_camera = new MapCamera();
    setMouseTracking(true);
    m_cursorPos = QPointF(width() / 2.0, height() / 2.0);
    emitCursorData();
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
    if (m_maskTextures[0] || m_maskTextures[1] || m_maskTextures[2]
        || m_maskTextures[3])
    {
        makeCurrent();
        glDeleteTextures(int(m_maskTextures.size()), m_maskTextures.data());
        m_maskTextures.fill(0);
        doneCurrent();
    }
}

// TODO: Make this async so that the UI doesn't freeze when loading large maps
void
OpenGLWidget::setPixelOrdering(PixelOrdering ordering)
{
    m_pixelOrdering = ordering;
    emitCursorData();
    update();
}

void
OpenGLWidget::setColormap(Colormap colormap)
{
    m_colormap = colormap;
    update();
}

void
OpenGLWidget::resetColormapRange()
{
    m_vmin = m_dataMin;
    m_vmax = m_dataMax;
    update();
}

void
OpenGLWidget::setColormapMin(double min)
{
    m_vmin = static_cast<float>(min);
    update();
}

void
OpenGLWidget::setColormapMax(double max)
{
    m_vmax = static_cast<float>(max);
    update();
}

void
OpenGLWidget::setColormapScale(ColormapScale scale)
{
    m_colormapScale = scale;
    update();
}

void
OpenGLWidget::setColormapInvert(bool invert)
{
    m_invertColormap = invert;
    update();
}

void
OpenGLWidget::setBackgroundColor(const QColor &color)
{
    m_bgColor = color;
    update();
}

void
OpenGLWidget::setUnseenColor(const QColor &color)
{
    m_unseenColor = color;
    update();
}

void
OpenGLWidget::setMaskMode(int mode)
{
    m_maskMode = mode;
    update();
}

void
OpenGLWidget::setMaskOpacity(float opacity)
{
    m_maskOpacity = opacity;
    update();
}

void
OpenGLWidget::recomputeDataRange()
{
    long totalPixels = m_pendingMap.Npix();
    long validPixels = 0;

    // Range of valid pixels (ignoring non-finite, HEALPix UNSEEN, masked)
    struct Active
    {
        const uint8_t *data;
        bool inverted;
    };

    std::vector<Active> active;

    for (const MaskEntry &m : m_masks)
        if (m.enabled && m.data.size() == size_t(totalPixels))
            active.push_back({m.data.data(), m.inverted});

    m_dataMin = std::numeric_limits<float>::max();
    m_dataMax = std::numeric_limits<float>::lowest();

    const float *data = m_pendingMap.Map().begin();
    for (long i = 0; i < totalPixels; ++i)
    {
        float v = data[i];
        if (!std::isfinite(v) || v < -1e30f)
            continue;
        if (!active.empty())
        {
            float keep = 1.0f; // same product the shader computes
            for (const Active &m : active)
            {
                const float w = m.data[i] * (1.0f / 255.0f);
                keep *= m.inverted ? 1.0f - w : w;
            }
            if (keep < 0.5f)
                continue;
        }

        // This pixel survived all cuts; count it toward f_sky
        validPixels++;

        m_dataMin = std::min(m_dataMin, v);
        m_dataMax = std::max(m_dataMax, v);
    }

    if (totalPixels > 0)
        m_fsky = float(validPixels) / float(totalPixels);
    else
        m_fsky = 0.0f;

    // Handle edge case where no valid pixels were found
    if (m_dataMin > m_dataMax)
    {
        m_dataMin = 0.0f;
        m_dataMax = 1.0f;
    }
    else if (m_dataMin >= m_dataMax)
    {
        m_dataMax = m_dataMin + 1.0f;
    }
}

void
OpenGLWidget::setMap(const Healpix_Map<float> &map)
{
    // Masks only make sense for the resolution they were loaded for
    const bool sizeChanged = map.Npix() != m_pendingMap.Npix();

    m_pendingMap    = map;
    m_hasPendingMap = true;

    if (sizeChanged && !m_masks.empty())
    {
        m_masks.clear();
        m_maskDirty = true;
        emit masksChanged();
    }

    recomputeDataRange();
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

    // ---------------------------
    // Main Render              //
    // ---------------------------

    QRectF colorbar_rect = colorbarRect();

    if (m_hasPendingMap)
    {
        uploadMapToGPU(m_pendingMap);
        m_hasPendingMap = false;

        emit mapLoaded(m_pendingMap.Nside(), m_pendingMap.Npix());
    }

    if (m_maskDirty)
        uploadMaskToGPU();

    m_shaderProgram.bind();

    m_camera->uploadUniforms(m_shaderProgram);

    // 1. Bind the 1D HEALPix map texture to texture unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_mapTexture);
    m_shaderProgram.setUniformValue("mapTexture", 0);

    // Mask textures on units 1..4; their samplers are only read for masks
    // whose bit is set in mask_bits
    for (int g = 0; g < 4; ++g)
    {
        glActiveTexture(GL_TEXTURE1 + g);
        glBindTexture(GL_TEXTURE_2D, m_maskTextures[g]);
        m_shaderProgram.setUniformValue(
            QByteArray("maskTex" + QByteArray::number(g)).constData(), 1 + g);
    }
    glActiveTexture(GL_TEXTURE0);
    m_shaderProgram.setUniformValue("viewport_size",
                                    QVector2D(width() * devicePixelRatioF(),
                                              height() * devicePixelRatioF()));
    m_shaderProgram.setUniformValue("mask_bits", maskBits());
    m_shaderProgram.setUniformValue("mask_invert_bits", maskBits(true));
    m_shaderProgram.setUniformValue("mask_mode", m_maskMode);
    m_shaderProgram.setUniformValue("mask_opacity", m_maskOpacity);

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
    m_shaderProgram.setUniformValue("colorbar_enabled", m_colorBarVisible);
    m_shaderProgram.setUniformValue(
        "colorbar_rect",
        QVector4D(colorbar_rect.left(), colorbar_rect.top(),
                  colorbar_rect.right(), colorbar_rect.bottom()));

    m_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_vao.release();

    glBindTexture(GL_TEXTURE_2D, 0);

    // Minimap: the whole map in a small 2:1 viewport (bottom-right corner)
    const double dpr        = devicePixelRatioF();
    const QRect minimapRect = this->minimapRect();
    // Only useful once the view is smaller than the whole map
    const bool showMinimap  = minimapVisible();
    if (showMinimap)
    {
        const GLint physW = qRound(kMinimapW * dpr);
        const GLint physH = qRound(kMinimapH * dpr);
        const GLint physX = qRound(minimapRect.left() * dpr);
        // GL origin is bottom-left, Qt's is top-left
        const GLint physY = qRound((height() - minimapRect.bottom() - 1) * dpr);

        glViewport(physX, physY, physW, physH);
        m_shaderProgram.setUniformValue("zoom", 1.0f);
        m_shaderProgram.setUniformValue("panOffset", QVector2D(0.0f, 0.0f));
        m_shaderProgram.setUniformValue("viewport_size",
                                        QVector2D(physW, physH));
        m_shaderProgram.setUniformValue("colorbar_enabled", false);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_mapTexture);
        for (int g = 0; g < 4; ++g)
        {
            glActiveTexture(GL_TEXTURE1 + g);
            glBindTexture(GL_TEXTURE_2D, m_maskTextures[g]);
        }
        glActiveTexture(GL_TEXTURE0);

        m_vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_vao.release();

        glViewport(0, 0, qRound(width() * dpr), qRound(height() * dpr));
    }

    m_shaderProgram.release();

    if (!m_colorBarVisible && !showMinimap)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::TextAntialiasing);

    if (showMinimap)
    {
        // The view window spans panOffset +/- 1/zoom on the projection
        // plane [-1, 1]^2, which the minimap shows in full
        const double hw = 0.5 * kMinimapW;
        const double hh = 0.5 * kMinimapH;
        const double cx
            = minimapRect.left() + hw + double(m_camera->panOffset.x()) * hw;
        const double cy
            = minimapRect.top() + hh - double(m_camera->panOffset.y()) * hh;
        const double vw = hw / m_camera->zoom;
        const double vh = hh / m_camera->zoom;

        p.setClipRect(minimapRect);
        p.setPen(QPen(QColor(255, 80, 80), 1.5));
        p.setBrush(QColor(255, 80, 80, 40));
        p.drawRect(QRectF(cx - vw, cy - vh, 2 * vw, 2 * vh));
        p.setClipping(false);

        p.setPen(QPen(QColor(200, 200, 200, 200), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(minimapRect.adjusted(0, 0, -1, -1));
    }

    if (!m_colorBarVisible)
        return;

    const QRectF r      = colorbarRect(); // device px, y up
    // Painter wants logical px with y DOWN:
    const double left   = r.left() / dpr;
    const double width  = r.width() / dpr;
    const double bottom = height() - r.top() / dpr; // bar's bottom edge, y down
    // Dark backdrop so the labels stay readable over bright parts of the map
    const double barTop = bottom - r.height() / dpr;
    // (clipped so it doesn't darken the gradient bar drawn by the shader)
    const QRect outer(qRound(left - 14), qRound(barTop - 6), qRound(width + 28),
                      qRound(r.height() / dpr + 36));
    const QRect barRect(qRound(left), qRound(barTop), qRound(width),
                        qRound(r.height() / dpr));
    p.setClipRegion(QRegion(outer).subtracted(QRegion(barRect)));
    p.fillRect(outer, QColor(0, 0, 0, 140));
    p.setClipping(false);
    p.setPen(Qt::white);

    // Use as many ticks as fit without the labels touching (5 down to 2)
    const double maxAbs
        = std::max({std::abs(double(m_vmin)), std::abs(double(m_vmax)), 1e-30});
    struct Tick
    {
        double x;
        QString label;
        double w;
    };
    QVector<Tick> ticks;
    for (int count = 5; count >= 2; --count)
    {
        const QVector<double> values = niceTicks(m_vmin, m_vmax, count);
        if (values.isEmpty())
            continue;
        const double step
            = values.size() > 1 ? values[1] - values[0] : m_vmax - m_vmin;

        ticks.clear();
        bool fits        = true;
        double lastRight = -1e9;
        for (double v : values)
        {
            Tick t;
            t.x     = left + width * scaleValue(v);
            t.label = formatTick(v, step, maxAbs);
            t.w     = p.fontMetrics().horizontalAdvance(t.label);
            if (t.x - t.w / 2 < lastRight + 6)
                fits = false;
            lastRight = t.x + t.w / 2;
            ticks << t;
        }
        if (fits)
            break;
    }
    for (const Tick &t : ticks)
    {
        p.drawLine(QPointF(t.x, bottom), QPointF(t.x, bottom + 5));
        p.drawText(QPointF(t.x - t.w / 2, bottom + 18), t.label);
    }
}

int
OpenGLWidget::addMaskFiles(const QStringList &files)
{
    if (m_pendingMap.Npix() == 0)
    {
        QMessageBox::warning(this, "Mask", "Load a map before loading a mask.");
        return 0;
    }

    int added = 0;
    for (const QString &file : files)
    {
        if (maskCount() >= kMaxMasks)
        {
            QMessageBox::warning(
                this, "Mask",
                QString("At most %1 masks can be loaded.").arg(kMaxMasks));
            break;
        }
        const QString path = QFileInfo(file).absoluteFilePath();
        const bool dup
            = std::any_of(m_masks.begin(), m_masks.end(),
                          [&](const MaskEntry &m) { return m.path == path; });
        if (dup)
            continue;

        try
        {
            Healpix_Map<float> mask;
            read_Healpix_map_from_fits(file.toStdString(), mask, 1, 2);

            if (mask.Nside() != m_pendingMap.Nside())
            {
                QMessageBox::warning(
                    this, "Mask",
                    QString("%1: nside %2 does not match the map (nside %3).")
                        .arg(QFileInfo(file).fileName())
                        .arg(mask.Nside())
                        .arg(m_pendingMap.Nside()));
                continue;
            }
            if (mask.Scheme() != m_pendingMap.Scheme())
                mask.swap_scheme();

            MaskEntry entry;
            entry.name = QFileInfo(file).fileName();
            entry.path = path;
            entry.data.resize(size_t(mask.Npix()));
            const float *d = mask.Map().begin();
            for (long i = 0; i < mask.Npix(); ++i)
            {
                // NaN / UNSEEN count as masked; clamp weights to [0, 1]
                float v = (std::isfinite(d[i]) && d[i] > -1e30f) ? d[i] : 0.0f;
                entry.data[i] = static_cast<uint8_t>(
                    std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            m_masks.push_back(std::move(entry));
            ++added;
        }
        catch (const PlanckError &e)
        {
            QMessageBox::critical(
                this, "Mask",
                QString("Failed to load %1: %2").arg(file, QString(e.what())));
        }
        catch (const std::exception &e)
        {
            QMessageBox::critical(
                this, "Mask",
                QString("Failed to load %1: %2").arg(file, QString(e.what())));
        }
    }

    if (added)
        applyMaskChange(true);
    return added;
}

void
OpenGLWidget::removeMask(int index)
{
    if (index < 0 || index >= maskCount())
        return;
    m_masks.erase(m_masks.begin() + index);
    applyMaskChange(true);
}

void
OpenGLWidget::clearMasks()
{
    m_masks.clear();
    applyMaskChange(true);
}

void
OpenGLWidget::setMaskInverted(int index, bool inverted)
{
    if (index < 0 || index >= maskCount()
        || m_masks[index].inverted == inverted)
        return;
    m_masks[index].inverted = inverted;
    applyMaskChange(false); // uniform only: no re-upload
}

void
OpenGLWidget::setMaskEnabled(int index, bool enabled)
{
    if (index < 0 || index >= maskCount() || m_masks[index].enabled == enabled)
        return;
    m_masks[index].enabled = enabled;
    applyMaskChange(false); // only the enabled bits change: no re-upload
}

// Bit i = mask i is enabled (or, with `inverted`, mask i is inverted)
int
OpenGLWidget::maskBits(bool inverted) const
{
    int bits = 0;
    for (size_t i = 0; i < m_masks.size(); ++i)
        if (inverted ? m_masks[i].inverted : m_masks[i].enabled)
            bits |= 1 << i;
    return bits;
}

// Refreshes everything that depends on the mask list: the GPU textures (only
// if masks were added/removed), the implicit colormap range and the UI list.
void
OpenGLWidget::applyMaskChange(bool texturesChanged)
{
    if (texturesChanged)
        m_maskDirty = true;

    // The implicit range excludes masked pixels only while a mask is enabled
    recomputeDataRange();
    m_vmin = m_dataMin;
    m_vmax = m_dataMax;

    emit masksChanged();
    emit colormapRangeChanged(m_vmin, m_vmax);
    update();
}

// Packs the masks 4 per RGBA8 texture (mask i -> texture i/4, channel i%4)
void
OpenGLWidget::uploadMaskToGPU()
{
    makeCurrent();
    glDeleteTextures(int(m_maskTextures.size()), m_maskTextures.data());
    m_maskTextures.fill(0);
    m_maskDirty = false;

    const size_t npix = size_t(m_npix);
    const int W       = 4096;
    const int H       = int((npix + W - 1) / W);

    for (size_t g = 0; g * 4 < m_masks.size(); ++g)
    {
        // Unused channels stay 255 (keep)
        std::vector<uint8_t> buf(size_t(W) * H * 4, 255);
        for (size_t c = 0; c < 4 && g * 4 + c < m_masks.size(); ++c)
        {
            const std::vector<uint8_t> &d = m_masks[g * 4 + c].data;
            for (size_t i = 0; i < d.size(); ++i)
                buf[i * 4 + c] = d[i];
        }

        glGenTextures(1, &m_maskTextures[g]);
        glBindTexture(GL_TEXTURE_2D, m_maskTextures[g]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, buf.data());
        // Restore the default: Qt's text renderer relies on it
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
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
    // Restore the default: Qt's text renderer relies on it
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

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

// CPU copy of inverseMollweide() in shaders/projections.glsl
static bool
inverseMollweide(QVector2D uv, double &colat, double &phi)
{
    const double x_range = 2.82842712, y_range = 1.41421356;
    const double x = -(uv.x() - 0.5) * 2.0 * x_range;
    const double y = (uv.y() - 0.5) * 2.0 * y_range;
    if ((x * x / 8.0) + (y * y / 2.0) > 1.0)
        return false;

    const double theta_aux = std::asin(std::clamp(y / 1.41421356, -1.0, 1.0));
    const double lat       = std::asin(std::clamp(
        (2.0 * theta_aux + std::sin(2.0 * theta_aux)) / M_PI, -1.0, 1.0));
    const double cosTheta  = std::cos(theta_aux);
    double lon             = 0.0;
    if (std::abs(cosTheta) > 0.0001)
        lon = (M_PI * x) / (2.82842712 * cosTheta);
    lon = std::fmod(lon, 2.0 * M_PI);
    if (lon < 0.0)
        lon += 2.0 * M_PI;

    colat = M_PI / 2.0 - lat;
    phi   = lon;
    return true;
}

void
OpenGLWidget::emitCursorData()
{
    // Same mapping as the fragment shader: NDC -> camera -> projection
    const QVector2D p
        = toNdc(m_cursorPos) / m_camera->zoom + m_camera->panOffset;
    double colat, phi;
    if (m_nside <= 0 || m_pendingMap.Npix() == 0
        || !inverseMollweide(p * 0.5f + QVector2D(0.5f, 0.5f), colat, phi))
    {
        emit cursorPosChanged(QPointF(), std::nan(""), -1);
        return;
    }

    const Healpix_Base base(
        static_cast<int>(m_nside),
        m_pixelOrdering == PixelOrdering::NESTED ? NEST : RING, SET_NSIDE);
    const qlonglong idx = base.ang2pix(pointing(colat, phi));
    float v             = m_pendingMap.Map()[idx];
    if (v < -1.0e30f || !std::isfinite(v))
        v = std::nanf("");

    double lon = phi * 180.0 / M_PI;
    if (lon > 180.0)
        lon -= 360.0;
    emit cursorPosChanged(QPointF(lon, 90.0 - colat * 180.0 / M_PI), v, idx);
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
            emit zoomChanged(m_camera->zoom);
            emitCursorData();
            update();
            return true;
        }
        if (g->gestureType() == Qt::SmartZoomNativeGesture)
        {
            m_camera->reset();
            emit zoomChanged(m_camera->zoom);
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
        emitCursorData();
        update();
        e->accept();
        return;
    }

    // Mouse wheel, or pinch reported as Ctrl+wheel: zoom about the cursor
    const double steps
        = (px.isNull() ? e->angleDelta().y() / 120.0 : px.y() / 50.0);
    m_camera->zoomAt(float(std::pow(1.15, steps)), toNdc(e->position()));
    emit zoomChanged(m_camera->zoom);
    m_cursorPos = e->position();
    emitCursorData();
    update();
    e->accept();
}

QRect
OpenGLWidget::minimapRect() const
{
    return QRect(width() - kMinimapW - kMinimapMargin,
                 height() - kMinimapH - kMinimapMargin, kMinimapW, kMinimapH);
}

void
OpenGLWidget::panToMinimapPoint(const QPointF &pos)
{
    const QRect r       = minimapRect();
    const float nx      = float((pos.x() - r.left()) / r.width() * 2.0 - 1.0);
    const float ny      = float(1.0 - (pos.y() - r.top()) / r.height() * 2.0);
    m_camera->panOffset = QVector2D(nx, ny);
    m_camera->clampPan();
    emitCursorData();
    update();
}

void
OpenGLWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && minimapVisible()
        && minimapRect().contains(e->pos()))
    {
        m_draggingMinimap = true;
        panToMinimapPoint(e->position());
        return;
    }
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
        m_draggingMinimap = false;
        m_isPanning       = false;
        unsetCursor();
    }
}

void
OpenGLWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && minimapVisible()
        && minimapRect().contains(e->pos()))
        return;
    if (e->button() == Qt::LeftButton)
    {
        m_camera->reset();
        emit zoomChanged(m_camera->zoom);
        emitCursorData();
        update();
    }
}

void
OpenGLWidget::enterEvent(QEnterEvent *e)
{
    m_cursorPos = e->position();
    emitCursorData();
    QOpenGLWidget::enterEvent(e);
}

void
OpenGLWidget::mouseMoveEvent(QMouseEvent *e)
{
    m_cursorPos = e->position();
    if (m_draggingMinimap)
    {
        panToMinimapPoint(e->position());
        return;
    }
    if (!m_isPanning)
    {
        emitCursorData();
        return;
    }

    const QPoint delta = e->pos() - m_lastMousePos;
    m_lastMousePos     = e->pos();

    m_camera->panBy(QVector2D(2.0f * delta.x() / float(width()),
                              -2.0f * delta.y() / float(height())));
    emitCursorData();
    update();
}

void
OpenGLWidget::setColorBarVisible(bool visible)
{
    if (m_colorBarVisible == visible)
        return;
    m_colorBarVisible = visible;
    update();
}

QRectF
OpenGLWidget::colorbarRect() const // device pixels, y up
{
    const double dpr = devicePixelRatioF();
    const double W   = width() * dpr;
    const double bw  = std::min(300.0 * dpr, 0.6 * W);
    const double x0  = 0.5 * (W - bw);
    return QRectF(x0, 34.0 * dpr, bw, 16.0 * dpr); // x, y, w, h
}

double
OpenGLWidget::scaleValue(double v) const
{
    if (m_colormapScale == ColormapScale::LOG)
    {
        if (m_vmin > 0)
            return (std::log(std::max(v, double(m_vmin))) - std::log(m_vmin))
                   / (std::log(m_vmax) - std::log(m_vmin));
        double u = std::clamp((v - m_vmin) / (m_vmax - m_vmin), 0.0, 1.0);
        return std::log(1.0 + 1000.0 * u) / std::log(1001.0);
    }
    return std::clamp((v - m_vmin) / (m_vmax - m_vmin), 0.0, 1.0);
}

void
OpenGLWidget::exportImage(const QString &filePath, const ExportOptions &options)
{
    if (filePath.isEmpty())
        return;

    const QImage image = grabFramebuffer();
    if (!image.save(filePath, nullptr, options.quality))
    {
        QMessageBox::critical(
            this, "Export Image",
            QString("Failed to save image to %1").arg(filePath));
    }
}

void
OpenGLWidget::resetView()
{
    m_camera->reset();
    emit zoomChanged(m_camera->zoom);
    emitCursorData();
    update();
}

void
OpenGLWidget::setZoom(float zoom)
{
    m_camera->adjustZoom(zoom);
    emit zoomChanged(m_camera->zoom);
    emitCursorData();
    update();
}
