#pragma once

#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "openglwidget.hpp"

#include <QMainWindow>
#include <QTabWidget>

class HpixView : public QMainWindow
{
    Q_OBJECT
public:
    explicit HpixView(int argc, char *argv[], QWidget *parent = nullptr);

    void OpenFile(const QString &filePath = QString());

private:
    void initUI();
    void initMenubar();
    void initCFITSIO();

private:
    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_viewMenu = nullptr;

    QTabWidget *m_tabWidget = nullptr;
};
