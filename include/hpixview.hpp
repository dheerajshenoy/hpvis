#pragma once

#include "argparse.hpp"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "openglwidget.hpp"
#include "sidepanel.hpp"
#include "tabwidget.hpp"
#include "view.hpp"

#include <QMainWindow>
#include <QTabWidget>

class HpixView : public QMainWindow
{
    Q_OBJECT
public:
    explicit HpixView(const argparse::ArgumentParser &parser,
                      QWidget *parent = nullptr);

    void OpenFile(const QString &filePath = QString());

private:
    void parseArgs(const argparse::ArgumentParser &parser);
    void initUI();
    void initConnections();
    void initMenubar();
    void initCFITSIO();
    void updateSidePanelStateFromView(const View *view);

private slots:
    void onPixelOrderingChanged(PixelOrdering ordering);
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onColormapChanged(Colormap colormap);
    void onColormapMaxChanged(double max);
    void onColormapMinChanged(double min);

private:
    QMenu *m_fileMenu = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_viewMenu = nullptr;

    View *m_view = nullptr;

    TabWidget *m_tabWidget = nullptr;
    SidePanel *m_sidePanel = nullptr;
};
