#pragma once

#include "appsettings.hpp"
#include "argparse.hpp"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "openglwidget.hpp"
#include "recentfiles.hpp"
#include "statusbar.hpp"
#include "tabwidget.hpp"
#include "view.hpp"
#include "viewtoolbar.hpp"

#include <QMainWindow>
#include <QTabWidget>

class HpixView : public QMainWindow
{
    Q_OBJECT
public:
    explicit HpixView(const argparse::ArgumentParser &parser,
                      QWidget *parent = nullptr);

    void OpenFile(const QString &filePath = QString());

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void parseArgs(const argparse::ArgumentParser &parser);
    void initUI();
    void initConnections();
    void initViewConnections(const View *view);
    void initMenubar();
    void updateRecentMenu();
    void applyDefaults(View *view);
    void initCFITSIO();
    void updateStatusBarFromView(const View *view);
    void updateMaskListFromView(const View *view);
    void updateControlsFromView(const View *view);
    void rebuildMasksMenu(const View *view);
    void initShortcuts();

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
    void onExportActionTriggered();
    void onSettingsActionTriggered();
    void applySavedSettings();
    void onGraticuleToggled(bool enabled);
    void onPowerSpectrumActionTriggered();
    void onStatisticsActionTriggered();
    void onRotationActionTriggered();

private:
    QMenu *m_editMenu = nullptr;

    QMenu *m_fileMenu           = nullptr;
    QAction *m_fileOpenAction   = nullptr;
    QMenu *m_fileOpenRecentMenu = nullptr;
    RecentFiles m_recentFiles;
    AppSettings m_settings        = AppSettings::load();
    QAction *m_fileExportAction   = nullptr;
    QAction *m_fileCloseTabAction = nullptr;

    QMenu *m_viewMenu              = nullptr;
    QAction *m_viewMinimapAction   = nullptr;
    QAction *m_headerAction        = nullptr;
    QAction *m_viewColorBarAction  = nullptr;
    QMenu *m_pixelOrderingMenu     = nullptr;
    QAction *m_nestedAction        = nullptr;
    QAction *m_ringAction          = nullptr;
    QMenu *m_graticuleMenu         = nullptr;
    QMenu *m_graticuleStepMenu     = nullptr;
    QMenu *m_masksMenu             = nullptr;
    QAction *m_viewResetViewAction = nullptr;
    QMenu *m_viewZoomMenu          = nullptr;

    QMenu *m_toolsMenu             = nullptr;
    QMenu *m_toolsAnalysisMenu     = nullptr;
    QAction *m_toolsSettingsAction = nullptr;

    View *m_view           = nullptr;
    TabWidget *m_tabWidget = nullptr;
    ViewToolBar *m_toolBar = nullptr;
    StatusBar *m_statusBar = nullptr;
};
