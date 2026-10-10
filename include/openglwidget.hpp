#pragma once

#include "colormap.hpp"
#include "pixelordering.hpp"

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <healpix_map.h>

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

    inline void setBackgroundColor(const QColor &color)
    {
        m_bgColor = color;
        update();
    }

    void setMap(const Healpix_Map<float> &map);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void uploadMapToGPU(const Healpix_Map<float> &map);
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
    Healpix_Map<float> m_pendingMap;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer}; // VBO wrapper
    QOpenGLShaderProgram m_shaderProgram;
};
