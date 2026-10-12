#include "openglwidget.hpp"

#include "colormapregistry.hpp"
#include "fitsinfo.hpp"
#include "npyreader.hpp"
#include "healpix_cxx/error_handling.h"
#include "healpix_cxx/fitshandle.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "healpix_map.h"
#include "parallel.hpp"
#include "projectionmath.hpp"
#include "shaderloader.hpp"

#include <QEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVector3D>
#include <QWheelEvent>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <healpix_base.h>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <unordered_map>
#include <pointing.h>
#include <qcoreapplication.h>
#include <qsurfaceformat.h>
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
    setFocusPolicy(Qt::ClickFocus);
    m_globeTimer.setInterval(16);
    connect(&m_globeTimer, &QTimer::timeout, this,
            &OpenGLWidget::tickGlobeAnimation);
    connect(&ColormapRegistry::instance(), &ColormapRegistry::changed, this,
            [this]()
    {
        m_lutDirty = true;
        update();
    });
    m_cursorPos = QPointF(width() / 2.0, height() / 2.0);
    setCursor(Qt::CrossCursor);
    emitCursorData();
}

OpenGLWidget::~OpenGLWidget()
{
    makeCurrent();
    deleteMapTextures();
    deleteMaskTextures();
    if (m_tiles)
        m_tiles->destroy();
    doneCurrent();
    if (m_lutTexture)
    {
        makeCurrent();
        glDeleteTextures(1, &m_lutTexture);
        m_lutTexture = 0;
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
OpenGLWidget::setStreamingNside(int nside)
{
    if (nside == m_streamNside)
        return;
    m_streamNside = nside;
    // Only a map already on the GPU whose mode would change is re-uploaded
    if (m_nside > 0)
    {
        const bool streams = m_nside > nside && TileCache::supports(m_nside);
        if (streams != (m_levelShift > 0))
            m_needsReupload = true;
    }
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

std::vector<uint8_t>
OpenGLWidget::keepMask() const
{
    const long npix = m_pendingMap.Npix();
    struct Active
    {
        const uint8_t *data;
        bool inverted;
    };
    std::vector<Active> active;
    for (const MaskEntry &m : m_masks)
        if (m.enabled && m.data.size() == size_t(npix))
            active.push_back({m.data.data(), m.inverted});
    if (active.empty())
        return {};

    std::vector<uint8_t> keep(size_t(npix), 1);
#pragma omp parallel for schedule(static)
    for (long i = 0; i < npix; ++i)
    {
        float k = 1.0f; // the same product as the shader and the statistics
        for (const Active &m : active)
        {
            const float w = m.data[i] * (1.0f / 255.0f);
            k *= m.inverted ? 1.0f - w : w;
        }
        keep[size_t(i)] = k >= 0.5f ? 1 : 0;
    }
    return keep;
}

void
OpenGLWidget::recomputeDataRange()
{
    const long totalPixels = m_pendingMap.Npix();

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

    struct Stats
    {
        long valid   = 0;
        double sum   = 0.0; // for the mask-aware average / rms
        double sumSq = 0.0;
        float min    = std::numeric_limits<float>::max();
        float max    = std::numeric_limits<float>::lowest();
    };

    const float *data = m_pendingMap.Map().begin();

    // One chunk of the map per thread
    const Stats st = parallel::reduceRange<Stats>(totalPixels,
                                                  [&](long begin, long end)
    {
        Stats s;
        for (long i = begin; i < end; ++i)
        {
            const float v = data[i];
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
            ++s.valid;
            s.sum += v;
            s.sumSq += double(v) * v;
            s.min = std::min(s.min, v);
            s.max = std::max(s.max, v);
        }
        return s;
    }, [](Stats &total, const Stats &part)
    {
        total.valid += part.valid;
        total.sum += part.sum;
        total.sumSq += part.sumSq;
        total.min = std::min(total.min, part.min);
        total.max = std::max(total.max, part.max);
    });

    const long validPixels = st.valid;
    m_dataMin              = st.min;
    m_dataMax              = st.max;

    // Same definitions as Healpix_Map::average() / rms() (rms is about zero),
    // but over the surviving pixels only
    m_mapAverage = validPixels ? st.sum / validPixels : 0.0;
    m_mapRms     = validPixels ? std::sqrt(st.sumSq / validPixels) : 0.0;
    // Standard deviation about the mean (clamped: rounding can go negative)
    m_mapStd = validPixels
                   ? std::sqrt(std::max(0.0, st.sumSq / validPixels
                                                 - m_mapAverage * m_mapAverage))
                   : 0.0;
    // Extremes of the surviving pixels (before the empty-range fallbacks below)
    m_mapMin = validPixels ? m_dataMin : 0.0f;
    m_mapMax = validPixels ? m_dataMax : 0.0f;

    m_fsky = totalPixels > 0 ? float(validPixels) / float(totalPixels) : 0.0f;

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
OpenGLWidget::setMap(Healpix_Map<float> &&map)
{
    // Masks only make sense for the resolution they were loaded for
    const bool sizeChanged = map.Npix() != m_pendingMap.Npix();

    m_pendingMap.swap(map); // takes the buffer without copying it
    m_hasPendingMap = true;

    m_pixelOrdering = static_cast<PixelOrdering>(m_pendingMap.Scheme());
    if (sizeChanged && !m_masks.empty())
    {
        m_masks.clear();
        m_maskDirty = true;
        emit masksChanged();
    }

    recomputeDataRange();
    m_vmin = m_dataMin;
    m_vmax = m_dataMax;

    // With a live context, upload now rather than on the next paint
    if (isValid())
    {
        uploadMapToGPU(m_pendingMap);
        m_hasPendingMap = false;
        emit mapLoaded(m_pendingMap.Nside(), m_pendingMap.Npix(), m_mapAverage,
                       m_mapRms, m_mapStd, m_mapMin, m_mapMax);
    }

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

// Text is drawn as filled outlines instead of with QPainter::drawText. On a
// QOpenGLWidget drawText goes through Qt's GL glyph cache, which is attached
// to the (global) font engine and is destroyed during process exit, after
// Qt's per-thread GL state is already gone, so exit() could segfault when a
// style plugin keeps a font alive.
static void
drawTextPath(QPainter &p, const QPointF &baseline, const QString &text,
             const QColor &color)
{
    QPainterPath path;
    path.addText(baseline, p.font(), text);
    p.fillPath(path, color);
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

        emit mapLoaded(m_pendingMap.Nside(), m_pendingMap.Npix(), m_mapAverage,
                       m_mapRms, m_mapStd, m_mapMin, m_mapMax);
    }

    if (m_needsReupload)
    {
        m_needsReupload = false;
        uploadMapToGPU(m_pendingMap);
        m_maskDirty = !m_masks.empty();
    }
    if (m_streaming)
        updateTiles();
    if (m_maskDirty)
        uploadMaskToGPU();
    if (m_lutDirty)
        uploadColormapLut();

    m_shaderProgram.bind();

    m_camera->uploadUniforms(m_shaderProgram);

    // 1. Bind the HEALPix map and the masks (arrays of face textures)
    bindDataTextures(true);
    // Custom colormaps
    glActiveTexture(GL_TEXTURE0 + kLutUnit);
    glBindTexture(GL_TEXTURE_2D, m_lutTexture);
    m_shaderProgram.setUniformValue("colormapLut", kLutUnit);
    m_shaderProgram.setUniformValue("lut_rows", m_lutRows);
    m_shaderProgram.setUniformValue("seg_layers", m_segLayers);
    m_shaderProgram.setUniformValue("level_shift", m_levelShift);
    m_shaderProgram.setUniformValue("streaming", m_streaming);
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
    m_shaderProgram.setUniformValue("vmin", m_vmin);
    m_shaderProgram.setUniformValue("vmax", m_vmax);
    m_shaderProgram.setUniformValue("colormap", static_cast<int>(m_colormap));
    m_shaderProgram.setUniformValue("colormap_scale",
                                    static_cast<int>(m_colormapScale));
    m_shaderProgram.setUniformValue(
        "bg_color",
        QVector3D(m_bgColor.redF(), m_bgColor.greenF(), m_bgColor.blueF()));
    m_shaderProgram.setUniformValue(
        "unseen_color", QVector3D(m_unseenColor.redF(), m_unseenColor.greenF(),
                                  m_unseenColor.blueF()));
    m_shaderProgram.setUniformValue("invert_colormap",
                                    m_invertColormap ? 1 : 0);
    m_shaderProgram.setUniformValue("view_scale", viewScale());
    m_shaderProgram.setUniformValue("colorbar_enabled", m_colorBarVisible);
    m_shaderProgram.setUniformValue("graticule_enabled", m_graticuleEnabled);
    m_shaderProgram.setUniformValue("globe_shadow", m_globeShadow);
    m_shaderProgram.setUniformValue("view_outside", m_viewOutside);
    m_shaderProgram.setUniformValue("limb_darkening", m_limbDarkening);
    m_shaderProgram.setUniformValue(
        "graticule_step", float(qDegreesToRadians(m_graticuleStepDeg)));
    m_shaderProgram.setUniformValue(
        "graticule_color",
        QVector4D(m_graticuleColor.redF(), m_graticuleColor.greenF(),
                  m_graticuleColor.blueF(), m_graticuleColor.alphaF()));
    m_shaderProgram.setUniformValue(
        "colorbar_rect",
        QVector4D(colorbar_rect.left(), colorbar_rect.top(),
                  colorbar_rect.right(), colorbar_rect.bottom()));
    m_shaderProgram.setUniformValue("projection_type",
                                    static_cast<int>(m_projection));
    m_shaderProgram.setUniformValue("globe_rotation",
                                    m_rotation.toRotationMatrix());
    m_shaderProgram.setUniformValue(
        "frame_to_native", skyframe::rotation(displayFrame(), m_mapFrame));

    m_shaderProgram.setUniformValue("out_alpha", 1.0f);
    const float fade = projectionFade();
    m_vao.bind();
    if (fade < 1.0f)
    {
        // The old projection, opaque, then the new one fading in over it
        m_shaderProgram.setUniformValue("projection_type",
                                        static_cast<int>(m_prevProjection));
        m_shaderProgram.setUniformValue("view_scale",
                                        viewScale(m_prevProjection));
        glDrawArrays(GL_TRIANGLES, 0, 6);

        m_shaderProgram.setUniformValue("projection_type",
                                        static_cast<int>(m_projection));
        m_shaderProgram.setUniformValue("view_scale", viewScale());
        m_shaderProgram.setUniformValue("out_alpha", fade);
        glEnable(GL_BLEND);
        // Keep the framebuffer's alpha at 1: the widget composites it
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO,
                            GL_ONE);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisable(GL_BLEND);
        m_shaderProgram.setUniformValue("out_alpha", 1.0f);
        QTimer::singleShot(0, this, [this]() { update(); });
    }
    else
        glDrawArrays(GL_TRIANGLES, 0, 6);
    m_vao.release();

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    // Minimap: the whole map in a small 2:1 viewport (bottom-right corner)
    const double dpr        = devicePixelRatioF();
    const QRect minimapRect = this->minimapRect();
    // Only useful once the view is smaller than the whole map
    const bool showMinimap  = minimapVisible();
    if (showMinimap)
    {
        const GLint physW = qRound(minimapRect.width() * dpr);
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
        m_shaderProgram.setUniformValue("graticule_enabled", false);
        m_shaderProgram.setUniformValue("view_scale", QVector2D(1.0f, 1.0f));

        bindDataTextures(false);

        m_vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_vao.release();

        glViewport(0, 0, qRound(width() * dpr), qRound(height() * dpr));
    }

    m_shaderProgram.release();

    if (!m_colorBarVisible && !showMinimap && !m_isRegionSelecting
        && !m_graticuleEnabled)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (showMinimap)
    {
        // The view window spans panOffset +/- 1/zoom on the projection
        // plane [-1, 1]^2, which the minimap shows in full
        const double hw = 0.5 * minimapRect.width();
        const double hh = 0.5 * kMinimapH;
        const double cx
            = minimapRect.left() + hw + double(m_camera->panOffset.x()) * hw;
        const double cy
            = minimapRect.top() + hh - double(m_camera->panOffset.y()) * hh;
        // The window shows viewScale() / zoom of the plane in each direction
        const QVector2D vs = viewScale();
        const double vw    = hw * vs.x() / m_camera->zoom;
        const double vh    = hh * vs.y() / m_camera->zoom;

        p.setClipRect(minimapRect);
        p.setPen(QPen(QColor(255, 80, 80), 1.5));
        p.setBrush(QColor(255, 80, 80, 40));
        p.drawRect(QRectF(cx - vw, cy - vh, 2 * vw, 2 * vh));
        p.setClipping(false);

        p.setPen(QPen(QColor(200, 200, 200, 200), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(minimapRect.adjusted(0, 0, -1, -1));
    }

    if (m_graticuleEnabled && !m_projectionFading)
        paintGraticuleLabels(p);

    if (m_isRegionSelecting)
    {
        p.setPen(QPen(QColor(255, 80, 80), 1.5));
        p.setBrush(QColor(255, 80, 80, 40));
        QRectF r(m_regionStart, m_regionEnd);
        r = QRectF(std::min(r.left(), r.right()), std::min(r.top(), r.bottom()),
                   std::abs(r.width()), std::abs(r.height()));
        p.drawRect(r);
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
        drawTextPath(p, QPointF(t.x - t.w / 2, bottom + 18), t.label,
                     Qt::white);
    }
}

// Reads the files on a worker thread (they can be large), then adds the
// masks on the GUI thread. Messages are collected and shown there too.
void
OpenGLWidget::addMaskFiles(const QStringList &files)
{
    if (m_pendingMap.Npix() == 0)
    {
        QMessageBox::warning(this, "Mask", "Load a map before loading a mask.");
        return;
    }

    // Skip files that are already loaded
    QStringList todo;
    for (const QString &file : files)
    {
        const QString path = QFileInfo(file).absoluteFilePath();
        const bool dup
            = std::any_of(m_masks.begin(), m_masks.end(),
                          [&](const MaskEntry &m) { return m.path == path; });
        if (!dup && !todo.contains(file))
            todo << file;
    }
    if (todo.isEmpty())
        return;

    struct Result
    {
        std::vector<MaskEntry> masks;
        QStringList warnings;
        QStringList errors;
    };

    const long mapNside  = m_pendingMap.Nside();
    const long mapNpix   = m_pendingMap.Npix();
    const auto mapScheme = m_pendingMap.Scheme();

    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this,
            [this, watcher, mapNpix]()
    {
        Result result = watcher->result();
        watcher->deleteLater();

        // The map may have been replaced while the masks were loading
        if (m_pendingMap.Npix() != mapNpix)
            return;

        int added = 0;
        for (MaskEntry &entry : result.masks)
        {
            if (maskCount() >= kMaxMasks)
            {
                result.warnings << QString("At most %1 masks can be loaded.")
                                       .arg(kMaxMasks);
                break;
            }
            const bool dup = std::any_of(m_masks.begin(), m_masks.end(),
                                         [&](const MaskEntry &m)
            { return m.path == entry.path; });
            if (dup)
                continue;
            m_masks.push_back(std::move(entry));
            ++added;
        }

        if (!result.warnings.isEmpty())
            QMessageBox::warning(this, "Mask", result.warnings.join("\n"));
        if (!result.errors.isEmpty())
            QMessageBox::critical(this, "Mask", result.errors.join("\n"));
        if (added)
            applyMaskChange(true);
    });

    watcher->setFuture(QtConcurrent::run([todo, mapNside, mapNpix, mapScheme]()
    {
        Result result;
        for (const QString &file : todo)
        {
            try
            {
                // Use the file's first HEALPix map HDU / column (masks have
                // their own layout, independent of the map's selection)
                Healpix_Map<float> mask;
                if (isNpyPath(file.toStdString()))
                    mask = std::move(
                        *readNpyMap(file.toStdString(), 0, kMaxSupportedNside));
                else
                {
                    fitshandle fits;
                    fits.open(file.toStdString());
                    const FitsInfo info = scanFits(fits);
                    if (info.hdus.isEmpty())
                    {
                        result.errors << QString("%1: no HEALPix map found.")
                                             .arg(QFileInfo(file).fileName());
                        continue;
                    }
                    fits.goto_hdu(info.hdus.first().index);
                    read_Healpix_map_from_fits(
                        fits, mask, info.hdus.first().columns.first().index);
                }

                if (mask.Nside() != mapNside)
                {
                    result.warnings
                        << QString("%1: nside %2 does not match the map "
                                   "(nside %3).")
                               .arg(QFileInfo(file).fileName())
                               .arg(mask.Nside())
                               .arg(mapNside);
                    continue;
                }
                if (mask.Scheme() != mapScheme)
                    mask.swap_scheme();

                MaskEntry entry;
                entry.name = QFileInfo(file).fileName();
                entry.path = QFileInfo(file).absoluteFilePath();
                entry.data.resize(size_t(mapNpix));
                const float *d = mask.Map().begin();
                uint8_t *out   = entry.data.data();
                parallel::forRange(mapNpix, [d, out](long begin, long end)
                {
                    for (long i = begin; i < end; ++i)
                    {
                        // NaN / UNSEEN count as masked; weights clamp to [0, 1]
                        const float v = (std::isfinite(d[i]) && d[i] > -1e30f)
                                            ? d[i]
                                            : 0.0f;
                        out[i]        = static_cast<uint8_t>(
                            std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                    }
                });
                result.masks.push_back(std::move(entry));
            }
            catch (const PlanckError &e)
            {
                result.errors << QString("Failed to load %1: %2")
                                     .arg(file, QString(e.what()));
            }
            catch (const std::exception &e)
            {
                result.errors << QString("Failed to load %1: %2")
                                     .arg(file, QString(e.what()));
            }
        }
        return result;
    }));
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
// Rebuilds the texture of user colormaps (a row per colormap; one dummy row
// when there are none, so the sampler is always valid)
void
OpenGLWidget::uploadColormapLut()
{
    m_lutDirty = false;

    const auto &registry = ColormapRegistry::instance();
    QByteArray data      = registry.lutData();
    m_lutRows            = std::max(1, registry.customCount());
    if (data.isEmpty())
        data = QByteArray(ColormapRegistry::kLutSize * 3, '\0');

    if (!m_lutTexture)
        glGenTextures(1, &m_lutTexture);
    glBindTexture(GL_TEXTURE_2D, m_lutTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, ColormapRegistry::kLutSize,
                 m_lutRows, 0, GL_RGB, GL_UNSIGNED_BYTE, data.constData());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// The texel of face f at (x, y) holds map pixel xyf2pix(x, y, f), so the GPU
// never deals with pixel numbers: it finds (x, y, f) from the direction and
// reads that texel, whatever the map's ordering. The texture may be a coarser
// level (texNside < nside): each texel then stands for a block of
// (nside / texNside)^2 pixels. Calls put(texel, pixels, count) for every
// texel of `face` (row-major, texNside wide), in parallel.
template <class Put>
static void
gatherFace(const Healpix_Base &base, int face, int texNside, Put put)
{
    const int r = base.Nside() / texNside;
    parallel::forRange(texNside, [&](long y0, long y1) {
        std::vector<int> pix(size_t(r) * r);
        for (long y = y0; y < y1; ++y)
            for (int x = 0; x < texNside; ++x)
            {
                int n = 0;
                for (int dy = 0; dy < r; ++dy)
                    for (int dx = 0; dx < r; ++dx)
                        pix[size_t(n++)]
                            = base.xyf2pix(x * r + dx, int(y) * r + dy, face);
                put(size_t(y) * texNside + x, pix.data(), n);
            }
    });
}

void
OpenGLWidget::deleteMapTextures()
{
    glDeleteTextures(int(m_mapTextures.size()), m_mapTextures.data());
    m_mapTextures.fill(0);
}

void
OpenGLWidget::deleteMaskTextures()
{
    glDeleteTextures(int(m_maskTextures.size()), m_maskTextures.data());
    m_maskTextures.fill(0);
}

void
OpenGLWidget::bindDataTextures(bool setSamplers)
{
    const int segs = 12 / m_segLayers;
    auto setUnit   = [&](const QByteArray &name, int unit) {
        if (setSamplers)
            m_shaderProgram.setUniformValue(name.constData(), unit);
    };
    // Samplers of unused segments point at the first segment's unit (same
    // sampler type, never read), which frees units for the tile textures
    for (int k = 0; k < kMaxSegments; ++k)
    {
        glActiveTexture(GL_TEXTURE0 + k);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_mapTextures[k]);
        setUnit("mapTexture" + QByteArray::number(k), k < segs ? k : 0);
    }
    for (int g = 0; g < 4; ++g)
        for (int k = 0; k < kMaxSegments; ++k)
        {
            const int unit = kMaxSegments + g * kMaxSegments + k;
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D_ARRAY,
                          m_maskTextures[g * kMaxSegments + k]);
            setUnit("maskTex" + QByteArray::number(g) + "_"
                        + QByteArray::number(k),
                    k < segs ? unit : kMaxSegments + g * kMaxSegments);
        }
    // The tile textures take the units of the map segments 1 and 2, which a
    // streamed (coarse, single segment) map does not use. Without streaming
    // the tile samplers just alias unit 0 and are never read.
    const bool tiles = m_streaming && m_tiles && m_tiles->atlas();
    if (tiles)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_tiles->atlas());
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_tiles->pages());
    }
    setUnit("tileAtlas", tiles ? 1 : 0);
    setUnit("tilePages", tiles ? 2 : 0);
    glActiveTexture(GL_TEXTURE0);
}

bool
OpenGLWidget::allocSegment(GLuint &tex, GLenum internalFormat, GLenum format,
                           GLenum type)
{
    const int ns = int(m_texNside);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glGetError(); // clear
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalFormat, ns, ns, m_segLayers,
                 0, format, type, nullptr);
    if (const GLenum e = glGetError(); e != GL_NO_ERROR)
    {
        std::cerr << "[GL] allocating a " << ns << "x" << ns << "x"
                  << m_segLayers << " texture failed, error 0x" << std::hex << e
                  << std::dec << std::endl;
        glDeleteTextures(1, &tex);
        tex = 0;
        return false;
    }
    // NEAREST keeps crisp pixel edges
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

// The face layers are spread over textures of at most 1 GiB: some drivers
// (Mesa on Intel) refuse larger single textures even with plenty of memory.
static int
segmentLayers(int nside)
{
    constexpr size_t kMaxBytes = size_t(1) << 30;
    for (int layers : {12, 6, 4, 3, 2, 1})
        if (size_t(layers) * nside * nside * 4 <= kMaxBytes)
            return layers;
    return 1;
}

void
OpenGLWidget::uploadMaskToGPU()
{
    makeCurrent();
    deleteMaskTextures();
    m_maskDirty = false;

    if (m_masks.empty())
        return;

    // With streaming the masks are only kept at the coarse level
    const int ns = int(m_texNside);
    const Healpix_Base base(int(m_nside),
                            m_pixelOrdering == PixelOrdering::NESTED ? NEST
                                                                     : RING,
                            SET_NSIDE);
    std::vector<uint8_t> buf(size_t(ns) * ns * 4);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (size_t g = 0; g * 4 < m_masks.size(); ++g)
        for (int seg = 0; seg < 12 / m_segLayers; ++seg)
        {
            GLuint &tex = m_maskTextures[g * kMaxSegments + seg];
            if (!allocSegment(tex, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE))
            {
                deleteMaskTextures();
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                QMessageBox::warning(this, "Out of GPU memory",
                                     "The masks do not fit in video memory.");
                return;
            }
            for (int l = 0; l < m_segLayers; ++l)
            {
                gatherFace(
                    base, seg * m_segLayers + l, ns,
                    [&](size_t texel, const int *pix, int n) {
                        for (size_t c = 0; c < 4; ++c)
                        {
                            // Unused channels stay 255 (keep)
                            const size_t m = g * 4 + c;
                            if (m >= m_masks.size())
                            {
                                buf[texel * 4 + c] = 255;
                                continue;
                            }
                            const std::vector<uint8_t> &d = m_masks[m].data;
                            int sum                       = 0;
                            for (int i = 0; i < n; ++i)
                                sum += d[size_t(pix[i])];
                            buf[texel * 4 + c] = uint8_t((sum + n / 2) / n);
                        }
                    });
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, l, ns, ns, 1,
                                GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            }
        }
    // Restore the default: Qt's text renderer relies on it
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
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
    deleteMapTextures();
    if (m_tiles)
        m_tiles->destroy();
    m_tiles.reset();

    // Streaming: keep a coarse level (at most nside 2048, and not above the
    // user's limit) resident, and the rest as tiles on demand
    const int ns   = int(m_nside);
    m_streaming    = false;
    m_levelShift   = 0;
    if (ns > m_streamNside && TileCache::supports(ns))
    {
        const int limit = std::min(m_streamNside, 2048);
        while ((ns >> m_levelShift) > limit)
            ++m_levelShift;
        m_levelShift = std::max(m_levelShift, 1);
    }
    m_texNside = ns >> m_levelShift;

    GLint maxSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    if (m_texNside > maxSize)
    {
        std::cerr << "[GL] nside " << m_texNside
                  << " exceeds GL_MAX_TEXTURE_SIZE " << maxSize << std::endl;
        QMessageBox::warning(
            this, "Map too large",
            QString("nside %1 needs %1x%1 textures, but this GPU supports "
                    "at most %2. Lower the streaming limit in the settings.")
                .arg(m_texNside)
                .arg(maxSize));
        return;
    }

    const int tex = int(m_texNside);
    m_segLayers   = segmentLayers(tex);
    const Healpix_Base base(ns, map.Scheme(), SET_NSIDE);
    std::vector<float> buf(size_t(tex) * tex); // one face at a time

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int seg = 0; seg < 12 / m_segLayers; ++seg)
    {
        if (!allocSegment(m_mapTextures[seg], GL_R32F, GL_RED, GL_FLOAT))
        {
            deleteMapTextures();
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            QMessageBox::warning(
                this, "Out of GPU memory",
                QString("nside %1 needs %2 MB of video memory. Lower the "
                        "streaming limit in the settings.")
                    .arg(tex)
                    .arg(12.0 * tex * tex * 4 / 1048576.0, 0, 'f', 0));
            return;
        }
        for (int l = 0; l < m_segLayers; ++l)
        {
            gatherFace(base, seg * m_segLayers + l, tex,
                       [&](size_t texel, const int *pix, int n) {
                           if (n == 1)
                           {
                               buf[texel] = mapDataPtr[pix[0]];
                               return;
                           }
                           // Average of the valid pixels of the block
                           double sum = 0.0;
                           int valid  = 0;
                           for (int i = 0; i < n; ++i)
                           {
                               const float v = mapDataPtr[pix[i]];
                               if (v > -1.0e30f && std::isfinite(v))
                               {
                                   sum += v;
                                   ++valid;
                               }
                           }
                           buf[texel] = valid ? float(sum / valid)
                                              : -1.6375e30f; // UNSEEN
                       });
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, l, tex, tex, 1,
                            GL_RED, GL_FLOAT, buf.data());
        }
    }
    // Restore the default: Qt's text renderer relies on it
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (const GLenum e = glGetError(); e != GL_NO_ERROR)
        std::cerr << "[GL] uploading the map failed, error 0x" << std::hex << e
                  << std::dec << std::endl;
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    if (m_levelShift > 0)
    {
        m_tiles = std::make_unique<TileCache>();
        // 256 tiles = 256 MB; fewer if the GPU cannot spare that
        if (m_tiles->create(ns, 256))
            m_streaming = true;
        else
        {
            m_tiles.reset();
            std::cerr << "[GL] tile streaming unavailable; showing the "
                         "nside "
                      << m_texNside << " version" << std::endl;
        }
    }
}

