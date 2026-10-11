#pragma once

#include "colormap.hpp"
#include "exportoptions.hpp"
#include "mapcamera.hpp"
#include "pixelordering.hpp"
#include "projection.hpp"

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QQuaternion>
#include <array>
#include <cstdint>
#include <healpix_map.h>
#include <qobject.h>
#include <vector>

// Largest nside the viewer can show: the map is packed into one 2D texture
// (4096 wide), whose height is limited by the GPU (nside 2048 needs 12288 rows)
constexpr int kMaxSupportedNside = 2048;

class OpenGLWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit OpenGLWidget(QWidget *parent = nullptr);
    ~OpenGLWidget();

    inline PixelOrdering pixelOrdering() const
    {
        return m_pixelOrdering;
    }

    void setPixelOrdering(PixelOrdering ordering);

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
    bool graticuleEnabled() const
    {
        return m_graticuleEnabled;
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
    void masksChanged(); // list or enabled flags changed
    // Emitted when the implicit colormap range changes (e.g. mask applied)
    void colormapRangeChanged(float min, float max);
    // lonLat in degrees; value is NaN and index -1 when off the map
    void cursorPosChanged(QPointF lonLat, QPointF raDec, double value,
                          qlonglong index);

private:
    QVector2D toNdc(const QPointF &pos) const;
    QVector2D viewScale() const;
    bool projectToWindow(double latRad, double lonRad, QPointF &out) const;
    bool viewDirectionAt(const QPointF &pos, QVector3D &dir) const;
    bool startRotation(const QPointF &pos);
    void updateRotation(const QPointF &pos);
    int minimapWidth() const;
    void paintGraticuleLabels(QPainter &p);
    static constexpr int kMapPadding = 24; // logical px around the map
    void emitCursorData();
    void uploadMapToGPU(const Healpix_Map<float> &map);
    void uploadMaskToGPU();
    void recomputeDataRange();
    void applyMaskChange(bool texturesChanged);
    int maskBits(bool inverted = false) const;
    void setupGeometry();
    double scaleValue(double v) const;
    void processSelectionRegion();

private:
    PixelOrdering m_pixelOrdering = PixelOrdering::RING;
    Colormap m_colormap           = Colormap::VIRIDIS;
    ColormapScale m_colormapScale = ColormapScale::LINEAR;
    long m_nside                  = 0;
    long m_npix                   = 0;
    GLuint m_mapTexture           = 0;
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
    QPoint m_regionStart;
    QPoint m_regionEnd;

    std::vector<MaskEntry> m_masks;
    static constexpr int kMaxMasks = 16; // 4 textures x 4 channels
    std::array<GLuint, 4> m_maskTextures{};
    bool m_maskDirty               = false; // mask textures need re-uploading
    bool m_colorBarVisible         = true;
    int m_maskMode                 = 0;
    float m_maskOpacity            = 0.8f;
    float m_fsky                   = 1.0f; // fraction of sky that is visible
    static constexpr int kMinimapH = 100;
    static constexpr int kMinimapMargin = 12;
    bool m_minimapEnabled               = false;
    bool m_graticuleEnabled             = false;
    double m_graticuleStepDeg           = 30.0;
    QColor m_graticuleColor             = QColor(255, 255, 255, 128);
    Projection m_projection             = Projection::MOLLWEIDE;
    bool m_isRotating                   = false;
    QVector3D m_rotateGrab; // view-frame direction grabbed at drag start
    QQuaternion m_rotation;      // current rotation quaternion for 3D view
    QQuaternion m_rotationStart; // rotation at the start of a drag
};
