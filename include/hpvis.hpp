#pragma once

#include "appsettings.hpp"
#include "argparse.hpp"
#include "command.hpp"
#include "healpix_cxx/healpix_map.h"
#include "healpix_cxx/healpix_map_fitsio.h"
#include "mapcolumn.hpp"
#include "openglwidget.hpp"
#include "openmap.hpp"
#include "powerspectrumdock.hpp"
#include "recentfiles.hpp"
#include "shortcuts.hpp"
#include "fileexplorerdock.hpp"
#include "statisticsdock.hpp"
#include "statusbar.hpp"
#include "tabwidget.hpp"
#include "view.hpp"
#include "viewtoolbar.hpp"

#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QTabWidget>
#include <functional>
#include <memory>
#include <optional>

class DowngradeDialog;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class SmoothDialog;
class MultipoleFilterDialog;
class HarmonicDialog;
class UsageDialog;
class MapOperationDialog;
class RotationDialog;
class UnitsDialog;
class MaskBuilderDock;
class TransformDialog;
class RandomMapDialog;

class HpVis : public QMainWindow
{
    Q_OBJECT
public:
    explicit HpVis(const argparse::ArgumentParser &parser,
                   QWidget *parent = nullptr);

    // hdu / column are 1-based; 0 picks the first map HDU / column.
    // onDone (if given) runs when loading ended, with the new view or null
    // when it failed.
    void OpenFile(const QString &filePath = QString(), int hdu = 0,
                  int column = 0, const QString &columnName = QString(),
                  std::function<void(View *)> onDone = {});

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
        std::optional<int> pixelGrid;
        QStringList masks;
        QString outputPath;
    };
    void applyCliOptions(View *view);

    void parseArgs(const argparse::ArgumentParser &parser);
    void initUI();
    void initConnections();
    void initViewConnections(const View *view);
    void initMenubar();
    void updateRecentMenu();
    void saveSession() const;
    int storedSessionSize() const;
    void restoreSession();
    void restoreNextSessionEntry();
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
    // Every command a key can be bound to (menu actions and the viewer's
    // own keys), and the QAction behind each menu one
    struct ShortcutTarget
    {
        ShortcutCommand command;
        Command cmd     = Command::None;
        QAction *action = nullptr; // null for the viewer's own keys
    };
    QList<ShortcutTarget> shortcutTargets();
    void collectShortcutTargets(QMenu *menu, const QStringList &path,
                                QList<ShortcutTarget> &out);
    QKeySequence effectiveKey(const ShortcutCommand &command) const;
    QList<ShortcutCommand> shortcutCommands();
    void applyShortcuts();
    void buildMasksMenu(const View *view);
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
    void onColormapPercentileRequested();
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
    void onDowngradeTriggered();
    void onSmoothTriggered();
    void onMultipoleFilterTriggered();
    void onHarmonicTriggered();
    void onUnitsTriggered();
    void onTransformTriggered();
    void onRandomMapTriggered();
    QList<OpenMap> openMaps() const;
    void onSaveMapTriggered();
    void openComputedMap(std::shared_ptr<Healpix_Map<float>> map,
                         const QString &title, SkyFrame frame);
    void openComputedColumns(std::vector<ComputedColumn> columns,
                             const QString &title, SkyFrame frame, int current);
    void addComputedView(View *view, const QString &title);
    void onLoadColormapTriggered();
    void onColormapRangeActionTriggered();

private:
    QPair<QKeySequence, QKeySequence> m_regionKeys{QKeySequence(Qt::Key_Return),
                                                   QKeySequence(Qt::Key_Escape)};
    bool m_shortcutsReady = false; // the built-in keys are in place
    CliOptions m_cli;
    QSet<QString> m_cliPaths; // files opened from the command line

    // The files of the last session, opened one after the other so that the
    // tabs keep their order
    struct SessionEntry
    {
        QString path;
        int hdu    = 0;
        int column = 0;
    };
    QList<SessionEntry> m_sessionQueue;
    int m_sessionPos        = 0; // next entry to open
    int m_sessionCurrent    = -1; // entry that was the current tab
    int m_sessionMissing    = 0;
    QPointer<View> m_sessionCurrentView;

    QMenu *m_editMenu                = nullptr;
    QAction *m_editCopyParamsAction  = nullptr;
    QAction *m_editPasteParamsAction = nullptr;

    QMenu *m_fileMenu           = nullptr;
    QAction *m_fileOpenAction   = nullptr;
    QMenu *m_fileOpenRecentMenu = nullptr;
    QAction *m_fileReopenSessionAction = nullptr;
    RecentFiles m_recentFiles;
    AppSettings m_settings        = AppSettings::load();
    QAction *m_fileSaveMapAction  = nullptr;
    QAction *m_fileExportAction   = nullptr;
    QAction *m_fileExitAction     = nullptr;
    QAction *m_fileCloseTabAction = nullptr;

    QMenu *m_viewMenu             = nullptr;
    QMenu *m_colormapMenu         = nullptr;
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
    QAction *m_cellGridShowAction    = nullptr;
    QAction *m_cellLabelsAction      = nullptr;
    QSpinBox *m_cellGridNsideSpin    = nullptr;
    QPushButton *m_cellGridColorButton = nullptr;
    QDoubleSpinBox *m_graticuleStepSpin = nullptr;
    QPushButton *m_graticuleColorButton = nullptr;
    QAction *m_reverseColormapAction = nullptr;
    QAction *m_scaleLinearAction     = nullptr;
    QAction *m_scaleLogAction        = nullptr;
    QAction *m_scaleAsinhAction      = nullptr;
    QSlider *m_asinhSlider           = nullptr;
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
    RotationDialog *m_rotationDialog    = nullptr;
    UnitsDialog *m_unitsDialog          = nullptr;
    TransformDialog *m_transformDialog  = nullptr;
    RandomMapDialog *m_randomDialog     = nullptr;
    bool m_recenterOnRightClick         = true;
    bool m_lockRollOnRecenter           = false;
    DowngradeDialog *m_downgradeDialog  = nullptr;
    SmoothDialog *m_smoothDialog        = nullptr;
    MultipoleFilterDialog *m_multipoleDialog = nullptr;
    HarmonicDialog *m_harmonicDialog    = nullptr;
    UsageDialog *m_usageDialog          = nullptr;
    FileExplorerDock *m_explorerDock    = nullptr;
    StatisticsDock *m_statsDock         = nullptr;
    MaskBuilderDock *m_maskDock         = nullptr;
    PowerSpectrumDock *m_powerDock      = nullptr;
    StatusBar *m_statusBar              = nullptr;
};
