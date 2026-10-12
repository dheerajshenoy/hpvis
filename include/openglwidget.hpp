#pragma once

#include "colormap.hpp"
#include "exportoptions.hpp"
#include "mapcamera.hpp"
#include "masksource.hpp"
#include "pixelordering.hpp"
#include "projection.hpp"
#include "skyframe.hpp"
#include "tilecache.hpp"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QQuaternion>
#include <QTimer>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <healpix_map.h>
#include <memory>
#include <qobject.h>
#include <vector>

// Largest nside the viewer can show. The map is stored as a 12-layer 2D array
// texture (one nside x nside layer per base face), so the GPU's maximum texture
// size (usually 16384) is the real limit. nside 8192 needs 3.2 GB of VRAM.
constexpr int kMaxSupportedNside = 8192;

class OpenGLWidget : public QOpenGLWidget, protected QOpenGLExtraFunctions
{
    Q_OBJECT

public:
    explicit OpenGLWidget(QWidget *parent = nullptr);
    ~OpenGLWidget();

    inline PixelOrdering pixelOrdering() const
    {
        return m_pixelOrdering;
    }

    // True when the pixel ordering was changed from the one the map was
    // loaded with (then the file no longer describes what is shown)
    bool orderingOverridden() const
    {
        return m_pixelOrdering != m_loadedOrdering;
    }
    void setPixelOrdering(PixelOrdering ordering);
    // Maps with a larger nside are streamed: a coarse copy stays on the GPU
    // and only the full-resolution tiles on screen are loaded. Applies to the
    // open map too.
    void setStreamingNside(int nside);

    void setProjection(Projection projection);

    inline Projection projection() const
    {
        return m_projection;
    }

    inline Colormap colormap() const
    {
        return m_colormap;
    }

    void setColormap(Colormap colormap);

    inline float colormapMin() const
    {
        return m_vmin;
    }

    inline bool colorBarVisible() const
    {
        return m_colorBarVisible;
    }

    void setColorBarVisible(bool visible);

    inline float colormapMax() const
    {
        return m_vmax;
    }

    QRect minimapRect() const;

    bool minimapVisible() const
    {
        return m_minimapEnabled && m_camera->zoom > 1.0f;
    }

    void panToMinimapPoint(const QPointF &pos);

    inline bool minimapEnabled() const
    {
        return m_minimapEnabled;
    }

    inline void setMinimapEnabled(bool enabled)
    {
        m_minimapEnabled = enabled;
        update();
    }

    // Restore the colormap range to the min/max of the loaded map
    void resetColormapRange();
    void setColormapMin(double min);
    void setColormapMax(double max);
    void setColormapScale(ColormapScale scale);

    inline ColormapScale colormapScale() const
    {
        return m_colormapScale;
    }

    inline bool colormapInvert() const
    {
        return m_invertColormap;
    }

    void setColormapInvert(bool invert);

    inline QColor backgroundColor() const
    {
        return m_bgColor;
    }

    void setBackgroundColor(const QColor &color);
    void setUnseenColor(const QColor &color);

    inline QColor unseenColor() const
    {
        return m_unseenColor;
    }

    inline float zoom() const
    {
        return m_camera->zoom;
    }

    void setZoom(float zoom);
    void resetZoom();
    // Arms a rubber-band: the next left-drag zooms to the dragged rectangle
    void beginZoomToRegion();

    // Statistics of a part of the sky. The region is a rectangle drawn in the
    // current projection (Ctrl + Shift + drag, or armed with
    // beginStatsRegion()); it stays fixed on the sky when the view moves.
    struct RegionStats
    {
        long pixels      = 0; // pixels inside the region
        long valid       = 0; // ... that are valid and not masked
        double areaDeg2  = 0.0;
        double skyFrac   = 0.0; // of the whole sky
        double min       = 0.0;
        double max       = 0.0;
        double average   = 0.0;
        double rms       = 0.0;
        double std       = 0.0;
    };
    void beginStatsRegion();
    void clearStatsRegion();
    bool hasStatsRegion() const
    {
        return m_statsRegion.active;
    }
    const RegionStats &regionStats() const
    {
        return m_regionStats;
    }
    // True while the statistics of the region are being computed (they are
    // computed on a worker thread; regionChanged() fires when they are done)
    bool regionBusy() const
    {
        return m_regionBusy;
    }
    // Changes whenever the pixels inside the region change (new or cleared
    // region, other pixel ordering); not when only the numbers are updated
    int regionRevision() const
    {
        return m_regionRevision;
    }
    // A function that builds the 0 / 1 mask of the region's pixels (1 =
    // inside, and kept by the enabled masks when withMasks). It owns copies
    // of what it needs, so it can run on any thread later. Empty when there
    // is no region.
    std::function<std::vector<uint8_t>()> regionKeepBuilder(bool withMasks) const;

