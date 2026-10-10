#pragma once

#include "colormap.hpp"
#include "mapcamera.hpp"
#include "pixelordering.hpp"

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <healpix_map.h>
#include <array>
#include <cstdint>
#include <vector>

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

    inline void setPixelOrdering(PixelOrdering ordering)
    {
        m_pixelOrdering = ordering;
        emitCursorData();
        update();
    }

    inline Colormap colormap() const
    {
        return m_colormap;
    }

    inline void setColormap(Colormap colormap)
    {
        m_colormap = colormap;
        update();
    }

    inline float colormapMin() const
    {
        return m_vmin;
    }

    inline float colormapMax() const
    {
        return m_vmax;
    }

    // Restore the colormap range to the min/max of the loaded map
    inline void resetColormapRange()
    {
        m_vmin = m_dataMin;
        m_vmax = m_dataMax;
        update();
    }

    inline void setColormapMin(double min)
    {
        m_vmin = static_cast<float>(min);
        update();
    }

    inline void setColormapMax(double max)
    {
        m_vmax = static_cast<float>(max);
        update();
    }

    inline void setColormapScale(ColormapScale scale)
    {
        m_colormapScale = scale;
        update();
    }

    inline ColormapScale colormapScale() const
    {
        return m_colormapScale;
    }

    bool colormapInvert() const
    {
        return m_invertColormap;
    }

    inline void setColormapInvert(bool invert)
    {
        m_invertColormap = invert;
        update();
    }

    inline QColor backgroundColor() const
    {
        return m_bgColor;
    }

    inline void setBackgroundColor(const QColor &color)
    {
        m_bgColor = color;
        update();
    }

    inline void setUnseenColor(const QColor &color)
    {
        m_unseenColor = color;
        update();
    }

    inline QColor unseenColor() const
    {
        return m_unseenColor;
    }

    float zoom() const
    {
        return m_camera->zoom;
    }

    // Adds mask maps to the list (each must match the map's nside; files
    // already in the list are skipped). Returns the number added.
    int addMaskFiles(const QStringList &files);
    void removeMask(int index);
    void clearMasks();
    void setMaskEnabled(int index, bool enabled);

    int maskCount() const
    {
        return static_cast<int>(m_masks.size());
    }
    QString maskName(int index) const
    {
        return m_masks[index].name;
    }
    QString maskPath(int index) const
    {
        return m_masks[index].path;
    }
    bool maskEnabled(int index) const
    {
        return m_masks[index].enabled;
    }
    int maskMode() const
    {
        return m_maskMode;
    }
    float maskOpacity() const
    {
        return m_maskOpacity;
    }
    void setMaskMode(int mode)
    {
        m_maskMode = mode;
        update();
    }
    void setMaskOpacity(float opacity)
    {
        m_maskOpacity = opacity;
        update();
    }
    void setMap(const Healpix_Map<float> &map);

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
    void zoomChanged(float zoom);
    void masksChanged(); // list or enabled flags changed
    // Emitted when the implicit colormap range changes (e.g. mask applied)
    void colormapRangeChanged(float min, float max);
    // lonLat in degrees; value is NaN and index -1 when off the map
    void cursorPosChanged(QPointF lonLat, double value, qlonglong index);

private:
    QVector2D toNdc(const QPointF &pos) const;
    void emitCursorData();
    void uploadMapToGPU(const Healpix_Map<float> &map);
    void uploadMaskToGPU();
    void recomputeDataRange();
    void applyMaskChange(bool texturesChanged);
    int maskBits() const;
    void setupGeometry();

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
    QPointF m_cursorPos; // last pointer position in widget coords
    struct MaskEntry
    {
        QString name;
        QString path;
        std::vector<uint8_t> data; // 0..255, 255 = keep, 0 = masked
        bool enabled = true;
    };
    std::vector<MaskEntry> m_masks;
    static constexpr int kMaxMasks = 16; // 4 textures x 4 channels
    std::array<GLuint, 4> m_maskTextures{};
    bool m_maskDirty = false; // mask textures need re-uploading
    int m_maskMode       = 0;
    float m_maskOpacity  = 0.8f;
};
