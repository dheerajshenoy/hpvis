#include "hpvis.hpp"
#include "aboutcontent.hpp"
#include "usagedialog.hpp"

#include "colormapregistry.hpp"
#include "command.hpp"
#include "downgradedialog.hpp"
#include "exportdialog.hpp"
#include "smoothdialog.hpp"
#include "multipolefilterdialog.hpp"
#include "harmonicdialog.hpp"
#include "fitsheaderdialog.hpp"
#include "masksourcedialog.hpp"
#include "mapoperationdialog.hpp"
#include "npyreader.hpp"
#include "pixelordering.hpp"
#include "powerspectrumdock.hpp"
#include "randommapdialog.hpp"
#include "rotationdialog.hpp"
#include "transformdialog.hpp"
#include "unitsdialog.hpp"
#include "settingsdialog.hpp"
#include "statisticsdock.hpp"
#include "view.hpp"
#include "viewtoolbar.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsView>
#include <QInputDialog>
#include <QClipboard>
#include <QGuiApplication>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QWidgetAction>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>
#include <iostream>
#include <tuple>

namespace
{
// Marks `action` as the command `command` (for its key binding)
QAction *
tag(QAction *action, Command command)
{
    action->setProperty("hpvisCommand", int(command));
    return action;
}

// An icon of the user's icon theme, else Qt's own for `fallback` (pass
// SP_CustomBase for none: then a missing theme icon leaves the item bare)
QIcon
themeIcon(const QString &name,
          QStyle::StandardPixmap fallback = QStyle::SP_CustomBase)
{
    if (fallback == QStyle::SP_CustomBase)
        return QIcon::fromTheme(name);
    return QIcon::fromTheme(name, QApplication::style()->standardIcon(fallback));
}

// Steps by powers of two: the pixel grid only takes power-of-two nsides
class Pow2SpinBox : public QSpinBox
{
public:
    using QSpinBox::QSpinBox;
    void stepBy(int steps) override
    {
        int v = value();
        for (int i = 0; i < std::abs(steps); ++i)
            v = steps > 0 ? v * 2 : std::max(1, v / 2);
        setValue(v);
    }
};

void
setSwatchColor(QPushButton *button, const QColor &c)
{
    if (!button)
        return;
    button->setStyleSheet(
        QString("background-color: rgba(%1,%2,%3,%4); border: 1px solid "
                "palette(mid); border-radius: 3px;")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue())
            .arg(c.alpha()));
}
} // namespace

HpVis::HpVis(const argparse::ArgumentParser &parser, QWidget *parent)
    : QMainWindow(parent)
{
    m_recentFiles.setMaxCount(m_settings.recentFilesMax);
    initUI();
    parseArgs(parser);
    // Files given on the command line replace the last session
    if (m_settings.restoreSession
        && !parser.is_used("files"))
        restoreSession();
    initShortcuts();
    restoreState(QSettings().value("window/state").toByteArray());
    this->show();
}

void
HpVis::parseArgs(const argparse::ArgumentParser &parser)
{
    auto fail = [](const std::string &what)
    {
        std::cerr << what << std::endl;
    };

    if (auto name = parser.present<std::string>("--colormap"))
    {
        auto &registry = ColormapRegistry::instance();
        QString n      = QString::fromStdString(*name);
        if (n.compare("gray", Qt::CaseInsensitive) == 0
            || n.compare("grey", Qt::CaseInsensitive) == 0)
            n = "grayscale";

        int id = registry.indexOf(n);
        if (id < 0 && QFileInfo::exists(n)) // a colormap file
        {
            QString error;
            id = registry.addFile(n, false, &error);
            if (id < 0)
                fail("Colormap " + *name + ": " + error.toStdString());
        }
        else if (id < 0)
            fail("Unknown colormap '" + *name + "' (choose from "
                 + registry.names().join(", ").toStdString() + ")");
        if (id >= 0)
            m_cli.colormap = Colormap(id);
    }

    if (auto scale = parser.present<std::string>("--cscale"))
        m_cli.scale
            = (*scale == "log")     ? ColormapScale::LOG
              : (*scale == "asinh") ? ColormapScale::ASINH
                                    : ColormapScale::LINEAR;

    if (auto proj = parser.present<std::string>("--projection"))
    {
        static const QStringList names
            = {"mollweide", "equirectangular", "orthographic",
               "azimuthal_equidistant", "gnomonic", "mercator", "sinusoidal",
               "lambert_cylindrical", "gall_peters", "stereographic",
               "hammer", "aitoff", "werner", "lambert_azimuthal"};
        const int i = names.indexOf(QString::fromStdString(*proj));
        if (i >= 0)
            m_cli.projection = Projection(i);
    }

    if (auto ori = parser.present<std::string>("--orientation"))
    {
        static const QStringList names
            = {"free", "galactic", "ecliptic", "equatorial"};
        const int i = names.indexOf(QString::fromStdString(*ori));
        if (i >= 0)
            m_cli.orientation = Orientation(i);
    }

    if (auto view = parser.present<std::string>("--view"))
        m_cli.viewFromOutside = (*view == "outside");

    m_cli.min       = parser.present<double>("--cmin");
    m_cli.max       = parser.present<double>("--cmax");
    m_cli.reverse   = parser.get<bool>("--creverse");
    m_cli.graticule = parser.get<bool>("--graticule");
    m_cli.pixelGrid = parser.present<int>("--pixel-grid");

    if (parser.is_used("--mask"))
        for (const auto &m : parser.get<std::vector<std::string>>("--mask"))
            m_cli.masks << QString::fromStdString(m);

    if (auto out = parser.present<std::string>("--output"))
        m_cli.outputPath = QString::fromStdString(*out);

    const int hdu    = parser.present<int>("--hdu").value_or(0);
    // --column takes a number or a column name (TT, QQ, ...)
    int column = 0;
    QString columnName;
    if (auto c = parser.present<std::string>("--column"))
    {
        bool isNumber = false;
        column        = QString::fromStdString(*c).toInt(&isNumber);
        if (!isNumber)
        {
            column     = 0;
            columnName = QString::fromStdString(*c);
        }
    }

    const auto cliFiles
        = parser.present<std::vector<std::string>>("files").value_or(
            std::vector<std::string>());
    for (const auto &file : cliFiles)
    {
        const QString path = QString::fromStdString(file);
        m_cliPaths.insert(path);
        OpenFile(path, hdu, column, columnName);
    }
}

// Applies the command-line options on top of the defaults, then exports and
// quits when --output was given
void
HpVis::applyCliOptions(View *view)
{
    OpenGLWidget *gl = view->glWidget();

    if (m_cli.colormap)
        gl->setColormap(*m_cli.colormap);
    if (m_cli.reverse)
        gl->setColormapInvert(true);
    if (m_cli.scale)
        gl->setColormapScale(*m_cli.scale);
    if (m_cli.projection)
        gl->setProjection(*m_cli.projection);
    if (m_cli.orientation)
        gl->setOrientation(*m_cli.orientation);
    if (m_cli.viewFromOutside)
        gl->setViewFromOutside(*m_cli.viewFromOutside);
    if (m_cli.graticule)
        gl->setGraticuleEnabled(true);
    if (m_cli.pixelGrid)
    {
        gl->setCellGridNside(*m_cli.pixelGrid);
        gl->setCellGridEnabled(true);
    }
    if (m_cli.min)
        gl->setColormapMin(*m_cli.min);
    if (m_cli.max)
        gl->setColormapMax(*m_cli.max);
    if (!m_cli.masks.isEmpty())
        gl->addMaskFiles(m_cli.masks);

    if (!m_cli.outputPath.isEmpty())
    {
        const QString out = m_cli.outputPath;
        m_cli.outputPath.clear(); // only the first file
        // Give the GPU a moment to upload and draw the map (and masks)
        QTimer::singleShot(1000, this, [this, gl, out]()
        {
            gl->exportImage(out);
            QApplication::quit();
        });
    }
}

void
HpVis::initUI()
{
    QVBoxLayout *layout = new QVBoxLayout();
    QWidget *widget     = new QWidget();
    widget->setLayout(layout);

    layout->setContentsMargins(0, 0, 0, 0);
    widget->setContentsMargins(0, 0, 0, 0);
    m_tabWidget = new TabWidget();
    m_tabWidget->setContentsMargins(0, 0, 0, 0);
    m_toolBar   = new ViewToolBar();
    m_explorerDock = new FileExplorerDock(this);
    if (!QSettings().contains("fileExplorer/root"))
        m_explorerDock->setRootPath(startDirectory());
    // A file that is already open gets its tab back instead of a second one
    connect(m_explorerDock, &FileExplorerDock::openRequested, this,
            [this](const QString &path)
    {
        const QString wanted = QFileInfo(path).canonicalFilePath();
        for (int i = 0; i < m_tabWidget->count(); ++i)
            if (auto *v = qobject_cast<View *>(m_tabWidget->widget(i));
                v && !v->filePath().isEmpty()
                && QFileInfo(v->filePath()).canonicalFilePath() == wanted)
            {
                m_tabWidget->setCurrentIndex(i);
                return;
            }
        rememberDirectory(path);
        OpenFile(path);
    });
    m_statsDock = new StatisticsDock(this);
    m_statsDock->setOpenMapsProvider([this]() { return openMaps(); });
    m_powerDock = new PowerSpectrumDock(this);
    m_powerDock->setOpenMapsProvider([this]() { return openMaps(); });
    m_statusBar = new StatusBar();
    m_toolBar->setEnabled(false);
    initMenubar();
    setAcceptDrops(true);
    layout->addWidget(m_tabWidget);
    setCentralWidget(widget);

    addToolBar(Qt::TopToolBarArea, m_toolBar);
    addDockWidget(Qt::LeftDockWidgetArea, m_explorerDock);
    addDockWidget(Qt::RightDockWidgetArea, m_statsDock);
    addDockWidget(Qt::RightDockWidgetArea, m_powerDock);
    m_statsDock->hide();
    m_powerDock->hide();

    // QMainWindow's own status bar sits below the dock areas and spans the
    // full window width; our widget fills it.
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(m_statusBar, 1);

    initConnections();
    applyInterfaceSettings();
}