    double mapAverage() const
    {
        return m_mapAverage;
    }
    double mapRms() const
    {
        return m_mapRms;
    }
    double mapStd() const
    {
        return m_mapStd;
    }
    float mapMin() const
    {
        return m_mapMin;
    }
    float mapMax() const
    {
        return m_mapMax;
    }

    inline float fsky() const
    {
        return m_fsky;
    }

    // Adds mask maps to the list (each must match the map's nside; files
    // already in the list are skipped). Returns the number added.
    // Asynchronous: masks appear (and masksChanged fires) once they are read
    void addMaskFiles(const QStringList &files);
    // The same for any column of a file, or a map in memory
    void addMasks(const QList<MaskSource> &sources);
    void removeMask(int index);
    void clearMasks();
    void setMaskEnabled(int index, bool enabled);
    void setMaskInverted(int index, bool inverted);

    inline int maskCount() const
    {
        return static_cast<int>(m_masks.size());
    }

    inline QString maskName(int index) const
    {
        return m_masks[index].name;
    }

    inline QString maskPath(int index) const
    {
        return m_masks[index].path;
    }

    inline bool maskEnabled(int index) const
    {
        return m_masks[index].enabled;
    }

    inline bool maskInverted(int index) const
    {
        return m_masks[index].inverted;
    }

    // 1 for the pixels the enabled masks keep, one entry per pixel; empty when
    // no mask is enabled
    std::vector<uint8_t> keepMask() const;

    inline int maskMode() const
    {
        return m_maskMode;
    }

    inline float maskOpacity() const
    {
        return m_maskOpacity;
    }

    inline const Healpix_Map<float> *healpixMap() const
    {
        return m_pendingMap.Npix() > 0 ? &m_pendingMap : nullptr;
    }

    void setGraticuleColor(const QColor &color);
    void setGraticuleEnabled(bool enabled);
    void setGraticuleStep(double degrees);
    void setGlobeShadow(bool enabled);

    // The frame the map is stored in (FITS COORDSYS)
    void setMapFrame(SkyFrame frame);
    SkyFrame mapFrame() const
    {
        return m_mapFrame;
    }
    // Frame shown (and, unless Free, kept north-up while dragging). Resets
    // the rotation.
    void setOrientation(Orientation orientation);
    Orientation orientation() const
    {
        return m_orientation;
    }
    bool orientationLocked() const
    {
        return m_orientation != Orientation::Free;
    }
    SkyFrame displayFrame() const
    {
        return skyframe::displayFrame(m_orientation, m_mapFrame);
    }
    // false: the sky seen from the inside (longitude increases to the left);
    // true: seen from the outside like a globe (the mirror image)
    void setViewFromOutside(bool outside);
    bool viewFromOutside() const
    {
        return m_viewOutside;
    }
    // Orthographic: inertia after a drag and a glide on reset
    void setAnimateGlobe(bool enabled);
    bool animateGlobe() const
    {
        return m_animateGlobe;
    }
    // Fade from the old picture to the new one when the projection changes
    void setAnimateProjection(bool enabled)
    {
        m_animateProjection = enabled;
    }
    void setLimbDarkening(bool enabled);
    bool graticuleEnabled() const
    {
        return m_graticuleEnabled;
    }
    QColor graticuleColor() const
    {
        return m_graticuleColor;
    }
    double graticuleStep() const
    {
        return m_graticuleStepDeg;
    }
    void setMaskMode(int mode);
    void setMaskOpacity(float opacity);
    // Takes ownership of `map` (its buffer is swapped in, not copied)
    void setMap(Healpix_Map<float> &&map);

    QRectF colorbarRect() const;

    void exportImage(const QString &filePath,
                     const ExportOptions &options = {});
    void resetView();
    void resetRotation();

