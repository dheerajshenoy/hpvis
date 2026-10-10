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

    inline const QString &filePath() const
    {
        return m_filePath;
    }

    inline int addMaskFiles(const QStringList &files)
    {
        return m_glWidget->addMaskFiles(files);
    }
    inline void removeMask(int index)
    {
        m_glWidget->removeMask(index);
    }
    inline void clearMasks()
    {
        m_glWidget->clearMasks();
    }
    inline int maskCount() const
    {
        return m_glWidget->maskCount();
    }
    inline QString maskName(int index) const
    {
        return m_glWidget->maskName(index);
    }
    inline QString maskPath(int index) const
    {
        return m_glWidget->maskPath(index);
    }
    inline bool maskEnabled(int index) const
    {
        return m_glWidget->maskEnabled(index);
    }
    inline int maskMode() const
    {
        return m_glWidget->maskMode();
    }
    inline float maskOpacity() const
    {
        return m_glWidget->maskOpacity();
    }
    inline void setMaskEnabled(int index, bool enabled)
    {
        m_glWidget->setMaskEnabled(index, enabled);
    }
    inline void setMaskMode(int mode)
    {
        m_glWidget->setMaskMode(mode);
    }
    inline void setMaskOpacity(float opacity)
    {
        m_glWidget->setMaskOpacity(opacity);
    }

    inline float zoom() const
    {
        return m_glWidget->zoom();
    }

    bool loadMap(const std::string &filename);

private:
    void initConnections();

signals:
    // lonLat in degrees; value is NaN and index -1 when off the map
    void cursorPosChanged(QPointF lonLat, double value, qlonglong index);
    void zoomChanged(float zoom);
    void masksChanged();
    void colormapRangeChanged(float min, float max);

private:
    OpenGLWidget *m_glWidget = nullptr;
    QString m_filePath;
};