void
HpVis::initConnections()
{
    connect(m_toolBar, &ViewToolBar::projectionChanged, this,
            [this](Projection projection)
    {
        if (m_view)
            m_view->glWidget()->setProjection(projection);
    });

    connect(m_toolBar, &ViewToolBar::orientationChanged, this,
            &HpVis::setOrientation);

    connect(m_toolBar, &ViewToolBar::fitsSelectionChanged, this,
            [this](int hdu, int column)
    {
        if (!m_view)
            return;
        if (m_view->loading())
        {
            // Busy: put the combos back to what is loaded
            m_toolBar->setFitsInfo(m_view->fitsInfo(), m_view->hdu(),
                                   m_view->column());
            return;
        }
        m_statusBar->showMessage(
            QString("Loading HDU %1, column %2...").arg(hdu).arg(column));
        m_view->loadHdu(hdu, column);
    });

    connect(m_toolBar, &ViewToolBar::colormapChanged, this,
            &HpVis::onColormapChanged);

    // Plot colors picked in the dock are the new defaults in the settings
    connect(m_powerDock, &PowerSpectrumDock::plotColorsChanged, this,
            [this](const QColor &bg, const QColor &fg, const QColor &line)
    {
        m_settings.plotBackground = bg;
        m_settings.plotForeground = fg;
        m_settings.plotLine       = line;
        m_statsDock->setPlotColors(bg, fg, line);
    });

    connect(m_tabWidget, &TabWidget::currentChanged, this,
            &HpVis::onTabChanged);

    connect(m_tabWidget, &TabWidget::tabCloseRequested, this,
            &HpVis::onTabCloseRequested);
}