    // Where the view is centered (degrees) and how it is rolled about the
    // line of sight. R = Rz(lon) * Ry(-lat) * Rx(roll)
    void setRotationAngles(double lonDeg, double latDeg, double rollDeg);
    void rotationAngles(double &lonDeg, double &latDeg, double &rollDeg) const;

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    bool event(QEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

signals:
    void mapLoaded(long nside, long npix, double avg, double rms, double std,
                   float min, float max);
    void zoomChanged(float zoom);
    void rotationChanged(); // drag, reset or setRotationAngles
    void masksChanged();    // list or enabled flags changed
    void regionChanged();   // statistics region set, cleared or recomputed
    // Emitted when the implicit colormap range changes (e.g. mask applied)
    void colormapRangeChanged(float min, float max);
    // lonLat in degrees; value is NaN and index -1 when off the map
    void cursorPosChanged(QPointF lonLat, QPointF raDec, double value,
                          qlonglong index);

private:
    QVector2D toNdc(const QPointF &pos) const;
    QVector2D viewScale() const { return viewScale(m_projection); }
    QVector2D viewScale(Projection projection) const;
    // 0..1 progress of the fade from the previous projection, or 1 when not
    // animating (also ends the animation once it is over)
    float projectionFade();
    bool projectToWindow(double latRad, double lonRad, QPointF &out) const;
    void setRotation(const QQuaternion &rotation);
    bool viewDirectionAt(const QPointF &pos, QVector3D &dir) const;
    bool startRotation(const QPointF &pos);
    void updateRotation(const QPointF &pos);
    int minimapWidth() const;
    void paintGraticuleLabels(QPainter &p);
    static constexpr int kMapPadding = 24; // logical px around the map
    void emitCursorData();
    void uploadMapToGPU(const Healpix_Map<float> &map);
    void uploadMaskToGPU();
    // Streaming: the tiles on screen that are finer than the coarse map, most
    // central first, and loading them
    std::vector<int> visibleTiles() const;
    void updateTiles();
    // Creates one segment texture (nside x nside x m_segLayers) with nearest
    // filtering; false (nothing left allocated) if the driver refuses
    bool allocSegment(GLuint &tex, GLenum internalFormat, GLenum format,
                      GLenum type);
    void deleteMapTextures();
    void deleteMaskTextures();
    // Binds map and mask segments: map on units 0..2, mask set g segment k on
    // 3 + 3 g + k, the colormap LUT on kLutUnit
    void bindDataTextures(bool setSamplers);
    QQuaternion withoutRoll(const QQuaternion &q) const;
    bool globeAnimates() const;
    void stopGlobeAnimation();
    void rotateTo(const QQuaternion &target);
    void tickGlobeAnimation();
    void uploadColormapLut();
    void recomputeDataRange();
    void recomputeRegionStats();
    void startRegionJob();
    // Stops the running computation; with wait, returns only once the worker
    // no longer touches the map
    void cancelRegionJob(bool wait);
    void setStatsRegionFromRect(const QRectF &widgetRect);
    void paintStatsRegion(QPainter &p);
    void applyMaskChange(bool texturesChanged);
    int maskBits(bool inverted = false) const;
    void setupGeometry();
    double scaleValue(double v) const;
    void processSelectionRegion();

private:
    PixelOrdering m_pixelOrdering = PixelOrdering::RING;
    PixelOrdering m_loadedOrdering = PixelOrdering::RING; // of the file
    Colormap m_colormap           = Colormap::VIRIDIS;
    unsigned int m_lutTexture     = 0; // custom colormaps, one row each
    int m_lutRows                 = 1;
    bool m_lutDirty               = true;
    ColormapScale m_colormapScale = ColormapScale::LINEAR;
    long m_nside                  = 0;
    long m_npix                   = 0;
    // The 12 face layers are split over up to kMaxSegments textures, each
    // holding m_segLayers faces (a single texture is capped at about 1 GB by
    // some drivers). Masks use the same split, 4 masks per set.
    // Streaming (nside above m_streamNside): the textures hold a coarse map of
    // m_texNside = nside >> m_levelShift and the tile cache the visible part
    // of the full-resolution map. Without it m_texNside == nside.
    int m_streamNside   = 4096;
    long m_texNside     = 0;
    int m_levelShift    = 0;
    bool m_streaming    = false;
    bool m_needsReupload = false; // m_streamNside changed
    std::unique_ptr<TileCache> m_tiles;
    static constexpr int kMaxSegments = 3;
    static constexpr int kLutUnit     = 3 + 4 * kMaxSegments; // 15
    std::array<GLuint, kMaxSegments> m_mapTextures{};
    int m_segLayers = 12;
    float m_vmin                  = 0.0f;
    float m_vmax                  = 1.0f;
    float m_dataMin               = 0.0f;
    float m_dataMax               = 1.0f;
    bool m_hasPendingMap          = false;
    double m_mapAverage           = 0.0;
    double m_mapRms               = 0.0;
    double m_mapStd               = 0.0;
    float m_mapMin                = 0.0f;
    float m_mapMax                = 1.0f;
    bool m_invertColormap         = false;
    QColor m_bgColor              = QColor(10, 10, 15);
    QColor m_unseenColor          = QColor(0, 0, 0);
    Healpix_Map<float> m_pendingMap;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer}; // VBO wrapper
    QOpenGLShaderProgram m_shaderProgram;
    MapCamera *m_camera = nullptr;
    bool m_isPanning    = false;
    QPoint m_lastMousePos;
    bool m_draggingMinimap = false;
    QPointF m_cursorPos; // last pointer position in widget coords

