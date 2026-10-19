#pragma once

#include "colormap.hpp"
#include "exportoptions.hpp"
#include "mapcamera.hpp"
#include "masksource.hpp"
#include "pixelordering.hpp"
#include <climits>
#include <QKeySequence>
#include "projection.hpp"
#include "skyframe.hpp"
#include "tilecache.hpp"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QImage>
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
    // Range between two percentiles of the valid, unmasked pixels (e.g. 1 and
    // 99 clip the outliers); a flat map keeps the current range
    void setColormapPercentiles(double lowPercent, double highPercent);
    void setColormapMin(double min);
    void setColormapMax(double max);
    void setColormapScale(ColormapScale scale);
    void setAsinhSoftening(double softening); // (0, 1]; smaller = more log-like

    inline double asinhSoftening() const
    {
        return m_asinhSoftening;
    }

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
    // Zoom about the window centre by `factor`, or pan by a fraction of the
    // window (right and up are positive; only moves a zoomed-in view)
    void zoomStep(float factor);
    void panStep(float right, float up);
    // Keys that close (or cancel) a region being drawn
    void setRegionKeys(const QKeySequence &finish, const QKeySequence &cancel);

    // The shapes a statistics region can have. Rectangle, ellipse and polygon
    // are drawn on the projection plane; a line is the great-circle arc
    // between two points of the sky (the statistics are of the pixels on it).
    enum class RegionShape
    {
        Rectangle,
        Ellipse,
        Polygon,
        Line
    };

    // Statistics of a part of the sky. The region is a rectangle drawn in the
    // current projection (Ctrl + Shift + drag, or armed with
    // beginStatsRegion()); it stays fixed on the sky when the view moves.
    struct RegionStats
    {
        long pixels      = 0; // pixels inside the region
        long valid       = 0; // ... that are valid and not masked
        double areaDeg2  = 0.0;
        double lengthDeg = 0.0; // of a line region, else 0
        double skyFrac   = 0.0; // of the whole sky
        double min       = 0.0;
        double max       = 0.0;
        double average   = 0.0;
        double rms       = 0.0;
        double std       = 0.0;
    };
    // Arms the selection: rectangle and ellipse by dragging, line by dragging
    // between its ends, polygon by clicking the corners (double click or
    // Enter closes it, Escape cancels, right click removes the last corner)
    void beginStatsRegion(RegionShape shape = RegionShape::Rectangle);
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

    // The same as a weight between 0 (masked) and 1 (kept) for every pixel:
    // the product of the enabled masks, without the 0.5 threshold. Used to
    // correct power spectra for the mask. Empty when no mask is enabled.
    // With `only` >= 0 it is the weight of that one mask alone (whether or not
    // it is enabled; an inverted mask gives 1 - w)
    std::vector<float> maskWeights(int only = -1) const;

    // Painting a mask by hand. In paint mode a left drag masks (or unmasks)
    // the pixels under the brush and a right drag does the opposite; the
    // strokes go to a mask called "Painted mask" in the mask list.
    enum class PaintTool
    {
        Mask,
        Unmask
    };
    void setPaintMode(bool on);
    inline bool paintMode() const
    {
        return m_paintMode;
    }
    void setPaintTool(PaintTool tool);
    // Brush radius in degrees on the sky; hardness 0..1 (1 = hard edge, lower
    // fades the brush towards its rim)
    void setPaintBrush(double radiusDeg, double hardness);
    void paintFill(uint8_t value); // 0 masks everything, 255 keeps everything
    void paintInvert();
    bool paintUndo();
    bool paintRedo();
    bool canPaintUndo() const
    {
        return !m_paintUndo.empty();
    }
    bool canPaintRedo() const
    {
        return !m_paintRedo.empty();
    }
    bool hasPaintLayer() const;
    // Sum of the painted weights / Npix (1 before anything is painted)
    double paintSkyFraction() const;
    // The painted layer as a map (null when there is none)
    std::shared_ptr<Healpix_Map<float>> paintLayerMap() const;

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
    // Boundaries of the HEALPix pixels of `nside` (a power of two) as an
    // overlay
    void setCellGridEnabled(bool enabled);
    void setCellGridNside(int nside);
    void setCellGridColor(const QColor &color);
    // Number each pixel of the grid (in the map's pixel ordering)
    void setCellLabelsEnabled(bool enabled);
    bool cellLabelsEnabled() const { return m_cellLabelsEnabled; }
    bool cellGridEnabled() const { return m_cellGridEnabled; }
    int cellGridNside() const { return m_cellGridNside; }
    QColor cellGridColor() const { return m_cellGridColor; }
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

    // Glides to center the view on a point (degrees, in the frame shown),
    // with no roll, or the current roll when keepRoll is set
    void centerOn(double lonDeg, double latDeg, bool keepRoll = false);

    // A right click glides the clicked point to the center along the great
    // circle. In a frame the north pole stays up; in the free orientation the
    // roll is kept when lockRoll is set.
    void setRecenterOnRightClick(bool enabled)
    {
        m_recenterOnRightClick = enabled;
    }
    void setLockRollOnRecenter(bool enabled)
    {
        m_lockRollOnRecenter = enabled;
    }

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
    void keyPressEvent(QKeyEvent *e) override;