void
HpVis::initMenubar()
{
    QMenuBar *menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    m_fileMenu = menuBar->addMenu("&File");

    m_fileOpenAction
        = tag(m_fileMenu->addAction("&Open...", this, [this]() { OpenFile(); }), Command::FileOpen);

    m_fileOpenAction->setIcon(
        themeIcon("document-open", QStyle::SP_DialogOpenButton));

    m_fileOpenRecentMenu = m_fileMenu->addMenu("&Open Recent");
    tag(m_fileOpenRecentMenu->menuAction(), Command::FileOpenRecent);
    m_fileOpenRecentMenu->setIcon(
        themeIcon("document-open-recent", QStyle::SP_DirOpenIcon));
    updateRecentMenu();

    m_fileReopenSessionAction = tag(m_fileMenu->addAction(
        "Reopen &Last Session", this, &HpVis::restoreSession), Command::FileReopenSession);
    m_fileReopenSessionAction->setIcon(
        themeIcon("document-revert", QStyle::SP_BrowserReload));
    connect(m_fileMenu, &QMenu::aboutToShow, this, [this]()
    {
        m_fileReopenSessionAction->setEnabled(storedSessionSize() > 0);
    });

    m_fileMenu->addSeparator();

    m_headerAction = tag(m_fileMenu->addAction("FITS &Header...", this, [this]()
    {
        if (!m_view)
        {
            QMessageBox::information(this, "FITS Header", "Open a file first.");
            return;
        }
        if (m_view->isComputed())
        {
            QMessageBox::information(
                this, "FITS Header",
                "This map was computed, so it has no FITS header. Save it "
                "with File > Save Map As... to make a FITS file.");
            return;
        }
        if (isNumpyPath(m_view->filePath().toStdString()))
        {
            QMessageBox::information(this, "FITS Header",
                                     ".npy and .npz files have no header.");
            return;
        }
        FitsHeaderDialog dialog(m_view->filePath(), m_view->hdu(), this);
        dialog.exec();
    }), Command::FileHeader);
    m_headerAction->setIcon(
        themeIcon("document-properties", QStyle::SP_FileDialogInfoView));

    m_fileSaveMapAction = tag(m_fileMenu->addAction("&Save Map As...", this,
                                                &HpVis::onSaveMapTriggered), Command::FileSaveMap);
    m_fileSaveMapAction->setIcon(
        themeIcon("document-save-as", QStyle::SP_DialogSaveButton));

    m_fileExportAction = tag(m_fileMenu->addAction("&Export Image...", this,
                                               &HpVis::onExportActionTriggered), Command::FileExport);
    m_fileExportAction->setIcon(
        themeIcon("document-export", QStyle::SP_FileIcon));

    m_fileMenu->addSeparator();

    m_fileCloseTabAction = tag(m_fileMenu->addAction("&Close Tab", this, [this]()
    {
        int index = m_tabWidget->currentIndex();
        if (index >= 0)
            onTabCloseRequested(index);
    }), Command::FileCloseTab);
    m_fileCloseTabAction->setIcon(
        themeIcon("tab-close", QStyle::SP_DialogCloseButton));

    m_fileExitAction = tag(m_fileMenu->addAction("&Exit", this, &HpVis::close), Command::FileExit);
    m_fileExitAction->setIcon(
        themeIcon("application-exit", QStyle::SP_TitleBarCloseButton));

    m_editMenu = menuBar->addMenu("&Edit");
    tag(m_editMenu->addAction(themeIcon("edit-copy"), "&Copy Image",
                          QKeySequence("Ctrl+Shift+C"), this,
                          [this]()
    {
        if (!m_view)
            return;
        QGuiApplication::clipboard()->setImage(
            m_view->glWidget()->grabFramebuffer());
        statusBar()->showMessage("Image copied to clipboard", 3000);
    }), Command::EditCopyImage);
    m_viewMenu = menuBar->addMenu("&View");

    m_viewMenu->addAction(
        tag(m_explorerDock->toggleViewAction(), Command::ViewFileExplorer));
    m_explorerDock->toggleViewAction()->setText("File &Explorer");
    m_explorerDock->toggleViewAction()->setIcon(
        themeIcon("folder-open", QStyle::SP_DirOpenIcon));

    m_viewMinimapAction
        = tag(m_viewMenu->addAction("&Minimap", this, &HpVis::onMinimapToggled), Command::ViewMinimap);
    m_viewMinimapAction->setCheckable(true);

    m_viewMenu->addSeparator();

    // Orientation (radio items: Free / Galactic / Ecliptic / Equatorial)
    m_orientationMenu      = m_viewMenu->addMenu("&Orientation");
    auto *orientationGroup = new QActionGroup(this);
    const QStringList orientationNames
        = {"&Default", "&Galactic", "&Ecliptic", "E&quatorial"};
    const QStringList orientationTips
        = {"The map's own frame; dragging also rolls the view",
           "Show the sky in Galactic coordinates, north up",
           "Show the sky in Ecliptic coordinates, north up",
           "Show the sky in Equatorial (J2000) coordinates, north up"};
    const Command orientationCommands[]
        = {Command::ViewOrientationDefault, Command::ViewOrientationGalactic,
           Command::ViewOrientationEcliptic, Command::ViewOrientationEquatorial};
    for (int i = 0; i < orientationNames.size(); ++i)
    {
        QAction *a = tag(m_orientationMenu->addAction(orientationNames[i]),
                         orientationCommands[i]);
        a->setStatusTip(orientationTips[i]);
        a->setCheckable(true);
        orientationGroup->addAction(a);
        m_orientationActions << a;
        connect(a, &QAction::triggered, this,
                [this, i]() { setOrientation(Orientation(i)); });
    }
    m_orientationActions[0]->setChecked(true);

    // Perspective (radio items: Inside / Outside)
    m_perspectiveMenu      = m_viewMenu->addMenu("&Perspective");
    auto *perspectiveGroup = new QActionGroup(this);
    m_insideAction         = tag(m_perspectiveMenu->addAction("&Inside (sky)"), Command::ViewPerspectiveInside);
    m_outsideAction        = tag(m_perspectiveMenu->addAction("&Outside (globe)"), Command::ViewPerspectiveOutside);
    m_insideAction->setStatusTip(
        "The sky seen from the center: longitude increases to the left");
    m_outsideAction->setStatusTip(
        "The mirror image, like a globe: longitude increases to the right");
    for (QAction *a : {m_insideAction, m_outsideAction})
    {
        a->setCheckable(true);
        perspectiveGroup->addAction(a);
    }
    m_insideAction->setChecked(true);
    connect(m_insideAction, &QAction::triggered, this, [this]()
    {
        if (m_view)
            m_view->glWidget()->setViewFromOutside(false);
    });
    connect(m_outsideAction, &QAction::triggered, this, [this]()
    {
        if (m_view)
            m_view->glWidget()->setViewFromOutside(true);
    });

    // Pixel ordering (radio items: Nested / Ring)
    m_pixelOrderingMenu = m_viewMenu->addMenu("Pixel &Ordering");
    auto *orderingGroup = new QActionGroup(this);
    m_nestedAction      = tag(m_pixelOrderingMenu->addAction("&Nested"), Command::ViewPixelOrderingNested);
    m_ringAction        = tag(m_pixelOrderingMenu->addAction("&Ring"), Command::ViewPixelOrderingRing);
    for (QAction *a : {m_nestedAction, m_ringAction})
    {
        a->setCheckable(true);
        orderingGroup->addAction(a);
    }
    m_nestedAction->setChecked(true);
    connect(m_nestedAction, &QAction::triggered, this,
            [this]() { onPixelOrderingChanged(PixelOrdering::NESTED); });
    connect(m_ringAction, &QAction::triggered, this,
            [this]() { onPixelOrderingChanged(PixelOrdering::RING); });

    m_viewMenu->addSeparator();

    m_viewZoomMenu = m_viewMenu->addMenu("&Zoom");

    tag(m_viewZoomMenu->addAction(themeIcon("zoom-in"), "Zoom &In", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->zoomStep(1.25f);
    }), Command::ViewZoomIn);
    tag(m_viewZoomMenu->addAction(themeIcon("zoom-out"), "Zoom &Out", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->zoomStep(0.8f);
    }), Command::ViewZoomOut);
    tag(m_viewZoomMenu->addAction(themeIcon("zoom-original"), "Reset Zoom", this,
                              [this]()
    {
        if (m_view)
            m_view->glWidget()->resetZoom();
    }), Command::ViewResetZoom);
    tag(m_viewZoomMenu->addAction(themeIcon("zoom-fit-best"), "Zoom To Region",
                              this, [this]()
    {
        if (m_view)
            m_view->glWidget()->beginZoomToRegion();
    }), Command::ViewZoomToRegion);

    m_viewZoomMenu->addSeparator();

    auto zoomLevels
        = {0.1f, 0.25f, 0.5f, 1.0f,  2.0f,  4.0f,   8.0f,
       16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f};

    const Command zoomCommands[]
        = {Command::ViewZoom10,    Command::ViewZoom25,   Command::ViewZoom50,
           Command::ViewZoom100,   Command::ViewZoom200,  Command::ViewZoom400,
           Command::ViewZoom800,   Command::ViewZoom1600, Command::ViewZoom3200,
           Command::ViewZoom6400,  Command::ViewZoom12800,
           Command::ViewZoom25600, Command::ViewZoom51200};
    int zoomIndex = 0;
    for (float zoom : zoomLevels)
    {
        QString label = QString::number(zoom * 100) + "x";
        tag(m_viewZoomMenu->addAction(label, this, [this, zoom]()
        {
            if (m_view)
                m_view->glWidget()->setZoom(zoom);
        }), zoomCommands[zoomIndex++]);
    }

    // Moves a zoomed-in view; the keys are up to the user (Settings > Shortcuts)
    QMenu *panMenu = m_viewMenu->addMenu("&Pan");
    const std::tuple<const char *, float, float, Command> pans[]
        = {{"&Left", -1.0f, 0.0f, Command::ViewPanLeft},
           {"&Right", 1.0f, 0.0f, Command::ViewPanRight},
           {"&Up", 0.0f, 1.0f, Command::ViewPanUp},
           {"&Down", 0.0f, -1.0f, Command::ViewPanDown}};
    for (const auto &[name, dx, dy, command] : pans)
        tag(panMenu->addAction(name, this, [this, dx = dx, dy = dy]()
        {
            if (m_view)
                m_view->glWidget()->panStep(dx, dy);
        }), command);

    QMenu *rotationMenu = m_viewMenu->addMenu("&Rotation");
    tag(rotationMenu->addAction("&Set Rotation...", this,
                                &HpVis::onRotationActionTriggered),
        Command::ViewSetRotation);
    tag(rotationMenu->addAction("&Reset Rotation", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->resetRotation();
    }), Command::ViewResetRotation);

    // Where to look: the poles and the center of the frame shown
    rotationMenu->addSeparator();
    QMenu *centerMenu = rotationMenu->addMenu("&Center On");
    const std::tuple<const char *, double, double, Command> centers[]
        = {{"&North Pole", 90.0, 0.0, Command::ViewCenterNorthPole},
           {"&South Pole", -90.0, 0.0, Command::ViewCenterSouthPole},
           {"&Equator (0\u00b0, 0\u00b0)", 0.0, 0.0, Command::ViewCenterEquator}};
    for (const auto &[name, lat, lon, command] : centers)
        tag(centerMenu->addAction(QString::fromUtf8(name), this,
                                  [this, lat = lat, lon = lon]()
        {
            if (!m_view)
                return;
            // At a pole keep the longitude, so the picture does not spin
            double lo, la, ro;
            m_view->glWidget()->rotationAngles(lo, la, ro);
            m_view->glWidget()->centerOn(std::abs(lat) > 89.0 ? lo : lon, lat);
        }), command);

    // A right click glides the point under it to the center
    rotationMenu->addSeparator();
    QSettings viewSettings;
    m_recenterOnRightClick
        = viewSettings.value("view/recenterOnRightClick", true).toBool();
    m_lockRollOnRecenter
        = viewSettings.value("view/lockRollOnRecenter", false).toBool();
    auto applyRecenter = [this]()
    {
        QSettings s;
        s.setValue("view/recenterOnRightClick", m_recenterOnRightClick);
        s.setValue("view/lockRollOnRecenter", m_lockRollOnRecenter);
        for (int i = 0; i < m_tabWidget->count(); ++i)
            if (auto *view = qobject_cast<View *>(m_tabWidget->widget(i)))
            {
                view->glWidget()->setRecenterOnRightClick(
                    m_recenterOnRightClick);
                view->glWidget()->setLockRollOnRecenter(m_lockRollOnRecenter);
            }
    };
    QAction *recenter = tag(rotationMenu->addAction(
        "Recenter on &Right Click", this, [this, applyRecenter](bool on)
    {
        m_recenterOnRightClick = on;
        applyRecenter();
    }), Command::ViewRecenterRightClick);
    recenter->setCheckable(true);
    recenter->setChecked(m_recenterOnRightClick);
    recenter->setStatusTip(
        "A right click glides the point under it to the center of the view");
    QAction *lockRoll = tag(rotationMenu->addAction(
        "&Lock Roll When Recentering", this, [this, applyRecenter](bool on)
    {
        m_lockRollOnRecenter = on;
        applyRecenter();
    }), Command::ViewLockRoll);
    lockRoll->setCheckable(true);
    lockRoll->setChecked(m_lockRollOnRecenter);
    lockRoll->setStatusTip("In the free orientation, keep the roll of the "
                           "view when recentering (a frame keeps north up)");

    m_viewResetViewAction = tag(m_viewMenu->addAction("&Reset View", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->resetView();
    }), Command::ViewResetView);
    m_viewResetViewAction->setIcon(
        themeIcon("view-refresh", QStyle::SP_BrowserReload));


    m_viewMenu->addSeparator();

    // Colors
    QMenu *colorsMenu = m_viewMenu->addMenu("&Colors");
    tag(colorsMenu->addAction(themeIcon("preferences-desktop-color"),
                          "&Background Color...", this, [this]()
    {
        if (!m_view)
            return;
        QColor c = QColorDialog::getColor(m_view->glWidget()->backgroundColor(),
                                          this, "Select Background Color");
        if (c.isValid())
            onBackgroundColorChanged(c);
    }), Command::ViewBackgroundColor);
    tag(colorsMenu->addAction(themeIcon("preferences-desktop-color"),
                          "&Unseen Pixel Color...", this, [this]()
    {
        if (!m_view)
            return;
        QColor c = QColorDialog::getColor(m_view->glWidget()->unseenColor(),
                                          this, "Select Unseen Color");
        if (c.isValid())
            onUnseenColorChanged(c);
    }), Command::ViewUnseenColor);

    // Graticule spacing and color
    m_graticuleMenu       = m_viewMenu->addMenu("&Graticule");
    m_graticuleShowAction = tag(m_graticuleMenu->addAction(
        "&Show Graticule", this, &HpVis::onGraticuleToggled),
        Command::ViewGraticuleShow);
    m_graticuleShowAction->setCheckable(true);
    auto *stepWidget = new QWidget;
    auto *stepLayout = new QHBoxLayout(stepWidget);
    stepLayout->setContentsMargins(12, 2, 12, 2);
    stepLayout->addWidget(new QLabel("Spacing:"));
    m_graticuleStepSpin = new QDoubleSpinBox;
    m_graticuleStepSpin->setRange(1.0, 90.0);
    m_graticuleStepSpin->setDecimals(1);
    m_graticuleStepSpin->setSingleStep(5.0);
    m_graticuleStepSpin->setSuffix("°");
    m_graticuleStepSpin->setValue(30.0);
    m_graticuleStepSpin->setKeyboardTracking(false); // typed: apply on Enter
    stepLayout->addWidget(m_graticuleStepSpin);
    auto *stepAction = new QWidgetAction(this);
    stepAction->setDefaultWidget(stepWidget);
    m_graticuleMenu->addAction(stepAction);
    connect(m_graticuleStepSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double deg)
    {
        if (m_view)
            m_view->glWidget()->setGraticuleStep(deg);
    });

    // A "Color:" row with a swatch that opens a color dialog changing the
    // color live (Cancel restores the old one)
    auto addColorRow
        = [this](QMenu *menu, QPushButton *&button, bool alpha,
                 std::function<QColor()> get,
                 std::function<void(const QColor &)> set, const QString &title)
    {
        auto *widget = new QWidget;
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(12, 2, 12, 2);
        layout->addWidget(new QLabel("Color:"));
        button = new QPushButton;
        button->setFixedSize(48, 20);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip("Change the color");
        setSwatchColor(button, QColor(128, 128, 128)); // set on tab switch
        layout->addWidget(button);
        auto *action = new QWidgetAction(this);
        action->setDefaultWidget(widget);
        menu->addAction(action);

        QPushButton *btn = button;
        connect(btn, &QPushButton::clicked, this,
                [this, menu, btn, alpha, get, set, title]()
        {
            if (!m_view)
                return;
            // The dialog is a separate window: close the menu chain
            for (QWidget *w = menu; w; w = w->parentWidget())
                if (auto *m = qobject_cast<QMenu *>(w))
                    m->close();

            const QColor before = get();
            auto *dlg           = new QColorDialog(before, this);
            dlg->setWindowTitle(title);
            if (alpha)
                dlg->setOption(QColorDialog::ShowAlphaChannel);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &QColorDialog::currentColorChanged, this,
                    [btn, set](const QColor &c)
            {
                set(c);
                setSwatchColor(btn, c);
            });
            connect(dlg, &QColorDialog::rejected, this,
                    [btn, set, before]()
            {
                set(before);
                setSwatchColor(btn, before);
            });
            dlg->open();
        });
    };
    addColorRow(m_graticuleMenu, m_graticuleColorButton, false,
                [this]() { return m_view->glWidget()->graticuleColor(); },
                [this](const QColor &c)
    {
        if (m_view)
            m_view->glWidget()->setGraticuleColor(c);
    }, "Select Graticule Color");

    // Pixel grid: the boundaries of the HEALPix pixels of any nside
    QMenu *cellMenu     = m_viewMenu->addMenu("&Pixel Grid");
    m_cellGridShowAction = tag(cellMenu->addAction("&Show Pixel Grid"), Command::ViewPixelGridShow);
    m_cellGridShowAction->setCheckable(true);
    connect(m_cellGridShowAction, &QAction::toggled, this, [this](bool on)
    {
        if (m_view)
            m_view->glWidget()->setCellGridEnabled(on);
    });
    auto *nsideWidget = new QWidget;
    auto *nsideLayout = new QHBoxLayout(nsideWidget);
    nsideLayout->setContentsMargins(12, 2, 12, 2);
    nsideLayout->addWidget(new QLabel("Nside:"));
    m_cellGridNsideSpin = new Pow2SpinBox;
    m_cellGridNsideSpin->setRange(1, 8192);
    m_cellGridNsideSpin->setValue(4);
    m_cellGridNsideSpin->setKeyboardTracking(false); // typed: apply on Enter
    m_cellGridNsideSpin->setToolTip("Rounded to a power of two (1 - 8192)");
    nsideLayout->addWidget(m_cellGridNsideSpin);
    auto *nsideAction = new QWidgetAction(this);
    nsideAction->setDefaultWidget(nsideWidget);
    cellMenu->addAction(nsideAction);
    connect(m_cellGridNsideSpin, &QSpinBox::valueChanged, this, [this](int n)
    {
        if (!m_view)
            return;
        auto *gl = m_view->glWidget();
        gl->setCellGridNside(n);
        gl->setCellGridEnabled(true);
        m_cellGridShowAction->setChecked(true);
        // Show the nside actually used (a power of two)
        const QSignalBlocker block(m_cellGridNsideSpin);
        m_cellGridNsideSpin->setValue(gl->cellGridNside());
    });
    m_cellLabelsAction = tag(cellMenu->addAction("Show Pixel &Numbers"), Command::ViewPixelGridNumbers);
    m_cellLabelsAction->setCheckable(true);
    connect(m_cellLabelsAction, &QAction::toggled, this, [this](bool on)
    {
        if (!m_view)
            return;
        m_view->glWidget()->setCellLabelsEnabled(on);
        // The numbers are those of the grid's pixels
        if (on && !m_view->glWidget()->cellGridEnabled())
            m_cellGridShowAction->setChecked(true);
    });
    addColorRow(cellMenu, m_cellGridColorButton, true,
                [this]() { return m_view->glWidget()->cellGridColor(); },
                [this](const QColor &c)
    {
        if (m_view)
            m_view->glWidget()->setCellGridColor(c);
    }, "Select Pixel Grid Color");

    m_viewMenu->addSeparator();

    // Everything about the region the Statistics dock measures
    QMenu *regionMenu = m_viewMenu->addMenu("&Statistics Region");
    const std::tuple<const char *, OpenGLWidget::RegionShape, Command> shapes[]
        = {{"&Rectangle", OpenGLWidget::RegionShape::Rectangle,
            Command::ViewRegionRectangle},
           {"&Ellipse", OpenGLWidget::RegionShape::Ellipse,
            Command::ViewRegionEllipse},
           {"&Polygon (double click or Enter to close)",
            OpenGLWidget::RegionShape::Polygon, Command::ViewRegionPolygon},
           {"&Line (great circle)", OpenGLWidget::RegionShape::Line,
            Command::ViewRegionLine}};
    for (const auto &[name, shape, command] : shapes)
        tag(regionMenu->addAction(name, this, [this, shape = shape]()
        {
            if (m_view)
                m_view->glWidget()->beginStatsRegion(shape);
        }), command);
    regionMenu->addSeparator();
    tag(regionMenu->addAction(themeIcon("edit-clear"), "&Clear Region", this,
                              [this]()
    {
        if (m_view)
            m_view->glWidget()->clearStatsRegion();
    }), Command::ViewRegionClear);

    // Colormap: the colors of the map and of its color bar
    m_colormapMenu = menuBar->addMenu("&Colormap");
    QMenu *customMenu = m_colormapMenu->addMenu("Custom Colormaps");
    tag(customMenu->addAction(themeIcon("document-open", QStyle::SP_DialogOpenButton),
                          "&Load Colormap...", this,
                          &HpVis::onLoadColormapTriggered), Command::ColormapLoad);
    tag(customMenu->addAction(themeIcon("folder-open", QStyle::SP_DirOpenIcon),
                          "&Open Colormaps Folder", this, []()
    {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(ColormapRegistry::instance().directory()));
    }), Command::ColormapOpenFolder);

    m_reverseColormapAction = tag(m_colormapMenu->addAction(
        "Re&verse Colormap", this, &HpVis::onColormapInvertChanged), Command::ColormapReverse);
    m_reverseColormapAction->setCheckable(true);
    m_reverseColormapAction->setIcon(themeIcon("object-flip-horizontal"));

    m_colormapMenu->addSeparator();
    QMenu *colorBarMenu = m_colormapMenu->addMenu("&Color Bar");
    m_viewColorBarAction
        = tag(colorBarMenu->addAction("&Show Color Bar", this, [this](bool state)
    {
        if (m_view)
            m_view->glWidget()->setColorBarVisible(state);
    }), Command::ColorBarShow);
    m_viewColorBarAction->setCheckable(true);
    m_viewColorBarAction->setChecked(true);
    colorBarMenu->addSeparator();
    auto *scaleGroup    = new QActionGroup(this);
    m_scaleLinearAction = tag(colorBarMenu->addAction("&Linear"), Command::ColorBarLinear);
    m_scaleLogAction    = tag(colorBarMenu->addAction("L&ogarithmic"), Command::ColorBarLog);
    m_scaleAsinhAction  = tag(colorBarMenu->addAction("&Asinh"), Command::ColorBarAsinh);
    for (QAction *a :
         {m_scaleLinearAction, m_scaleLogAction, m_scaleAsinhAction})
    {
        a->setCheckable(true);
        scaleGroup->addAction(a);
    }
    m_scaleLinearAction->setChecked(true);
    connect(m_scaleLinearAction, &QAction::triggered, this,
            [this]() { onColormapScaleChanged(ColormapScale::LINEAR); });
    connect(m_scaleLogAction, &QAction::triggered, this,
            [this]() { onColormapScaleChanged(ColormapScale::LOG); });
    connect(m_scaleAsinhAction, &QAction::triggered, this,
            [this]() { onColormapScaleChanged(ColormapScale::ASINH); });

    // Softening of the asinh scale: 10^-3 (log-like) .. 1 (nearly linear)
    auto *softWidget = new QWidget();
    auto *softLayout = new QHBoxLayout(softWidget);
    softLayout->setContentsMargins(12, 2, 12, 2);
    softLayout->addWidget(new QLabel("Asinh softening:"));
    m_asinhSlider = new QSlider(Qt::Horizontal);
    m_asinhSlider->setRange(0, 100);
    m_asinhSlider->setValue(67); // ~0.1
    m_asinhSlider->setMinimumWidth(140);
    m_asinhSlider->setToolTip("Left: more logarithmic. Right: more linear.");
    softLayout->addWidget(m_asinhSlider);
    auto *softAction = new QWidgetAction(this);
    softAction->setDefaultWidget(softWidget);
    // Only the asinh scale has a softening
    softWidget->setEnabled(false);
    connect(m_scaleAsinhAction, &QAction::toggled, softWidget,
            &QWidget::setEnabled);
    colorBarMenu->addSeparator();
    colorBarMenu->addAction(softAction);
    connect(m_asinhSlider, &QSlider::valueChanged, this, [this](int v)
    {
        if (m_view)
            m_view->glWidget()->setAsinhSoftening(
                std::pow(10.0, -3.0 + 3.0 * v / 100.0));
    });

    colorBarMenu->addSeparator();
    tag(colorBarMenu->addAction("&Range...", this,
                            &HpVis::onColormapRangeActionTriggered), Command::ColorBarRange);
    tag(colorBarMenu->addAction(themeIcon("edit-undo"), "Re&set Range", this,
                            &HpVis::onColormapRangeResetRequested), Command::ColorBarResetRange);
    tag(colorBarMenu->addAction("&Percentile Range...", this,
                            &HpVis::onColormapPercentileRequested), Command::ColorBarPercentile);

    m_masksMenu = menuBar->addMenu("&Masks");
    rebuildMasksMenu(nullptr);

    m_toolsMenu = menuBar->addMenu("&Tools");

    QMenu *mapsMenu = m_toolsMenu->addMenu("&Maps");

    tag(mapsMenu->addAction("&Operate on Maps...", this,
                           &HpVis::onOperateTriggered), Command::ToolsOperate);
    tag(mapsMenu->addAction("Change &Resolution...", this,
                           &HpVis::onDowngradeTriggered), Command::ToolsResolution);
    tag(mapsMenu->addAction("&Beam Smoothing...", this,
                           &HpVis::onSmoothTriggered), Command::ToolsSmooth);
    tag(mapsMenu->addAction("&Multipole Filter...", this,
                           &HpVis::onMultipoleFilterTriggered),
        Command::ToolsMultipoleFilter);
    tag(mapsMenu->addAction("&Derivatives and E/B Maps...", this,
                           &HpVis::onHarmonicTriggered), Command::ToolsHarmonic);
    mapsMenu->addSeparator();
    tag(mapsMenu->addAction("Convert &Units...", this,
                            &HpVis::onUnitsTriggered), Command::ToolsUnits);
    tag(mapsMenu->addAction("&Data Transform...", this,
                            &HpVis::onTransformTriggered),
        Command::ToolsTransform);
    tag(mapsMenu->addAction("&Generate Random Map...", this,
                            &HpVis::onRandomMapTriggered),
        Command::ToolsRandomMap);

    m_toolsAnalysisMenu = m_toolsMenu->addMenu("&Analysis");

    // The docks provide checkable actions that stay in sync with their
    // visibility (including their own close buttons)
    m_powerDock->toggleViewAction()->setIcon(themeIcon("office-chart-line"));
    m_statsDock->toggleViewAction()->setIcon(themeIcon("office-chart-bar"));
    m_toolsAnalysisMenu->addAction(
        tag(m_powerDock->toggleViewAction(), Command::ToolsPowerSpectrum));

    m_toolsAnalysisMenu->addAction(
        tag(m_statsDock->toggleViewAction(), Command::ToolsStatistics));

    m_toolsMenu->addSeparator();
    m_toolsSettingsAction = tag(m_toolsMenu->addAction(
        "&Settings...", this, &HpVis::onSettingsActionTriggered), Command::ToolsSettings);
    m_toolsSettingsAction->setIcon(themeIcon("preferences-system"));

    auto helpMenu = menuBar->addMenu("&Help");

    // The guide (docs/USAGE.md); one dialog, kept so it reopens where it was
    QAction *usage = tag(helpMenu->addAction("&Usage", this, [this]()
    {
        if (!m_usageDialog)
            m_usageDialog = new UsageDialog(this);
        m_usageDialog->show();
        m_usageDialog->raise();
        m_usageDialog->activateWindow();
    }), Command::HelpUsage);
    usage->setShortcut(QKeySequence("F1"));
    usage->setIcon(themeIcon("help-contents", QStyle::SP_DialogHelpButton));
    helpMenu->addSeparator();
    QAction *about = tag(helpMenu->addAction("&About", this, [this]()
    {
        // The same content as the About page of the settings
        QDialog dialog(this);
        dialog.setWindowTitle("About hpvis");
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(0, 0, 0, 8);
        layout->addWidget(new AboutContent());

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        buttons->setContentsMargins(12, 0, 12, 0);
        layout->addWidget(buttons);
        dialog.exec();
    }), Command::HelpAbout);
    about->setIcon(
        themeIcon("help-about", QStyle::SP_MessageBoxInformation));
}