    struct MaskEntry
    {
        QString name;
        QString path;
        std::vector<uint8_t> data; // 0..255, 255 = keep, 0 = masked
        bool enabled  = true;
        bool inverted = false;
    };

    bool m_isRegionSelecting = false;
    bool m_zoomRegionArmed   = false;
    bool m_statsRegionArmed  = false;
    bool m_selectingStats    = false; // the rubber-band is for statistics

    struct StatsRegion
    {
        bool active = false;
        Projection projection = Projection::MOLLWEIDE;
        bool outside          = false;
        // Map-frame direction -> the view frame it was drawn in (row-major)
        double toView[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        QVector2D lo, hi; // the rectangle on the projection plane
    };
    StatsRegion m_statsRegion;
    RegionStats m_regionStats;

    // The worker thread of the region statistics: one job at a time, a newer
    // request cancels the running one and starts when it has stopped
    QFutureWatcher<RegionStats> *m_regionWatcher = nullptr;
    std::shared_ptr<std::atomic<bool>> m_regionCancel;
    bool m_regionRunning = false;
    bool m_regionPending = false;
    bool m_regionBusy    = false;
    int m_regionRevision = 0;
    // Old maps the region worker may still be reading after setMap() swapped
    // them out; freed when the worker is done
    std::vector<std::shared_ptr<Healpix_Map<float>>> m_retiredMaps;
    QPoint m_regionStart;
    QPoint m_regionEnd;

    std::vector<MaskEntry> m_masks;
    static constexpr int kMaxMasks = 16; // 4 textures x 4 channels
    std::array<GLuint, 4 * kMaxSegments> m_maskTextures{}; // [set * 3 + seg]
    bool m_maskDirty               = false; // mask textures need re-uploading
    bool m_colorBarVisible         = true;
    int m_maskMode                 = 0;
    float m_maskOpacity            = 0.8f;
    float m_fsky                   = 1.0f; // fraction of sky that is visible
    static constexpr int kMinimapH = 100;
    static constexpr int kMinimapMargin = 12;
    bool m_minimapEnabled               = false;
    bool m_graticuleEnabled             = false;
    bool m_globeShadow                  = true;
    bool m_viewOutside                  = false;
    SkyFrame m_mapFrame                 = SkyFrame::Galactic;
    Orientation m_orientation           = Orientation::Free;
    bool m_limbDarkening                = true;
    double m_graticuleStepDeg           = 30.0;
    QColor m_graticuleColor             = QColor(255, 255, 255, 128);
    Projection m_projection             = Projection::MOLLWEIDE;
    Projection m_prevProjection         = Projection::MOLLWEIDE;
    bool m_projectionFading             = false;
    QElapsedTimer m_projectionClock;
    bool m_isRotating                   = false;
    QVector3D m_rotateGrab;      // view-frame direction grabbed at drag start
    QQuaternion m_rotation;      // current rotation quaternion for 3D view
    QQuaternion m_rotationStart; // rotation at the start of a drag

    // Globe animation. Spin: the drag's angular velocity (deg/s, around an
    // axis in the globe's own frame) decays with friction after release.
    // Glide: eases from m_glideFrom to m_glideTo.
    enum class GlobeAnim
    {
        None,
        Spin,
        Glide
    };
    bool m_animateGlobe   = true;
    bool m_animateProjection = true;
    GlobeAnim m_globeAnim = GlobeAnim::None;
    QTimer m_globeTimer;
    QElapsedTimer m_globeClock; // frame time while animating
    QElapsedTimer m_dragClock;  // time between drag moves
    QVector3D m_spinVelocity;   // axis * deg/s
    QQuaternion m_glideFrom, m_glideTo;
};