static QVector3D
directionFromAngles(double colat, double phi);
static void
anglesFromDirection(const QVector3D &v, double &colat, double &phi);

// The sky direction (in the map's frame) shown at a widget position; the
// same mapping as the fragment shader. False outside the projection.
static bool
mapDirectionAt(const QVector2D &plane, Projection projection, bool outside,
               const QQuaternion &rotation, const QGenericMatrix<3, 3, float> &toMap,
               QVector3D &dir)
{
    double colat, phi;
    if (!projectionmath::inverse(projection, plane, colat, phi, outside))
        return false;
    const QVector3D shown = rotation.rotatedVector(directionFromAngles(colat, phi));
    dir = skyframe::apply(toMap, shown);
    return true;
}

std::vector<int>
OpenGLWidget::visibleTiles() const
{
    // Sample the view on a grid. Where a screen pixel covers less sky than a
    // texel of the coarse map, the coarse map is blocky and the full-resolution
    // tile under the sample is wanted.
    constexpr int kGrid = 96;
    const int t         = m_tiles->tilesPerSide();
    const double coarse = std::sqrt(4.0 * M_PI / 12.0) / double(m_texNside);
    const auto toMap
        = skyframe::rotation(displayFrame(), m_mapFrame);
    const Healpix_Base base(int(m_nside), m_pixelOrdering == PixelOrdering::NESTED ? NEST : RING,
                            SET_NSIDE);

    const double stepX = double(width()) / (kGrid - 1);
    const double stepY = double(height()) / (kGrid - 1);
    const QPointF centre(width() / 2.0, height() / 2.0);

    std::vector<QVector3D> dirs(size_t(kGrid) * kGrid);
    std::vector<char> ok(size_t(kGrid) * kGrid, 0);
    for (int j = 0; j < kGrid; ++j)
        for (int i = 0; i < kGrid; ++i)
        {
            const QVector2D p
                = toNdc(QPointF(i * stepX, j * stepY)) / m_camera->zoom
                  + m_camera->panOffset;
            const size_t k = size_t(j) * kGrid + i;
            ok[k] = mapDirectionAt(p, m_projection, m_viewOutside, m_rotation,
                                   toMap, dirs[k]);
        }

    std::unordered_map<int, double> nearest; // tile -> distance from centre
    for (int j = 0; j < kGrid; ++j)
        for (int i = 0; i + 1 < kGrid; ++i)
        {
            const size_t k = size_t(j) * kGrid + i;
            if (!ok[k] || !ok[k + 1])
                continue;
            const double dot = std::clamp(
                double(QVector3D::dotProduct(dirs[k], dirs[k + 1])), -1.0, 1.0);
            const double perPixel = std::acos(dot) / stepX;
            if (perPixel >= coarse)
                continue; // the coarse map is as fine as the screen
            double colat, phi;
            anglesFromDirection(dirs[k], colat, phi);
            int ix, iy, face;
            base.pix2xyf(base.ang2pix(pointing(colat, phi)), ix, iy, face);
            const int key = TileCache::key(face, ix / TileCache::kTileSize,
                                           iy / TileCache::kTileSize, t);
            const double d = std::hypot(i * stepX - centre.x(),
                                        j * stepY - centre.y());
            auto [it, inserted] = nearest.try_emplace(key, d);
            if (!inserted)
                it->second = std::min(it->second, d);
        }

    std::vector<std::pair<double, int>> order;
    order.reserve(nearest.size());
    for (const auto &[key, d] : nearest)
        order.emplace_back(d, key);
    std::sort(order.begin(), order.end());
    std::vector<int> wanted;
    for (size_t i = 0; i < order.size() && int(i) < m_tiles->capacity(); ++i)
        wanted.push_back(order[i].second);
    return wanted;
}