// Rebuilds File > Open Recent from the saved list
void
HpVis::updateRecentMenu()
{
    m_fileOpenRecentMenu->clear();

    const QStringList files = m_recentFiles.files();
    if (files.isEmpty())
    {
        m_fileOpenRecentMenu->addAction("No recent files")->setEnabled(false);
        return;
    }

    int n = 1;
    for (const QString &path : files)
    {
        QAction *action = m_fileOpenRecentMenu->addAction(
            QString("&%1  %2").arg(n++).arg(QFileInfo(path).fileName()), this,
            [this, path]() { OpenFile(path); });
        action->setToolTip(path);
        action->setStatusTip(path);
    }

    m_fileOpenRecentMenu->addSeparator();
    m_fileOpenRecentMenu->addAction(themeIcon("edit-clear-history",
                                              QStyle::SP_DialogResetButton),
                                    "&Clear Recent Files", this, [this]()
    {
        m_recentFiles.clear();
        updateRecentMenu();
    });
}

// --- Session: the files that were open when the program was closed ---

// Called when the window closes
void
HpVis::saveSession() const
{
    QSettings s;
    s.remove("session");
    int n       = 0;
    int current = -1;
    s.beginWriteArray("session/tabs");
    for (int i = 0; i < m_tabWidget->count(); ++i)
    {
        const auto *v = qobject_cast<View *>(m_tabWidget->widget(i));
        // A computed map has no file to open again
        if (!v || v->isComputed() || v->filePath().isEmpty())
            continue;
        s.setArrayIndex(n);
        s.setValue("path", v->filePath());
        s.setValue("hdu", v->hdu());
        s.setValue("column", v->column());
        if (v == m_view)
            current = n;
        ++n;
    }
    s.endArray();
    s.setValue("session/current", current);
}

