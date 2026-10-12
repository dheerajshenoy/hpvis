#pragma once

#include "appsettings.hpp"
#include "argparse.hpp"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "openglwidget.hpp"
#include "openmap.hpp"
#include "powerspectrumdock.hpp"
#include "recentfiles.hpp"
#include "statisticsdock.hpp"
#include "statusbar.hpp"
#include "tabwidget.hpp"
#include "view.hpp"
#include "viewtoolbar.hpp"

#include <QMainWindow>
#include <QSet>
#include <QTabWidget>
#include <memory>
#include <optional>

class MapOperationDialog;

class HpVis : public QMainWindow
{
    Q_OBJECT
public:
    explicit HpVis(const argparse::ArgumentParser &parser,
                   QWidget *parent = nullptr);

    // hdu / column are 1-based; 0 picks the first map HDU / column
    void OpenFile(const QString &filePath = QString(), int hdu = 0,
                  int column = 0);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    // Command-line overrides of the saved settings, applied to every map
    // opened from the command line
    struct CliOptions
    {
        std::optional<Colormap> colormap;
        std::optional<ColormapScale> scale;
        std::optional<Projection> projection;
        std::optional<bool> viewFromOutside;
        std::optional<Orientation> orientation;
        std::optional<double> min, max;
        bool reverse   = false;
        bool graticule = false;
        QStringList masks;
        QString exportPath;
    };
    void applyCliOptions(View *view);

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
    void updateDocks();
    void updateCoordinateNames();
    void setOrientation(Orientation orientation);
    void rebuildMasksMenu(const View *view);
    void initShortcuts();
    void applyInterfaceSettings();
    QString startDirectory() const;
    void rememberDirectory(const QString &path) const;

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
    void onRotationActionTriggered();
    void onOperateTriggered();
    QList<OpenMap> openMaps() const;
    void onSaveMapTriggered();
    void openComputedMap(std::shared_ptr<Healpix_Map<float>> map,
                         const QString &title, SkyFrame frame);
    void onLoadColormapTriggered();
    void onColormapRangeActionTriggered();

private:
    CliOptions m_cli;
    QSet<QString> m_cliPaths; // files opened from the command line

    QMenu *m_editMenu                = nullptr;
    QAction *m_editCopyParamsAction  = nullptr;
    QAction *m_editPasteParamsAction = nullptr;

    QMenu *m_fileMenu           = nullptr;
    QAction *m_fileOpenAction   = nullptr;
    QMenu *m_fileOpenRecentMenu = nullptr;
    RecentFiles m_recentFiles;
    AppSettings m_settings        = AppSettings::load();
    QAction *m_fileSaveMapAction  = nullptr;
    QAction *m_fileExportAction   = nullptr;
    QAction *m_fileCloseTabAction = nullptr;

    QMenu *m_viewMenu             = nullptr;
    QAction *m_viewMinimapAction  = nullptr;
    QAction *m_headerAction       = nullptr;
    QAction *m_viewColorBarAction = nullptr;
    QMenu *m_orientationMenu      = nullptr;
    QList<QAction *> m_orientationActions; // indexed by Orientation
    QMenu *m_perspectiveMenu         = nullptr;
    QAction *m_insideAction          = nullptr;
    QAction *m_outsideAction         = nullptr;
    QMenu *m_pixelOrderingMenu       = nullptr;
    QAction *m_nestedAction          = nullptr;
    QAction *m_ringAction            = nullptr;
    QMenu *m_graticuleMenu           = nullptr;
    QAction *m_graticuleShowAction   = nullptr;
    QMenu *m_graticuleStepMenu       = nullptr;
    QAction *m_reverseColormapAction = nullptr;
    QAction *m_scaleLinearAction     = nullptr;
    QAction *m_scaleLogAction        = nullptr;
    QMenu *m_masksMenu               = nullptr;
    QAction *m_viewResetViewAction   = nullptr;
    QMenu *m_viewZoomMenu            = nullptr;

    QMenu *m_toolsMenu             = nullptr;
    QMenu *m_toolsAnalysisMenu     = nullptr;
    QAction *m_toolsSettingsAction = nullptr;

    View *m_view                        = nullptr;
    TabWidget *m_tabWidget              = nullptr;
    ViewToolBar *m_toolBar              = nullptr;
    MapOperationDialog *m_operateDialog = nullptr;
    StatisticsDock *m_statsDock         = nullptr;
    PowerSpectrumDock *m_powerDock      = nullptr;
    StatusBar *m_statusBar              = nullptr;
};
