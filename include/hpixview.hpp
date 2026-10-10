#pragma once

#include "OpenGLWidget.hpp"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"

#include <QMainWindow>

class HpixView : public QMainWindow
{
    Q_OBJECT
public:
    explicit HpixView(int argc, char *argv[], QWidget *parent = nullptr);

    void OpenFile(const QString &filePath);

private:
    void initUI();
    void initCFITSIO();

private:
    OpenGLWidget *m_glWidget = nullptr;
    uint32_t m_nside         = 0;
};
