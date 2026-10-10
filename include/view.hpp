#pragma once

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

    void loadMap(const std::string &filename);
    void setPixelOrdering(PixelOrdering ordering);

private:
    QVBoxLayout *m_layout    = nullptr;
    OpenGLWidget *m_glWidget = nullptr;
    Healpix_Map<float> m_healpix_map;
    bool m_mapLoaded = false;
};
