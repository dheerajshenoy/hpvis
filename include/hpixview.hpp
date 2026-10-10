#pragma once

#include "argparse.hpp"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "openglwidget.hpp"
#include "sidepanel.hpp"
#include "statusbar.hpp"
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
    void initViewConnections(const View *view);
    void initMenubar();
    void initCFITSIO();
    void updateStatusBarFromView(const View *view);
    void updateMaskListFromView(const View *view);
    void updateSidePanelStateFromView(const View *view);

private slots:
    void onPixelOrderingChanged(PixelOrdering ordering);
    void onTabChanged(int index);
    void onTabCloseRequested(int index);
    void onColormapChanged(Colormap colormap);
    void onColormapMaxChanged(double max);
    void onColormapMinChanged(double min);
    void onColormapInvertChanged(bool invert);
    void onColormapScaleChanged(ColormapScale scale);
    void onColormapRangeResetRequested();
    void onBackgroundColorChanged(const QColor &color);
    void onUnseenColorChanged(const QColor &color);
    void onMaskFilesSelected(const QStringList &files);
    void onMaskCleared();
    void onMinimapToggled(bool enabled);

private:
    QMenu *m_editMenu = nullptr;

    QMenu *m_fileMenu               = nullptr;
    QAction *m_fileOpenAction       = nullptr;
    QAction *m_fileOpenRecentAction = nullptr;

    QMenu *m_viewMenu              = nullptr;
    QAction *m_viewMinimapAction   = nullptr;
    QAction *m_viewSidePanelAction = nullptr;

    View *m_view           = nullptr;
    TabWidget *m_tabWidget = nullptr;
    SidePanel *m_sidePanel = nullptr;
    StatusBar *m_statusBar = nullptr;
};