void
OpenGLWidget::updateTiles()
{
    if (!m_tiles || m_pendingMap.Npix() == 0)
        return;
    // A few tiles per frame keep the view responsive; more follow
    const bool more
        = m_tiles->update(m_pendingMap, visibleTiles(), 6);
    if (more)
        QTimer::singleShot(0, this, [this]() { update(); });
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

// Unit vector for a colatitude / longitude pair, and back
static QVector3D
directionFromAngles(double colat, double phi)
{
    return QVector3D(float(std::sin(colat) * std::cos(phi)),
                     float(std::sin(colat) * std::sin(phi)),
                     float(std::cos(colat)));
}

static void
anglesFromDirection(const QVector3D &v, double &colat, double &phi)
{
    colat = std::atan2(std::hypot(double(v.x()), double(v.y())), double(v.z()));
    phi   = std::atan2(double(v.y()), double(v.x()));
    if (phi < 0.0)
        phi += 2.0 * M_PI;
}

void
OpenGLWidget::emitCursorData()
{
    // Same mapping as the fragment shader: NDC -> camera -> projection
    const QVector2D ndc = toNdc(m_cursorPos);
    const QVector2D p   = ndc / m_camera->zoom + m_camera->panOffset;
    double colat, phi;
    if (m_nside <= 0 || m_pendingMap.Npix() == 0
        || !projectionmath::inverse(m_projection, p, colat, phi, m_viewOutside))
    {
        emit cursorPosChanged(QPointF(), QPointF(), std::nan(""), -1);
        return;
    }
    // View frame -> the displayed frame (the same rotation the shader applies)
    const QVector3D shown
        = m_rotation.rotatedVector(directionFromAngles(colat, phi));
    double shownColat, shownPhi;
    anglesFromDirection(shown, shownColat, shownPhi);

    // The pixel is looked up in the frame the map is stored in
    double mapColat, mapPhi;
    anglesFromDirection(
        skyframe::apply(skyframe::rotation(displayFrame(), m_mapFrame), shown),
        mapColat, mapPhi);

    const Healpix_Base base(
        static_cast<int>(m_nside),
        m_pixelOrdering == PixelOrdering::NESTED ? NEST : RING, SET_NSIDE);
    const qlonglong idx = base.ang2pix(pointing(mapColat, mapPhi));
    float v             = m_pendingMap.Map()[idx];
    if (v < -1.0e30f || !std::isfinite(v))
        v = std::nanf("");

    double lon = shownPhi * 180.0 / M_PI;
    if (lon > 180.0)
        lon -= 360.0;
    auto lonlat = QPointF(lon, 90.0 - shownColat * 180.0 / M_PI);

    // RA / Dec of the same direction
    double raColat, raPhi;
    anglesFromDirection(
        skyframe::apply(
            skyframe::rotation(displayFrame(), SkyFrame::Equatorial), shown),
        raColat, raPhi);
    auto radec = QPointF(raPhi * 180.0 / M_PI, 90.0 - raColat * 180.0 / M_PI);
    emit cursorPosChanged(lonlat, radec, v, idx);
}

QVector2D
OpenGLWidget::toNdc(const QPointF &pos) const
{
    // The map occupies the window minus kMapPadding on each side, so the
    // [-1, 1] range maps to that inner rectangle (see view_scale in the shader)
    const QVector2D s = viewScale();
    return QVector2D((2.0f * float(pos.x()) / width() - 1.0f) * s.x(),
                     (1.0f - 2.0f * float(pos.y()) / height()) * s.y());
}

QVector2D
OpenGLWidget::viewScale(Projection projection) const
{
    // Largest box with the projection's aspect (2:1, or 1:1 for the disks)
    // that fits inside the window minus the padding, centered. Window NDC
    // divided by the box's share of the window gives map NDC in [-1, 1]
    // inside the box.
    const float aspect = float(projectionmath::aspect(projection));
    const float w      = float(std::max(width(), 1));
    const float h      = float(std::max(height(), 1));
    const float pad    = float(kMapPadding);
    const float aw     = std::max(w - 2.0f * pad, 1.0f);
    const float ah     = std::max(h - 2.0f * pad, 1.0f);
    const float bw     = std::min(aw, aspect * ah);
    const float bh     = bw / aspect;
    return QVector2D(w / bw, h / bh);
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
        const QVector2D s = viewScale();
        m_camera->panBy(QVector2D(2.0f * px.x() / float(width()) * s.x(),
                                  -2.0f * px.y() / float(height()) * s.y()));
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

// The thumbnail has the projection's aspect (2:1, or square for the disks)
int
OpenGLWidget::minimapWidth() const
{
    return qRound(kMinimapH * projectionmath::aspect(m_projection));
}

QRect
OpenGLWidget::minimapRect() const
{
    const int w = minimapWidth();
    return QRect(width() - w - kMinimapMargin,
                 height() - kMinimapH - kMinimapMargin, w, kMinimapH);
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

// View-frame (unrotated) direction of the sky under a widget position. False
// outside the projection's shape.
bool
OpenGLWidget::viewDirectionAt(const QPointF &pos, QVector3D &dir) const
{
    const QVector2D p = toNdc(pos) / m_camera->zoom + m_camera->panOffset;
    double colat, phi;
    if (!projectionmath::inverse(m_projection, p, colat, phi, m_viewOutside))
        return false;
    dir = directionFromAngles(colat, phi);
    return true;
}

// Drag-to-rotate: remember which sky point is under the cursor...
bool
OpenGLWidget::startRotation(const QPointF &pos)
{
    if (!viewDirectionAt(pos, m_rotateGrab))
        return false;
    stopGlobeAnimation();
    m_dragClock.invalidate();
    m_isRotating    = true;
    m_rotationStart = m_rotation;
    setCursor(Qt::ClosedHandCursor);
    m_dragClock.start();
    return true;
}

// ...and keep it there: rotate so the direction now under the cursor (v1)
// ends up where the grabbed one (v0) was, i.e. R = R0 * rotation(v1 -> v0)
void
OpenGLWidget::updateRotation(const QPointF &pos)
{
    QVector3D v1;
    if (!viewDirectionAt(pos, v1))
    {
        // The cursor left the shape. Orthographic keeps following it like a
        // trackball: past the limb the surface carries on over the horizon,
        // so the further out the cursor goes the more the globe tilts away.
        // Other projections keep the last rotation.
        if (m_projection != Projection::ORTHOGRAPHIC)
            return;
        const QVector2D p = toNdc(pos) / m_camera->zoom + m_camera->panOffset;
        const float r     = p.length();
        if (r < 1e-6f)
            return;

        double colat, phi;
        if (!projectionmath::inverse(m_projection, p * (0.9999f / r), colat,
                                     phi, m_viewOutside))
            return;
        const QVector3D limb = directionFromAngles(colat, phi); // on the edge
        if (!projectionmath::inverse(m_projection, QVector2D(0, 0), colat, phi,
                                     m_viewOutside))
            return;
        const QVector3D front = directionFromAngles(colat, phi); // disc center

        // Angle from the view axis: 90 deg at the limb, growing with the
        // distance outside it (one globe radius out = one more radian),
        // capped at the far pole
        const float theta = std::min(float(M_PI), float(M_PI_2) + (r - 1.0f));
        v1                = limb * std::sin(theta) + front * std::cos(theta);
    }

    const QQuaternion previous = m_rotation;
    QQuaternion next
        = (m_rotationStart * QQuaternion::rotationTo(v1, m_rotateGrab))
              .normalized();
    if (orientationLocked())
        next = withoutRoll(next);
    setRotation(next);

    // Angular velocity of this step (in the globe's own frame), smoothed
    if (globeAnimates() && m_dragClock.isValid())
    {
        const double dt = m_dragClock.nsecsElapsed() * 1e-9;
        if (dt > 1e-4)
        {
            QVector3D axis;
            float angle;
            (previous.inverted() * m_rotation).getAxisAndAngle(&axis, &angle);
            if (angle > 180.0f) // shortest way round
                angle -= 360.0f;
            const QVector3D v = axis * float(angle / dt);
            m_spinVelocity    = 0.5f * m_spinVelocity + 0.5f * v;
        }
    }
    m_dragClock.restart();
}

bool
OpenGLWidget::globeAnimates() const
{
    return m_animateGlobe && m_projection == Projection::ORTHOGRAPHIC;
}

void
OpenGLWidget::setAnimateGlobe(bool enabled)
{
    m_animateGlobe = enabled;
    if (!enabled)
        stopGlobeAnimation();
}

void
OpenGLWidget::stopGlobeAnimation()
{
    m_globeTimer.stop();
    m_globeAnim    = GlobeAnim::None;
    m_spinVelocity = QVector3D();
}

// Eases to `target`, or jumps there when animation is off
void
OpenGLWidget::rotateTo(const QQuaternion &target)
{
    stopGlobeAnimation();
    if (!globeAnimates())
    {
        setRotation(target);
        return;
    }
    m_glideFrom = m_rotation;
    m_glideTo   = target;
    m_globeAnim = GlobeAnim::Glide;
    m_globeClock.start();
    m_globeTimer.start();
}

void
OpenGLWidget::tickGlobeAnimation()
{
    if (m_globeAnim == GlobeAnim::Glide)
    {
        constexpr double kDurationMs = 450.0;
        const double t = std::min(1.0, m_globeClock.elapsed() / kDurationMs);
        const float e  = float(1.0 - std::pow(1.0 - t, 3.0)); // ease out
        setRotation(QQuaternion::slerp(m_glideFrom, m_glideTo, e).normalized());
        if (t >= 1.0)
            stopGlobeAnimation();
        return;
    }

    if (m_globeAnim == GlobeAnim::Spin)
    {
        const double dt = std::min(0.05, m_globeClock.nsecsElapsed() * 1e-9);
        m_globeClock.restart();

        constexpr float kFriction = 3.0f; // 1/s: the speed falls by e per 1/3 s
        m_spinVelocity *= float(std::exp(-kFriction * dt));
        const float speed = m_spinVelocity.length(); // deg/s
        if (speed < 4.0f)
        {
            stopGlobeAnimation();
            return;
        }
        QQuaternion next = (m_rotation
                            * QQuaternion::fromAxisAndAngle(
                                m_spinVelocity / speed, float(speed * dt)))
                               .normalized();
        setRotation(orientationLocked() ? withoutRoll(next) : next);
    }
}

void
OpenGLWidget::setRotation(const QQuaternion &rotation)
{
    m_rotation = rotation;
    emit rotationChanged();
    emitCursorData();
    update();
}

void
OpenGLWidget::resetRotation()
{
    rotateTo(QQuaternion());
}

void
OpenGLWidget::setRotationAngles(double lonDeg, double latDeg, double rollDeg)
{
    stopGlobeAnimation();
    const QQuaternion rz
        = QQuaternion::fromAxisAndAngle(0, 0, 1, float(lonDeg));
    const QQuaternion ry
        = QQuaternion::fromAxisAndAngle(0, 1, 0, float(-latDeg));
    const QQuaternion rx
        = QQuaternion::fromAxisAndAngle(1, 0, 0, float(rollDeg));
    setRotation((rz * ry * rx).normalized());
}

void
OpenGLWidget::rotationAngles(double &lonDeg, double &latDeg,
                             double &rollDeg) const
{
    // The view center is where the x axis ends up
    const QVector3D c = m_rotation.rotatedVector(QVector3D(1, 0, 0));
    const double lon  = std::atan2(double(c.y()), double(c.x()));
    const double lat  = std::asin(std::clamp(double(c.z()), -1.0, 1.0));

    // Undo lon and lat; what is left is the roll about x
    const QQuaternion rz = QQuaternion::fromAxisAndAngle(
        0, 0, 1, float(-qRadiansToDegrees(lon)));
    const QQuaternion ry
        = QQuaternion::fromAxisAndAngle(0, 1, 0, float(qRadiansToDegrees(lat)));
    const QVector3D u
        = (ry * rz * m_rotation).rotatedVector(QVector3D(0, 1, 0));

    lonDeg  = qRadiansToDegrees(lon);
    latDeg  = qRadiansToDegrees(lat);
    rollDeg = qRadiansToDegrees(std::atan2(double(u.z()), double(u.y())));
}

void
OpenGLWidget::mousePressEvent(QMouseEvent *e)
{
    // Take focus from toolbar combos/spinboxes so shortcuts work again
    setFocus(Qt::MouseFocusReason);
    stopGlobeAnimation();

    // Region Selection: Shift + Left drag, or the next drag after
    // View > Zoom > Zoom To Region
    if (e->button() == Qt::LeftButton
        && (e->modifiers() == Qt::ShiftModifier || m_zoomRegionArmed))
    {
        m_isRegionSelecting = true;
        m_regionStart       = e->pos();
        m_regionEnd         = e->pos();
        e->accept();
        return;
    }

    if (e->button() == Qt::LeftButton && minimapVisible()
        && minimapRect().contains(e->pos()))
    {
        m_draggingMinimap = true;
        panToMinimapPoint(e->position());
        return;
    }

    // Rotate with the middle button or Ctrl + left drag; the globe-like
    // projections rotate with a plain left drag (panning them is pointless)
    const bool disk = m_projection == Projection::ORTHOGRAPHIC
                      || m_projection == Projection::AZIMUTHAL_EQUIDISTANT
                      || m_projection == Projection::GNOMONIC;
    const bool wantsRotate
        = e->button() == Qt::MiddleButton
          || (e->button() == Qt::LeftButton
              && ((e->modifiers() & Qt::ControlModifier) || disk));
    if (wantsRotate)
    {
        m_lastMousePos = e->pos();
        startRotation(e->position()); // ignored when grabbing outside the shape
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
OpenGLWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_isRotating)
    {
        m_cursorPos = e->position();
        updateRotation(e->position());
        return;
    }

    if (m_isRegionSelecting)
    {
        m_regionEnd = e->pos();
        update();
        return;
    }

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

    const QVector2D s = viewScale();
    m_camera->panBy(QVector2D(2.0f * delta.x() / float(width()) * s.x(),
                              -2.0f * delta.y() / float(height()) * s.y()));
    emitCursorData();
    update();
}

void
OpenGLWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_isRegionSelecting && e->button() == Qt::LeftButton)
    {
        m_isRegionSelecting = false;
        m_zoomRegionArmed   = false;
        setCursor(Qt::CrossCursor);
        // unsetCursor();
        processSelectionRegion();
        update();
        e->accept();
        return;
    }

    if (m_isRotating
        && (e->button() == Qt::LeftButton || e->button() == Qt::MiddleButton))
    {
        m_isRotating = false;
        setCursor(Qt::CrossCursor);

        // Throw: keep spinning when released while still moving
        const bool moving = m_dragClock.isValid() && m_dragClock.elapsed() < 60
                            && m_spinVelocity.length() > 15.0f;
        if (globeAnimates() && moving)
        {
            m_globeAnim = GlobeAnim::Spin;
            m_globeClock.start();
            m_globeTimer.start();
        }
        else
            m_spinVelocity = QVector3D();
        return;
    }

    if (e->button() == Qt::LeftButton)
    {
        m_draggingMinimap = false;
        m_isPanning       = false;
        setCursor(Qt::CrossCursor);
        // unsetCursor();
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
        rotateTo(QQuaternion());
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
    rotateTo(QQuaternion());
    emit zoomChanged(m_camera->zoom);
    emitCursorData();
    update();
}

void
OpenGLWidget::resetZoom()
{
    m_camera->resetZoom();
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

void
OpenGLWidget::processSelectionRegion()
{
    const QRectF r = QRectF(m_regionStart, m_regionEnd).normalized();
    if (r.width() < 4 || r.height() < 4) // a click, not a drag
        return;

    m_camera->zoomToRect(toNdc(r.topLeft()), toNdc(r.bottomRight()),
                         viewScale());
    emit zoomChanged(m_camera->zoom);
    emitCursorData();
}

void
OpenGLWidget::beginZoomToRegion()
{
    m_zoomRegionArmed = true;
    setCursor(Qt::CrossCursor);
}

void
OpenGLWidget::setGraticuleEnabled(bool enabled)
{
    m_graticuleEnabled = enabled;
    update();
}

void
OpenGLWidget::setViewFromOutside(bool outside)
{
    if (m_viewOutside == outside)
        return;
    m_viewOutside = outside;
    emitCursorData();
    update();
}

void
OpenGLWidget::setMapFrame(SkyFrame frame)
{
    if (m_mapFrame == frame)
        return;
    m_mapFrame = frame;
    emitCursorData();
    update();
}

void
OpenGLWidget::setOrientation(Orientation orientation)
{
    if (m_orientation == orientation)
        return;
    m_orientation = orientation;
    // The old rotation means something else in the new frame
    stopGlobeAnimation();
    setRotation(QQuaternion());
    emitCursorData();
    update();
}

// Removes the roll about the view axis, so the frame's north pole stays up
QQuaternion
OpenGLWidget::withoutRoll(const QQuaternion &q) const
{
    const QVector3D c = q.rotatedVector(QVector3D(1, 0, 0));
    const double lon  = std::atan2(double(c.y()), double(c.x()));
    const double lat  = std::asin(std::clamp(double(c.z()), -1.0, 1.0));
    const QQuaternion rz
        = QQuaternion::fromAxisAndAngle(0, 0, 1, float(qRadiansToDegrees(lon)));
    const QQuaternion ry = QQuaternion::fromAxisAndAngle(
        0, 1, 0, float(-qRadiansToDegrees(lat)));
    return (rz * ry).normalized();
}

void
OpenGLWidget::setGlobeShadow(bool enabled)
{
    m_globeShadow = enabled;
    update();
}

void
OpenGLWidget::setLimbDarkening(bool enabled)
{
    m_limbDarkening = enabled;
    update();
}

void
OpenGLWidget::setGraticuleStep(double degrees)
{
    m_graticuleStepDeg = degrees;
    update();
}

// (lat, lon) in radians -> window pixels (logical, y down) in the current
// projection. False when the point isn't shown (far side of the globe).
bool
OpenGLWidget::projectToWindow(double lat, double lon, QPointF &out) const
{
    // Sky -> view frame (inverse of the shader's rotation)
    double colat, phi;
    anglesFromDirection(m_rotation.inverted().rotatedVector(
                            directionFromAngles(M_PI / 2.0 - lat, lon)),
                        colat, phi);

    QVector2D plane;
    if (!projectionmath::forward(m_projection, M_PI / 2.0 - colat, phi, plane,
                                 m_viewOutside))
        return false;

    // Inverse of the camera and of toNdc()
    const QVector2D ndc = (plane - m_camera->panOffset) * m_camera->zoom;
    const QVector2D s   = viewScale();
    out                 = QPointF((ndc.x() / s.x() + 1.0f) * 0.5f * width(),
                                  (1.0f - ndc.y() / s.y()) * 0.5f * height());
    return true;
}

void
OpenGLWidget::paintGraticuleLabels(QPainter &p)
{
    const double step = qDegreesToRadians(m_graticuleStepDeg);
    const QRectF bounds(6, 6, width() - 12, height() - 12);
    const QFontMetrics fm = p.fontMetrics();

    auto draw = [&](const QPointF &at, const QString &text, bool alignRight)
    {
        const QPointF origin(
            at.x() - (alignRight ? fm.horizontalAdvance(text) : 0),
            at.y() + fm.ascent() / 2.0);
        const QRectF box(origin.x(), origin.y() - fm.ascent(),
                         fm.horizontalAdvance(text), fm.height());
        if (!bounds.contains(box))
            return;
        // shadow keeps it readable on any map
        drawTextPath(p, origin + QPointF(1, 1), text, QColor(0, 0, 0, 200));
        drawTextPath(p, origin, text, Qt::white);
    };

    QPointF at;

    // Longitudes along the equator (0 at the center, increasing to the left)
    for (double lon = -M_PI + step; lon <= M_PI + 1e-9; lon += step)
    {
        const double deg = std::round(qRadiansToDegrees(lon) * 100.0) / 100.0;
        if (projectToWindow(0.0, lon, at))
            draw(at + QPointF(0, fm.height() * 0.8), QString("%1°").arg(deg),
                 false);
    }
    // Latitudes along a visible meridian near the left side of the picture
    const double meridian
        = projectionmath::labelMeridian(m_projection, m_viewOutside);
    for (double lat = -M_PI / 2 + step; lat < M_PI / 2 - 1e-9; lat += step)
    {
        const double deg = std::round(qRadiansToDegrees(lat) * 100.0) / 100.0;
        if (projectToWindow(lat, meridian, at))
            draw(at - QPointF(4, 0), QString("%1°").arg(deg), true);
    }
}

void
OpenGLWidget::setGraticuleColor(const QColor &color)
{
    m_graticuleColor = color;
    update();
}

float
OpenGLWidget::projectionFade()
{
    if (!m_projectionFading)
        return 1.0f;
    constexpr double kDurationMs = 300.0;
    const double t = m_projectionClock.elapsed() / kDurationMs;
    if (t >= 1.0)
    {
        m_projectionFading = false;
        return 1.0f;
    }
    return float(t * t * (3.0 - 2.0 * t)); // smoothstep
}

void
OpenGLWidget::setProjection(Projection projection)
{
    // Fade from the old picture to the new one, if there is one to show. The
    // rest of the widget (labels, cursor, minimap) switches at once.
    if (m_animateProjection && projection != m_projection && m_nside > 0
        && isVisible())
    {
        m_prevProjection   = m_projectionFading ? m_prevProjection
                                                : m_projection;
        m_projectionFading = true;
        m_projectionClock.start();
    }
    m_projection = projection;
    if (!globeAnimates())
        stopGlobeAnimation();
    update();
}