signals:
    void mapLoaded(long nside, long npix, double avg, double rms, double std,
                   float min, float max);
    void zoomChanged(float zoom);
    void rotationChanged(); // drag, reset or setRotationAngles
    void masksChanged();    // list or enabled flags changed
    void paintChanged();    // paint mode, undo state or the painted layer
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
    void paintCellLabels(QPainter &p);
    static constexpr int kMapPadding = 24; // logical px around the map
    void emitCursorData();
    void uploadMapToGPU(const Healpix_Map<float> &map);
    void uploadMaskToGPU();
    // Re-uploads only the texel rectangles the paint brush touched
    void uploadMaskRegions();
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
    // Eases to `target` (or jumps): only the orthographic globe eases, unless
    // `anyProjection` is set
    void rotateTo(const QQuaternion &target, bool anyProjection = false);
    void recenterAt(const QPointF &pos);
    static QQuaternion rotationFromAngles(double lonDeg, double latDeg,
                                          double rollDeg);
    void tickGlobeAnimation();
    void uploadColormapLut();
    // Colorbar: the colormap is baked on the GPU into a 256x1 strip, drawn
    // with the ticks and labels into m_colorbarImage, uploaded as a texture
    // and composited by m_colorbarProgram
    QImage bakeColormapStrip();
    QImage renderColorbarImage(const QImage &strip) const;
    void updateColorbarTexture();
    QRectF colorbarOverlayRect() const; // device pixels, y up
    void recomputeDataRange();
    void recomputeRegionStats();
    void applyPixelOrdering(PixelOrdering ordering,
                            std::vector<std::vector<uint8_t>> *newMasks);
    void startRegionJob();
    // Stops the running computation; with wait, returns only once the worker
    // no longer touches the map
    void cancelRegionJob(bool wait);
    // Fixes a region on the sky from points in widget coordinates: two
    // corners (rectangle, ellipse), the two ends (line) or the corners
    void setStatsRegion(RegionShape shape, const QVector<QPointF> &points);
    void finishPolygon();
    QKeySequence m_regionFinishKey = QKeySequence(Qt::Key_Return);
    QKeySequence m_regionCancelKey = QKeySequence(Qt::Key_Escape);
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
    int m_lutVersion              = 0;
    unsigned int m_colorbarTexture = 0;
    QVector<double> m_colorbarKey; // what the texture was last built from
    ColormapScale m_colormapScale = ColormapScale::LINEAR;
    float m_asinhSoftening        = 0.1f;
    long m_nside                  = 0;
    long m_npix                   = 0;
    // The 12 face layers are split over up to kMaxSegments textures, each
    // holding m_segLayers faces (a single texture is capped at about 1 GB by
    // some drivers). Masks use the same split, 4 masks per set.
    // Streaming (nside above m_streamNside): the textures hold a coarse map of
    // m_texNside = nside >> m_levelShift and the tile cache the visible part
    // of the full-resolution map. Without it m_texNside == nside.
    int m_streamNside   = 4096;
    // Texel type of the map textures, and the value range the normalized
    // formats span (set when the map is uploaded)
    MapTexFormat m_mapTexFormat = MapTexFormat::Float32;
    float m_texLo               = 0.0f;
    float m_texHi               = 0.0f;
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
    QOpenGLShaderProgram m_stripProgram;    // colormap -> 256x1 strip
    QOpenGLShaderProgram m_colorbarProgram; // draws the colorbar texture
    MapCamera *m_camera = nullptr;
    bool m_isPanning    = false;
    QPoint m_lastMousePos;
    bool m_draggingMinimap = false;
    QPointF m_cursorPos; // last pointer position in widget coords

    // Painting
    struct PaintUndo
    {
        std::vector<std::pair<int, uint8_t>> px; // pixel, value before
        std::vector<uint8_t> full;               // the whole layer before
        bool isFull = false;
        size_t bytes() const
        {
            return px.size() * sizeof(std::pair<int, uint8_t>) + full.size();
        }
    };
    int paintLayerIndex() const;
    int ensurePaintLayer();
    void paintBegin(const QPointF &pos, bool unmask);
    void paintTo(const QPointF &pos);
    void paintDab(const QPointF &pos);
    void paintEnd();
    void pushPaintUndo(PaintUndo &&u);
    PaintUndo applyPaintUndo(const PaintUndo &u);
    void paintBrushOverlay(QPainter &p);
    void clearPaintHistory();
    bool m_paintMode   = false;
    bool m_painting    = false; // a stroke is in progress
    bool m_paintUnmask = false; // the stroke unmasks
    PaintTool m_paintTool = PaintTool::Mask;
    double m_paintRadius   = 3.0; // degrees
    double m_paintHardness = 0.5;
    QPointF m_paintLast;
    std::unordered_map<int, uint8_t> m_strokeOld; // pixel -> value before
    std::vector<PaintUndo> m_paintUndo, m_paintRedo;

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
    RegionShape m_statsShape = RegionShape::Rectangle; // the armed shape
    RegionShape m_dragShape  = RegionShape::Rectangle; // of the current drag
    QVector<QPointF> m_polyPoints; // corners placed so far

    struct StatsRegion
    {
        bool active = false;
        Projection projection = Projection::MOLLWEIDE;
        bool outside          = false;
        // Map-frame direction -> the view frame it was drawn in (row-major)
        double toView[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        QVector2D lo, hi; // bounds on the projection plane
        RegionShape shape = RegionShape::Rectangle;
        QVector<QVector2D> poly; // polygon corners on the plane
        double lineA[3] = {1, 0, 0}, lineB[3] = {1, 0, 0}; // line ends, map frame
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
    // Pixel ordering changes: the masks are re-indexed on a worker thread; a
    // newer request makes the older result obsolete
    int m_orderingJob                 = 0;
    PixelOrdering m_orderingRequested = PixelOrdering::RING;
    int m_mapRevision                 = 0; // changes with every new map
    // Old maps the region worker may still be reading after setMap() swapped
    // them out; freed when the worker is done
    std::vector<std::shared_ptr<Healpix_Map<float>>> m_retiredMaps;
    QPoint m_regionStart;
    QPoint m_regionEnd;

    std::vector<MaskEntry> m_masks;
    static constexpr int kMaxMasks = 16; // 4 textures x 4 channels
    std::array<GLuint, 4 * kMaxSegments> m_maskTextures{}; // [set * 3 + seg]
    bool m_maskDirty               = false; // mask textures need re-uploading
    // Texel rectangles (per face, inclusive) the brush changed since the last
    // upload; cheaper than re-uploading every mask while a stroke is going
    struct DirtyRect
    {
        int x0 = INT_MAX, y0 = INT_MAX, x1 = -1, y1 = -1;
        bool empty() const { return x1 < 0; }
    };
    std::array<DirtyRect, 12> m_paintDirty{};
    bool m_paintRegionDirty = false;
    bool m_colorBarVisible         = true;
    int m_maskMode                 = 0;
    float m_maskOpacity            = 0.8f;
    float m_fsky                   = 1.0f; // fraction of sky that is visible
    static constexpr int kMinimapH = 100;
    static constexpr int kMinimapMargin = 12;
    bool m_minimapEnabled               = false;
    bool m_cellGridEnabled              = false;
    bool m_cellLabelsEnabled            = false;
    int m_cellGridNside                 = 4;
    QColor m_cellGridColor              = QColor(255, 220, 60, 200);
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
    // Dragging outside the globe's disc twists it about the view axis
    bool m_dragOutside = false;
    QQuaternion m_outsideBase; // rotation when the cursor left the disc
    float m_outsideLast  = 0.0f; // cursor angle at the last move
    float m_outsideTotal = 0.0f; // swept since leaving (rad, unwrapped)

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
    bool m_recenterOnRightClick = true;
    bool m_lockRollOnRecenter   = false;
    bool m_animateProjection = true;
    GlobeAnim m_globeAnim = GlobeAnim::None;
    QTimer m_globeTimer;
    QElapsedTimer m_globeClock; // frame time while animating
    QElapsedTimer m_dragClock;  // time between drag moves
    QVector3D m_spinVelocity;   // axis * deg/s
    QQuaternion m_glideFrom, m_glideTo;
};
