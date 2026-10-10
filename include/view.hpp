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

    inline const QString &filePath() const
    {
        return m_filePath;
    }

    // Direct access to the renderer, for settings (colormap, masks, ...)
    inline OpenGLWidget *glWidget() const
    {
        return m_glWidget;
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
