#pragma once

#include "colormap.hpp"
#include "healpix_map.h"
#include "openglwidget.hpp"
#include "pixelordering.hpp"

#include <QVBoxLayout>
#include <QWidget>

class View : public QWidget
{
    Q_OBJECT

public:
    View(QWidget *parent = nullptr);
    ~View();

    inline PixelOrdering pixelOrdering() const
    {
        return m_glWidget->pixelOrdering();
    }

    inline Colormap colormap() const
    {
        return m_glWidget->colormap();
    }

    inline void setPixelOrdering(PixelOrdering ordering)
    {
        m_glWidget->setPixelOrdering(ordering);
    }

    inline void setColormap(Colormap colormap)
    {
        m_glWidget->setColormap(colormap);
    }

    inline float colormapMin() const
    {
        return m_glWidget->colormapMin();
    }

    inline float colormapMax() const
    {
        return m_glWidget->colormapMax();
    }

    inline void resetColormapRange()
    {
        m_glWidget->resetColormapRange();
    }

    inline void setColormapMin(double min)
    {
        m_glWidget->setColormapMin(min);
    }

    inline void setColormapMax(double max)
    {
        m_glWidget->setColormapMax(max);
    }

    inline ColormapScale colormapScale() const
    {
        return m_glWidget->colormapScale();
    }

    inline void setColormapScale(ColormapScale scale)
    {
        m_glWidget->setColormapScale(scale);
    }

    inline bool colormapInvert() const
    {
        return m_glWidget->colormapInvert();
    }

    inline void setColormapInvert(bool invert)
    {
        m_glWidget->setColormapInvert(invert);
    }

    inline QColor backgroundColor() const
    {
        return m_glWidget->backgroundColor();
    }

    inline void setBackgroundColor(const QColor &color)
    {
        m_glWidget->setBackgroundColor(color);
    }

    inline QColor unseenColor() const
    {
        return m_glWidget->unseenColor();
    }

    inline void setUnseenColor(const QColor &color)
    {
        m_glWidget->setUnseenColor(color);
    }

    bool loadMap(const std::string &filename);

    const QString &filePath() const
    {
        return m_filePath;
    }
    float zoom() const
    {
        return m_glWidget->zoom();
    }

private:
    void initConnections();

signals:
    // lonLat in degrees; value is NaN and index -1 when off the map
    void cursorPosChanged(QPointF lonLat, double value, qlonglong index);
    void zoomChanged(float zoom);

private:
    OpenGLWidget *m_glWidget = nullptr;
    QString m_filePath;
};
