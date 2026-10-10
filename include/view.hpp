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

    inline void setBackgroundColor(const QColor &color)
    {
        m_glWidget->setBackgroundColor(color);
    }

    void loadMap(const std::string &filename);

private:
    QVBoxLayout *m_layout    = nullptr;
    OpenGLWidget *m_glWidget = nullptr;
    Healpix_Map<float> m_healpix_map;
    bool m_mapLoaded = false;
};