int
HpVis::storedSessionSize() const
{
    return QSettings().beginReadArray("session/tabs");
}

void
HpVis::restoreSession()
{
    if (!m_sessionQueue.isEmpty() && m_sessionPos < m_sessionQueue.size())
        return; // already restoring

    QSettings s;
    const int n = s.beginReadArray("session/tabs");
    m_sessionQueue.clear();
    m_sessionMissing = 0;
    for (int i = 0; i < n; ++i)
    {
        s.setArrayIndex(i);
        SessionEntry e;
        e.path   = s.value("path").toString();
        e.hdu    = s.value("hdu").toInt();
        e.column = s.value("column").toInt();
        if (e.path.isEmpty())
            continue;
        if (!QFileInfo::exists(e.path))
        {
            ++m_sessionMissing;
            continue;
        }
        m_sessionQueue << e;
    }
    s.endArray();

    // Position of the current tab among the files that still exist
    m_sessionCurrent = -1;
    const int saved  = s.value("session/current", -1).toInt();
    if (saved >= 0 && saved < n)
    {
        s.beginReadArray("session/tabs");
        s.setArrayIndex(saved);
        const QString path = s.value("path").toString();
        s.endArray();
        for (int i = 0; i < m_sessionQueue.size(); ++i)
            if (m_sessionQueue[i].path == path)
                m_sessionCurrent = i;
    }

    m_sessionPos = 0;
    m_sessionCurrentView.clear();
    if (m_sessionQueue.isEmpty() && m_sessionMissing > 0)
        m_statusBar->showMessage(
            QString("%1 file(s) from the last session were not found")
                .arg(m_sessionMissing),
            5000);
    restoreNextSessionEntry();
}

// One at a time, so that the tabs come back in their order
void
HpVis::restoreNextSessionEntry()
{
    if (m_sessionPos >= m_sessionQueue.size())
    {
        m_sessionQueue.clear();
        if (m_sessionCurrentView)
            m_tabWidget->setCurrentIndex(
                m_tabWidget->indexOf(m_sessionCurrentView));
        if (m_sessionMissing > 0)
            m_statusBar->showMessage(
                QString("%1 file(s) from the last session were not found")
                    .arg(m_sessionMissing),
                5000);
        return;
    }
    const int pos            = m_sessionPos++;
    const SessionEntry entry = m_sessionQueue[pos];
    OpenFile(entry.path, entry.hdu, entry.column, QString(),
             [this, pos](View *view)
    {
        if (view && pos == m_sessionCurrent)
            m_sessionCurrentView = view;
        restoreNextSessionEntry();
    });
}

void
HpVis::OpenFile(const QString &filePath, int hdu, int column,
                const QString &columnName, std::function<void(View *)> onDone)
{
    QString resolvedPath;
    if (filePath.isEmpty())
    {
        auto files = QFileDialog::getOpenFileNames(
            this, "Open FITS File", startDirectory(),
            "Map Files (*.fits *.fit *.fts *.npy *.npz);;FITS Files (*.fits *.fit "
            "*.fts);;"
            "NumPy Files (*.npy *.npz);;All Files (*)");

        if (!files.isEmpty())
            rememberDirectory(files.first());
        for (const auto &file : files)
            OpenFile(file);

        return; // Exit the function after processing selected files
    }
    else
        resolvedPath = filePath;

    if (filePath.startsWith("~"))
        resolvedPath = QDir::homePath() + filePath.mid(1);

    const bool fromCli     = m_cliPaths.remove(filePath);
    const QString filename = QFileInfo(resolvedPath).fileName();
    m_statusBar->showMessage(QString("Loading %1...").arg(filename));

    // Parented to the window so it is cleaned up if we quit while loading
    View *view = new View(this);
    view->hide();

    connect(view, &View::loadFinished, this,
            [this, view, filename, resolvedPath, fromCli,
             onDone](bool ok, const QString &error)
    {
        if (!ok)
        {
            // e.g. a recent file that was moved or is no longer valid
            m_recentFiles.remove(resolvedPath);
            updateRecentMenu();
            QMessageBox::critical(this, "Error",
                                  QString("Failed to load map: %1").arg(error));
            m_statusBar->showMessage(QString("Failed to load %1").arg(filename),
                                     5000);
            view->deleteLater();
            if (onDone)
                onDone(nullptr);
            return;
        }

        applyDefaults(view);
        if (fromCli)
            applyCliOptions(view);
        m_tabWidget->addTab(view, filename);
        // Show it now so the GPU upload happens at open time, not on the
        // first tab switch
        m_tabWidget->setCurrentIndex(m_tabWidget->indexOf(view));
        initViewConnections(view);

        // The tab-changed signal fired inside addTab(), before this view had
        // its file info; fill the side panel and status bar now
        if (m_view == view)
        {
            updateControlsFromView(view);
            updateStatusBarFromView(view);
        }
        m_recentFiles.add(resolvedPath);
        updateRecentMenu();
        m_statusBar->showMessage(QString("Loaded %1").arg(filename), 3000);

        m_toolBar->setEnabled(true);
        if (onDone)
            onDone(view);
    });

    view->loadMap(resolvedPath, hdu, column, columnName);
}

void
HpVis::initViewConnections(const View *view)
{
    connect(view, &View::cursorPosChanged, m_statusBar,
            &StatusBar::setCursorData);

    connect(view, &View::zoomChanged, m_statusBar, &StatusBar::setZoom);

    // Result of switching HDU / column from the toolbar
    connect(view, &View::hduLoaded, this,
            [this, view](bool ok, const QString &error)
    {
        if (!ok)
            QMessageBox::critical(this, "Error",
                                  QString("Failed to load map: %1").arg(error));
        else
            m_statusBar->showMessage(QString("Loaded HDU %1, column %2")
                                         .arg(view->hdu())
                                         .arg(view->column()),
                                     3000);
        // Show what is really loaded (the previous selection after an error)
        if (view == m_view)
        {
            m_toolBar->setFitsInfo(view->fitsInfo(), view->hdu(),
                                   view->column());
            updateDocks();
        }
    });

    connect(view, &View::regionChanged, this, [this, view]()
    {
        if (view == m_view)
            m_statsDock->updateRegion();
    });

    connect(view, &View::masksChanged, this, [this, view]()
    {
        if (view == m_view)
        {
            updateMaskListFromView(view);
            updateDocks();
        }
    });
}

void
HpVis::updateControlsFromView(const View *view)
{
    if (!view || !view->glWidget() || !view->glWidget()->healpixMap())
        return;

    const OpenGLWidget *gl = view->glWidget();
    m_toolBar->setFitsInfo(view->fitsInfo(), view->hdu(), view->column());
    m_toolBar->setProjection(gl->projection());
    switch (gl->colormapScale())
    {
    case ColormapScale::LINEAR: m_scaleLinearAction->setChecked(true); break;
    case ColormapScale::LOG: m_scaleLogAction->setChecked(true); break;
    case ColormapScale::ASINH: m_scaleAsinhAction->setChecked(true); break;
    }
    {
        const QSignalBlocker block(m_asinhSlider);
        m_asinhSlider->setValue(
            int(std::lround((std::log10(gl->asinhSoftening()) + 3.0) / 3.0
                            * 100.0)));
    }
    m_graticuleShowAction->setChecked(gl->graticuleEnabled());
    {
        const QSignalBlocker block(m_cellGridShowAction);
        m_cellGridShowAction->setChecked(gl->cellGridEnabled());
    }
    {
        const QSignalBlocker block(m_cellLabelsAction);
        m_cellLabelsAction->setChecked(gl->cellLabelsEnabled());
    }
    {
        const QSignalBlocker block(m_cellGridNsideSpin);
        m_cellGridNsideSpin->setValue(gl->cellGridNside());
    }
    setSwatchColor(m_cellGridColorButton, gl->cellGridColor());
    m_reverseColormapAction->setChecked(gl->colormapInvert());
    m_toolBar->setColormap(gl->colormap());

    (gl->viewFromOutside() ? m_outsideAction : m_insideAction)
        ->setChecked(true);
    m_orientationActions[int(gl->orientation())]->setChecked(true);
    m_toolBar->setOrientation(gl->orientation());
    updateCoordinateNames();
    (gl->pixelOrdering() == PixelOrdering::NESTED ? m_nestedAction
                                                  : m_ringAction)
        ->setChecked(true);
    {
        const QSignalBlocker block(m_graticuleStepSpin);
        m_graticuleStepSpin->setValue(gl->graticuleStep());
    }
    setSwatchColor(m_graticuleColorButton, gl->graticuleColor());

    updateMaskListFromView(view);
    updateDocks();
}

