#pragma once

#include "healpix_map.h"
#include "openglwidget.hpp"

#include <QVBoxLayout>
#include <QWidget>

class View : public QWidget
{
public:
    View(QWidget *parent = nullptr);
    ~View();

    void loadMap(const std::string &filename);

private:
    QVBoxLayout *m_layout    = nullptr;
    OpenGLWidget *m_glWidget = nullptr;
    Healpix_Map<float> m_healpix_map;
    bool m_mapLoaded = false;
};