// Applies an orientation from the toolbar or the menu to the current tab and
// keeps both in step
void
HpVis::setOrientation(Orientation orientation)
{
    if (m_view)
        m_view->glWidget()->setOrientation(orientation);
    m_orientationActions[int(orientation)]->setChecked(true);
    m_toolBar->setOrientation(orientation);
    updateCoordinateNames();
}

// The status bar names the coordinates of the frame being shown
void
HpVis::updateCoordinateNames()
{
    if (!m_view)
        return;
    const SkyFrame frame = m_view->glWidget()->displayFrame();
    m_statusBar->setCoordinateNames(skyframe::lonName(frame),
                                    skyframe::latName(frame));
}

// Points the dockable tools at the current tab
void
HpVis::updateDocks()
{
    m_statsDock->setView(m_view);
    m_powerDock->setView(m_view);
}

void
HpVis::onPixelOrderingChanged(PixelOrdering ordering)
{
    if (!m_view)
        return;

    m_view->glWidget()->setPixelOrdering(ordering);
}

void
HpVis::onTabChanged(int index)
{
    if (index < 0 || index >= m_tabWidget->count())
        return;

    QWidget *widget = m_tabWidget->widget(index);
    m_view          = qobject_cast<View *>(widget);

    updateControlsFromView(m_view);
    updateDocks();
    updateStatusBarFromView(m_view);
    if (m_view)
    {
        m_viewMinimapAction->setChecked(m_view->glWidget()->minimapEnabled());
        m_viewColorBarAction->setChecked(m_view->glWidget()->colorBarVisible());
        m_explorerDock->revealFile(m_view->filePath());
    }
    if (m_rotationDialog)
        m_rotationDialog->setTarget(m_view ? m_view->glWidget() : nullptr);
}

void
HpVis::updateMaskListFromView(const View *view)
{
    rebuildMasksMenu(view);
}

// Rebuilds the Masks menu: loading/clearing, mode, and one submenu per mask
void
HpVis::rebuildMasksMenu(const View *view)
{
    buildMasksMenu(view);
    // The menu's actions were just recreated
    if (m_shortcutsReady)
        applyShortcuts();
}

void
HpVis::buildMasksMenu(const View *view)
{
    m_masksMenu->clear();
    OpenGLWidget *gl = view ? view->glWidget() : nullptr;

    QAction *add = tag(m_masksMenu->addAction(themeIcon("list-add"),
                                          "&Add Mask...", this, [this]()
    {
        const auto files = QFileDialog::getOpenFileNames(
            this, "Load Mask(s)", "",
            "Mask Files (*.fits *.npy *.npz);;FITS Files (*.fits);;"
            "NumPy Files (*.npy *.npz);;All Files (*)");
        if (!files.isEmpty())
            onMaskFilesSelected(files);
    }), Command::MaskAdd);
    add->setEnabled(gl);

    QAction *addColumn = tag(m_masksMenu->addAction(
        themeIcon("list-add"), "Add Mask From &Map Column...", this, [this]()
    {
        if (!m_view)
            return;
        MaskSourceDialog dialog(openMaps(), m_view, this);
        if (dialog.exec() == QDialog::Accepted && m_view)
            m_view->glWidget()->addMasks(dialog.selected());
    }), Command::MaskAddFromColumn);
    addColumn->setEnabled(gl);

    m_masksMenu->addSeparator();
    auto addClear = [this, gl]()
    {
        QAction *clear = tag(m_masksMenu->addAction(
            themeIcon("edit-clear", QStyle::SP_DialogResetButton),
            "&Clear All", this, &HpVis::onMaskCleared), Command::MaskClear);
        clear->setEnabled(gl && gl->maskCount() > 0);
    };

    if (!gl || gl->maskCount() == 0)
    {
        addClear();
        return;
    }

    for (int i = 0; i < gl->maskCount(); ++i)
    {
        QMenu *sub = m_masksMenu->addMenu(gl->maskName(i));
        sub->menuAction()->setCheckable(true);
        sub->setToolTipsVisible(true);
        sub->setToolTip(gl->maskPath(i));

        QAction *on = sub->addAction("&Enabled");
        on->setCheckable(true);
        on->setChecked(gl->maskEnabled(i));
        connect(on, &QAction::toggled, this, [this, i](bool v)
        {
            if (m_view)
                m_view->glWidget()->setMaskEnabled(i, v);
        });

        QAction *inv = sub->addAction("&Invert");
        inv->setCheckable(true);
        inv->setChecked(gl->maskInverted(i));
        connect(inv, &QAction::toggled, this, [this, i](bool v)
        {
            if (m_view)
                m_view->glWidget()->setMaskInverted(i, v);
        });

        sub->addAction(themeIcon("list-remove"), "&Remove", this, [this, i]()
        {
            if (m_view)
                m_view->glWidget()->removeMask(i);
        });
    }

    m_masksMenu->addSeparator();
    QMenu *mode = m_masksMenu->addMenu("&Mode");
    auto *group = new QActionGroup(mode);
    for (int m = 0; m < 2; ++m)
    {
        QAction *a = mode->addAction(m == 0 ? "Hide" : "Dim");
        a->setCheckable(true);
        a->setChecked(gl->maskMode() == m);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, m]()
        {
            if (m_view)
                m_view->glWidget()->setMaskMode(m);
        });
    }

    QAction *strength
        = tag(m_masksMenu->addAction("Dim &Strength...", this, [this]()
    {
        if (!m_view)
            return;
        bool ok     = false;
        const int v = QInputDialog::getInt(
            this, "Dim Strength",
            "Strength (%):", qRound(m_view->glWidget()->maskOpacity() * 100.0f),
            0, 100, 1, &ok);
        if (ok)
            m_view->glWidget()->setMaskOpacity(v / 100.0f);
    }), Command::MaskDimStrength);
    strength->setEnabled(gl->maskMode() == 1);

    m_masksMenu->addSeparator();
    addClear();
}

void
HpVis::updateStatusBarFromView(const View *view)
{
    if (!view)
        return;

    const QFileInfo info(view->filePath());
    m_statusBar->setFilename(view->displayName());
    m_statusBar->setFilesize(view->isComputed()
                                 ? QString("computed")
                                 : QLocale().formattedDataSize(info.size()));
    m_statusBar->setZoom(view->glWidget()->zoom());
    m_statusBar->setCursorData(QPointF(), QPointF(), std::nan(""),
                               -1); // clear stale
}

void
HpVis::onTabCloseRequested(int index)
{
    if (index < 0 || index >= m_tabWidget->count())
        return;

    QWidget *widget = m_tabWidget->widget(index);
    m_powerDock->forget(qobject_cast<View *>(widget));
    m_statsDock->forget(qobject_cast<View *>(widget));
    m_tabWidget->removeTab(index);
    delete widget;

    if (m_tabWidget->count() == 0)
    {
        m_view = nullptr;
        updateDocks();
        m_toolBar->setEnabled(false);
        rebuildMasksMenu(nullptr);
        m_statusBar->clear();
    }
}

void
HpVis::onColormapChanged(Colormap colormap)
{
    if (m_view)
        m_view->glWidget()->setColormap(colormap);
}

void
HpVis::onColormapMaxChanged(double max)
{
    if (m_view)
        m_view->glWidget()->setColormapMax(max);
}

void
HpVis::onColormapMinChanged(double min)
{
    if (m_view)
        m_view->glWidget()->setColormapMin(min);
}

void
HpVis::onColormapInvertChanged(bool reversed)
{
    if (m_view)
        m_view->glWidget()->setColormapInvert(reversed);
}

void
HpVis::onColormapScaleChanged(ColormapScale scale)
{
    if (m_view)
        m_view->glWidget()->setColormapScale(scale);
}

void
HpVis::onColormapRangeResetRequested()
{
    if (m_view)
        m_view->glWidget()->resetColormapRange();
}

// Auto-range: the color range between the p-th and (100 - p)-th percentile, so
// a few extreme pixels (the Galactic plane, point sources) do not wash out
// the fluctuations
void
HpVis::onColormapPercentileRequested()
{
    if (!m_view)
        return;
    bool ok = false;
    const double p
        = QInputDialog::getDouble(this, "Percentile Range",
                                  "Clip this percent at each end:", 1.0, 0.0,
                                  49.9, 2, &ok);
    if (ok)
        m_view->glWidget()->setColormapPercentiles(p, 100.0 - p);
}

void
HpVis::onBackgroundColorChanged(const QColor &color)
{
    if (m_view)
        m_view->glWidget()->setBackgroundColor(color);
}

void
HpVis::onUnseenColorChanged(const QColor &color)
{
    if (m_view)
        m_view->glWidget()->setUnseenColor(color);
}

void
HpVis::onMaskFilesSelected(const QStringList &files)
{
    if (m_view)
        m_view->glWidget()->addMaskFiles(files);
}

void
HpVis::onMaskCleared()
{
    if (m_view)
        m_view->glWidget()->clearMasks();
}

void
HpVis::onMinimapToggled(bool state)
{
    if (m_view)
        m_view->glWidget()->setMinimapEnabled(state);
}

void
HpVis::onExportActionTriggered()
{
    if (!m_view)
        return;

    // Preselect the size in device pixels, what the export really has
    const auto *gl = m_view->glWidget();
    ExportDialog dialog(gl->size() * gl->devicePixelRatioF(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString ext = dialog.format();
    QString filePath  = QFileDialog::getSaveFileName(
        this, "Export Image", startDirectory() + "/image." + ext,
        QString("%1 Files (*.%2);;All Files (*)").arg(ext.toUpper(), ext));
    if (filePath.isEmpty())
        return;

    // Add the extension of the chosen format when none was typed
    if (QFileInfo(filePath).suffix().isEmpty())
        filePath += "." + ext;

    rememberDirectory(filePath);
    m_view->glWidget()->exportImage(filePath, dialog.options());
}

void
HpVis::onSettingsActionTriggered()
{
    SettingsDialog dialog(shortcutCommands(), this);
    connect(&dialog, &SettingsDialog::settingsSaved, this,
            &HpVis::applySavedSettings);
    dialog.exec();
}

// Picks up the settings the dialog just saved
void
HpVis::applySavedSettings()
{
    const AppSettings previous = m_settings;
    m_settings                 = AppSettings::load();
    applyShortcuts();
    m_recentFiles.setMaxCount(m_settings.recentFilesMax);
    updateRecentMenu();
    applyInterfaceSettings();
    m_powerDock->setPlotColors(m_settings.plotBackground,
                               m_settings.plotForeground, m_settings.plotLine);
    m_statsDock->setPlotColors(m_settings.plotBackground,
                               m_settings.plotForeground, m_settings.plotLine);
    m_powerDock->refreshTheory();

    // Apply what changed to the maps that are already open too. Only the
    // changed options are touched, so per-map tweaks to the others survive.
    const AppSettings &s = m_settings;
    for (int i = 0; i < m_tabWidget->count(); ++i)
    {
        auto *view = qobject_cast<View *>(m_tabWidget->widget(i));
        if (!view)
            continue;
        OpenGLWidget *gl = view->glWidget();

        if (s.colormap != previous.colormap)
            gl->setColormap(s.colormap);

        if (s.reverseColormap != previous.reverseColormap)
            gl->setColormapInvert(s.reverseColormap);

        if (s.scale != previous.scale)
            gl->setColormapScale(s.scale);

        if (s.background != previous.background)
            gl->setBackgroundColor(s.background);

        if (s.unseen != previous.unseen)
            gl->setUnseenColor(s.unseen);

        if (s.streamingNside != previous.streamingNside)
            gl->setStreamingNside(s.streamingNside);

        if (s.colorBar != previous.colorBar)
            gl->setColorBarVisible(s.colorBar);

        if (s.minimap != previous.minimap)
            gl->setMinimapEnabled(s.minimap);

        if (s.graticule != previous.graticule)
            gl->setGraticuleEnabled(s.graticule);

        if (s.graticuleStep != previous.graticuleStep)
            gl->setGraticuleStep(s.graticuleStep);

        if (s.globeShadow != previous.globeShadow)
            gl->setGlobeShadow(s.globeShadow);

        if (s.limbDarkening != previous.limbDarkening)
            gl->setLimbDarkening(s.limbDarkening);

        if (s.animateOrthoGlobe != previous.animateOrthoGlobe)
            gl->setAnimateGlobe(s.animateOrthoGlobe);

        if (s.animateProjection != previous.animateProjection)
            gl->setAnimateProjection(s.animateProjection);

        if (s.orientation != previous.orientation)
            gl->setOrientation(s.orientation);

        if (s.viewFromOutside != previous.viewFromOutside)
            gl->setViewFromOutside(s.viewFromOutside);

        if (s.projection != previous.projection)
            gl->setProjection(s.projection);

        if (s.graticuleColor != previous.graticuleColor)
            gl->setGraticuleColor(s.graticuleColor);

        if (s.maskMode != previous.maskMode)
            gl->setMaskMode(s.maskMode);

        if (s.maskOpacityPercent != previous.maskOpacityPercent)
            gl->setMaskOpacity(s.maskOpacityPercent / 100.0f);
    }

    // Refresh the toolbar and menu check marks for the current tab
    if (m_view)
        onTabChanged(m_tabWidget->currentIndex());
}

void
HpVis::applyInterfaceSettings()
{
    m_toolBar->setVisible(m_settings.showToolbar);
    statusBar()->setVisible(m_settings.showStatusBar);
}

// Folder the file dialogs start in
QString
HpVis::startDirectory() const
{
    if (m_settings.rememberLastDir)
    {
        const QString dir
            = QSettings().value("general/lastDirectory").toString();
        if (!dir.isEmpty() && QDir(dir).exists())
            return dir;
    }
    return QDir::homePath();
}

void
HpVis::rememberDirectory(const QString &path) const
{
    if (m_settings.rememberLastDir && !path.isEmpty())
        QSettings().setValue("general/lastDirectory",
                             QFileInfo(path).absolutePath());
}

// Gives a newly loaded map the user's default view options
void
HpVis::applyDefaults(View *view)
{
    OpenGLWidget *gl = view->glWidget();
    gl->setRegionKeys(m_regionKeys.first, m_regionKeys.second);
    gl->setColormap(m_settings.colormap);
    gl->setColormapInvert(m_settings.reverseColormap);
    gl->setColormapScale(m_settings.scale);
    gl->setBackgroundColor(m_settings.background);
    gl->setUnseenColor(m_settings.unseen);
    gl->setStreamingNside(m_settings.streamingNside);
    gl->setColorBarVisible(m_settings.colorBar);
    gl->setMinimapEnabled(m_settings.minimap);
    gl->setGraticuleEnabled(m_settings.graticule);
    gl->setGraticuleStep(m_settings.graticuleStep);
    gl->setGlobeShadow(m_settings.globeShadow);
    gl->setLimbDarkening(m_settings.limbDarkening);
    gl->setProjection(m_settings.projection);
    gl->setViewFromOutside(m_settings.viewFromOutside);
    gl->setOrientation(m_settings.orientation);
    gl->setAnimateGlobe(m_settings.animateOrthoGlobe);
    gl->setAnimateProjection(m_settings.animateProjection);
    gl->setRecenterOnRightClick(m_recenterOnRightClick);
    gl->setLockRollOnRecenter(m_lockRollOnRecenter);
    gl->setGraticuleColor(m_settings.graticuleColor);
    gl->setMaskMode(m_settings.maskMode);
    gl->setMaskOpacity(m_settings.maskOpacityPercent / 100.0f);
}

void
HpVis::onGraticuleToggled(bool enabled)
{
    if (m_view)
        m_view->glWidget()->setGraticuleEnabled(enabled);
}

// Files from a file manager or from the file explorer dock
void
HpVis::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    for (const QUrl &url : event->mimeData()->urls())
        if (url.isLocalFile())
        {
            event->acceptProposedAction();
            return;
        }
}

// Files open in new tabs; a folder becomes the root of the file explorer
void
HpVis::dropEvent(QDropEvent *event)
{
    bool any = false;
    for (const QUrl &url : event->mimeData()->urls())
    {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (info.isDir())
        {
            m_explorerDock->setRootPath(path);
            m_explorerDock->show();
        }
        else if (info.isFile())
        {
            rememberDirectory(path);
            OpenFile(path);
        }
        else
            continue;
        any = true;
    }
    if (any)
        event->acceptProposedAction();
}

void
HpVis::initShortcuts()
{
    m_fileCloseTabAction->setShortcut(QKeySequence("Ctrl+W"));
    m_fileOpenRecentMenu->menuAction()->setShortcut(
        QKeySequence("Ctrl+Shift+O"));
    m_fileOpenAction->setShortcut(QKeySequence("Ctrl+O"));
    m_viewMinimapAction->setShortcut(QKeySequence("M"));
    m_viewColorBarAction->setShortcut(QKeySequence("C"));
    m_viewResetViewAction->setShortcut(QKeySequence("0"));

    // Those are the defaults; the user's own keys go on top
    m_shortcutsReady = true;
    applyShortcuts();
}

// The stored name of a command, as the settings dialog and file know it
static QString
commandId(Command c)
{
    const std::string_view name = storedName(c);
    return QString::fromUtf8(name.data(), qsizetype(name.size()));
}

// The menu items that are commands (tagged with tag()); `path` is only used
// for the label shown in the settings
void
HpVis::collectShortcutTargets(QMenu *menu, const QStringList &path,
                              QList<ShortcutTarget> &out)
{
    for (QAction *a : menu->actions())
    {
        if (a->isSeparator())
            continue;
        QString text = a->text();
        text.remove('&');
        if (text.isEmpty())
            continue;

        const Command cmd = Command(a->property("hpvisCommand").toInt());
        if (cmd == Command::None)
        {
            // Not a command itself; its entries may be (a menu), or it is
            // not bindable (the recent files, one entry per mask, ...)
            if (QMenu *sub = a->menu())
                collectShortcutTargets(sub, path + QStringList{text}, out);
            continue;
        }

        // The built-in key is whatever the action has the first time it is
        // seen, before any of the user's keys are applied
        if (!a->property("hpvisDefaultKey").isValid())
            a->setProperty("hpvisDefaultKey",
                           a->shortcut().toString(QKeySequence::PortableText));

        ShortcutTarget t;
        t.action              = a;
        t.cmd                 = cmd;
        t.command.id          = commandId(cmd);
        t.command.label       = (path.mid(1) + QStringList{text}).join(" > ");
        t.command.category    = path.value(0);
        t.command.defaultKey  = QKeySequence(
            a->property("hpvisDefaultKey").toString(),
            QKeySequence::PortableText);
        out.append(t);
    }
}

QList<HpVis::ShortcutTarget>
HpVis::shortcutTargets()
{
    QList<ShortcutTarget> out;
    for (QAction *top : menuBar()->actions())
    {
        QString title = top->text();
        title.remove('&');
        if (top->menu())
            collectShortcutTargets(top->menu(), {title}, out);
    }

    // Keys read by the viewer rather than being menu actions
    const std::pair<Command, const char *> region[]
        = {{Command::RegionFinish, "Finish polygon region"},
           {Command::RegionCancel, "Cancel region selection"}};
    for (const auto &[cmd, label] : region)
    {
        ShortcutTarget t;
        t.cmd              = cmd;
        t.command.id       = commandId(cmd);
        t.command.label    = label;
        t.command.category = "Region selection";
        t.command.defaultKey
            = QKeySequence(cmd == Command::RegionFinish ? Qt::Key_Return
                                                        : Qt::Key_Escape);
        out.append(t);
    }
    return out;
}

QList<ShortcutCommand>
HpVis::shortcutCommands()
{
    QList<ShortcutCommand> out;
    for (const ShortcutTarget &t : shortcutTargets())
        out.append(t.command);
    return out;
}

QKeySequence
HpVis::effectiveKey(const ShortcutCommand &command) const
{
    const auto it = m_settings.shortcuts.constFind(command.id);
    if (it == m_settings.shortcuts.cend())
        return command.defaultKey;
    return QKeySequence(it.value(), QKeySequence::PortableText);
}

// Puts the built-in keys, with the user's changes on top, on the actions and
// on the viewers
void
HpVis::applyShortcuts()
{
    QKeySequence finish(Qt::Key_Return), cancel(Qt::Key_Escape);
    for (const ShortcutTarget &t : shortcutTargets())
    {
        const QKeySequence key = effectiveKey(t.command);
        if (t.action)
            t.action->setShortcut(key);
        else if (t.cmd == Command::RegionFinish)
            finish = key;
        else if (t.cmd == Command::RegionCancel)
            cancel = key;
    }
    m_regionKeys = {finish, cancel};
    for (int i = 0; i < m_tabWidget->count(); ++i)
        if (auto *view = qobject_cast<View *>(m_tabWidget->widget(i)))
            view->glWidget()->setRegionKeys(finish, cancel);
}

void
HpVis::onRotationActionTriggered()
{
    if (!m_rotationDialog)
        m_rotationDialog = new RotationDialog(this);
    m_rotationDialog->setTarget(m_view ? m_view->glWidget() : nullptr);
    m_rotationDialog->show();
    m_rotationDialog->raise();
    m_rotationDialog->activateWindow();
}

void
HpVis::onColormapRangeActionTriggered()
{
    if (!m_view)
        return;
    OpenGLWidget *gl = m_view->glWidget();

    QDialog dialog(this);
    dialog.setWindowTitle("Color Bar Range");
    auto *layout = new QFormLayout(&dialog);

    auto makeSpin = [](double value)
    {
        auto *spin = new QDoubleSpinBox();
        spin->setRange(-1e9, 1e9);
        spin->setDecimals(6);
        spin->setValue(value);
        return spin;
    };
    auto *minSpin = makeSpin(gl->colormapMin());
    auto *maxSpin = makeSpin(gl->colormapMax());
    layout->addRow("Min:", minSpin);
    layout->addRow("Max:", maxSpin);

    // Applied live
    connect(minSpin, &QDoubleSpinBox::valueChanged, &dialog,
            [gl](double v) { gl->setColormapMin(v); });
    connect(maxSpin, &QDoubleSpinBox::valueChanged, &dialog,
            [gl](double v) { gl->setColormapMax(v); });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *reset   = buttons->addButton("Reset", QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, &dialog, [=]()
    {
        gl->resetColormapRange();
        QSignalBlocker b1(minSpin), b2(maxSpin);
        minSpin->setValue(gl->colormapMin());
        maxSpin->setValue(gl->colormapMax());
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    dialog.exec();
}

// Picks CSV / JSON colormap files, adds them to the colormaps folder and
// selects the last one in the current view
void
HpVis::onLoadColormapTriggered()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Load Colormap", startDirectory(),
        "Colormaps (*.csv *.txt *.json);;All Files (*)");
    if (files.isEmpty())
        return;
    rememberDirectory(files.first());

    int last = -1;
    for (const QString &file : files)
    {
        QString error;
        const int id = ColormapRegistry::instance().addFile(file, true, &error);
        if (id < 0)
            QMessageBox::warning(
                this, "Load Colormap",
                QString("%1:\n%2").arg(QFileInfo(file).fileName(), error));
        else
            last = id;
    }

    if (last >= 0 && m_view)
    {
        m_view->glWidget()->setColormap(Colormap(last));
        m_toolBar->setColormap(Colormap(last));
    }
}

void
HpVis::closeEvent(QCloseEvent *event)
{
    // Remembers which tools are docked, floating or hidden
    QSettings().setValue("window/state", saveState());
    saveSession();
    QMainWindow::closeEvent(event);
}

// The maps in the tabs, for the tools that combine or compare them
QList<OpenMap>
HpVis::openMaps() const
{
    QList<OpenMap> maps;
    for (int i = 0; i < m_tabWidget->count(); ++i)
        if (auto *v = qobject_cast<View *>(m_tabWidget->widget(i)))
            maps.append({m_tabWidget->tabText(i), v});
    return maps;
}

// The Operate dialog: kept so it can stay open while the results are examined
void
HpVis::onOperateTriggered()
{
    if (!m_operateDialog)
    {
        m_operateDialog
            = new MapOperationDialog([this]() { return openMaps(); }, this);
        connect(m_operateDialog, &MapOperationDialog::mapReady, this,
                &HpVis::openComputedMap);
    }
    m_operateDialog->show();
    m_operateDialog->raise();
    m_operateDialog->activateWindow();
}

// The Change Resolution dialog: works on the current tab
void
HpVis::onDowngradeTriggered()
{
    if (!m_downgradeDialog)
    {
        m_downgradeDialog
            = new DowngradeDialog([this]() { return m_view; }, this);
        connect(m_downgradeDialog, &DowngradeDialog::mapReady, this,
                &HpVis::openComputedMap);
    }
    m_downgradeDialog->refresh();
    m_downgradeDialog->show();
    m_downgradeDialog->raise();
    m_downgradeDialog->activateWindow();
}

// The Beam Smoothing dialog: works on the current tab
void
HpVis::onSmoothTriggered()
{
    if (!m_smoothDialog)
    {
        m_smoothDialog = new SmoothDialog([this]() { return m_view; }, this);
        connect(m_smoothDialog, &SmoothDialog::mapReady, this,
                &HpVis::openComputedMap);
    }
    m_smoothDialog->refresh();
    m_smoothDialog->show();
    m_smoothDialog->raise();
    m_smoothDialog->activateWindow();
}

// The Multipole Filter dialog: works on the current tab
void
HpVis::onMultipoleFilterTriggered()
{
    if (!m_multipoleDialog)
    {
        m_multipoleDialog
            = new MultipoleFilterDialog([this]() { return m_view; }, this);
        connect(m_multipoleDialog, &MultipoleFilterDialog::mapReady, this,
                &HpVis::openComputedMap);
    }
    m_multipoleDialog->refresh();
    m_multipoleDialog->show();
    m_multipoleDialog->raise();
    m_multipoleDialog->activateWindow();
}

// Opens a map that was computed from other maps as a new tab
// The Derivatives and E/B Maps dialog: works on the current tab
void
HpVis::onHarmonicTriggered()
{
    if (!m_harmonicDialog)
    {
        m_harmonicDialog
            = new HarmonicDialog([this]() { return m_view; }, this);
        connect(m_harmonicDialog, &HarmonicDialog::mapReady, this,
                &HpVis::openComputedMap);
    }
    m_harmonicDialog->refresh();
    m_harmonicDialog->show();
    m_harmonicDialog->raise();
    m_harmonicDialog->activateWindow();
}

// The Data Transform dialog: works on the current tab
void
HpVis::onTransformTriggered()
{
    if (!m_transformDialog)
    {
        m_transformDialog
            = new TransformDialog([this]() { return m_view; }, this);
        connect(m_transformDialog, &TransformDialog::mapReady, this,
                [this](std::shared_ptr<Healpix_Map<float>> map,
                       const QString &title, SkyFrame frame)
        { openComputedMap(map, title, frame); });
    }
    m_transformDialog->refresh();
    m_transformDialog->show();
    m_transformDialog->raise();
    m_transformDialog->activateWindow();
}

// The Convert Units dialog: works on the current tab
void
HpVis::onUnitsTriggered()
{
    if (!m_unitsDialog)
    {
        m_unitsDialog = new UnitsDialog([this]() { return m_view; }, this);
        connect(m_unitsDialog, &UnitsDialog::mapReady, this,
                [this](std::shared_ptr<Healpix_Map<float>> map,
                       const QString &title, SkyFrame frame, const QString &unit)
        {
            openComputedMap(map, title, frame);
            if (m_view && m_view->isComputed())
                m_view->setUnit(unit);
        });
    }
    m_unitsDialog->refresh();
    m_unitsDialog->show();
    m_unitsDialog->raise();
    m_unitsDialog->activateWindow();
}

// The Generate Random Map dialog
void
HpVis::onRandomMapTriggered()
{
    if (!m_randomDialog)
    {
        m_randomDialog = new RandomMapDialog(this);
        connect(m_randomDialog, &RandomMapDialog::mapReady, this,
                [this](std::shared_ptr<Healpix_Map<float>> map,
                       const QString &title, SkyFrame frame, const QString &unit)
        {
            openComputedMap(map, title, frame);
            if (m_view && m_view->isComputed())
                m_view->setUnit(unit);
        });
    }
    m_randomDialog->show();
    m_randomDialog->raise();
    m_randomDialog->activateWindow();
}

void
HpVis::openComputedMap(std::shared_ptr<Healpix_Map<float>> map,
                       const QString &title, SkyFrame frame)
{
    View *view = new View(this);
    view->hide();
    view->setComputedMap(std::move(*map), title, frame);

    applyDefaults(view);
    m_tabWidget->addTab(view, title);
    m_tabWidget->setCurrentIndex(m_tabWidget->indexOf(view));
    initViewConnections(view);
    if (m_view == view)
    {
        updateControlsFromView(view);
        updateStatusBarFromView(view);
    }
    m_toolBar->setEnabled(true);
    m_statusBar->showMessage(QString("Opened %1").arg(title), 3000);
}

void
HpVis::onSaveMapTriggered()
{
    if (!m_view || !m_view->healpixMap())
    {
        QMessageBox::information(this, "Save Map", "Open a map first.");
        return;
    }

    QString suggestion
        = m_view->isComputed()
              ? QString("result.fits")
              : QFileInfo(m_view->filePath()).completeBaseName() + "_copy.fits";
    QFileDialog dialog(this, "Save Map As", startDirectory() + "/" + suggestion,
                       "FITS Files (*.fits);;All Files (*)");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix("fits"); // added when the name has no extension
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    QString path = dialog.selectedFiles().first();

    // Some native dialogs ignore the default suffix: add it here, and ask
    // about overwriting since the dialog did not see the final name
    if (QFileInfo(path).suffix().isEmpty())
    {
        path += ".fits";
        if (QFileInfo::exists(path)
            && QMessageBox::question(
                   this, "Save Map",
                   QString("%1 already exists. Overwrite it?")
                       .arg(QFileInfo(path).fileName()))
                   != QMessageBox::Yes)
            return;
    }
    rememberDirectory(path);

    QString error;
    if (m_view->saveMap(path, &error))
        m_statusBar->showMessage(QString("Saved %1").arg(path), 4000);
    else
        QMessageBox::critical(
            this, "Save Map",
            QString("Failed to save %1: %2").arg(path, error));
}
