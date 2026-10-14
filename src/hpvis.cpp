#include "hpvis.hpp"

#include "colormapregistry.hpp"
#include "downgradedialog.hpp"
#include "exportdialog.hpp"
#include "smoothdialog.hpp"
#include "fitsheaderdialog.hpp"
#include "masksourcedialog.hpp"
#include "mapoperationdialog.hpp"
#include "pixelordering.hpp"
#include "powerspectrumdock.hpp"
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

HpVis::HpVis(const argparse::ArgumentParser &parser, QWidget *parent)
    : QMainWindow(parent)
{
    m_recentFiles.setMaxCount(m_settings.recentFilesMax);
    initUI();
    parseArgs(parser);
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

    if (auto scale = parser.present<std::string>("--scale"))
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
               "hammer"};
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

    m_cli.min       = parser.present<double>("--min");
    m_cli.max       = parser.present<double>("--max");
    m_cli.reverse   = parser.get<bool>("--reverse");
    m_cli.graticule = parser.get<bool>("--graticule");
    m_cli.pixelGrid = parser.present<int>("--pixel-grid");

    if (parser.is_used("--mask"))
        for (const auto &m : parser.get<std::vector<std::string>>("--mask"))
            m_cli.masks << QString::fromStdString(m);

    if (auto out = parser.present<std::string>("--export"))
        m_cli.exportPath = QString::fromStdString(*out);

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

    for (const auto &file : parser.get<std::vector<std::string>>("files"))
    {
        const QString path = QString::fromStdString(file);
        m_cliPaths.insert(path);
        OpenFile(path, hdu, column, columnName);
    }
}

// Applies the command-line options on top of the defaults, then exports and
// quits when --export was given
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

    if (!m_cli.exportPath.isEmpty())
    {
        const QString out = m_cli.exportPath;
        m_cli.exportPath.clear(); // only the first file
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
    m_statsDock = new StatisticsDock(this);
    m_statsDock->setOpenMapsProvider([this]() { return openMaps(); });
    m_powerDock = new PowerSpectrumDock(this);
    m_powerDock->setOpenMapsProvider([this]() { return openMaps(); });
    m_statusBar = new StatusBar();
    m_toolBar->setEnabled(false);
    initMenubar();
    layout->addWidget(m_tabWidget);
    setCentralWidget(widget);

    addToolBar(Qt::TopToolBarArea, m_toolBar);
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
        = m_fileMenu->addAction("&Open", this, [this]() { OpenFile(); });

    m_fileOpenRecentMenu = m_fileMenu->addMenu("&Open Recent");
    updateRecentMenu();

    m_fileExportAction = m_fileMenu->addAction("&Export", this,
                                               &HpVis::onExportActionTriggered);

    m_headerAction = m_fileMenu->addAction("FITS &Header...", this, [this]()
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
        if (m_view->filePath().endsWith(".npy", Qt::CaseInsensitive))
        {
            QMessageBox::information(this, "FITS Header",
                                     ".npy files have no header.");
            return;
        }
        FitsHeaderDialog dialog(m_view->filePath(), m_view->hdu(), this);
        dialog.exec();
    });

    m_fileSaveMapAction = m_fileMenu->addAction("&Save Map As...", this,
                                                &HpVis::onSaveMapTriggered);

    m_fileCloseTabAction = m_fileMenu->addAction("&Close Tab", this, [this]()
    {
        int index = m_tabWidget->currentIndex();
        if (index >= 0)
            onTabCloseRequested(index);
    });

    m_fileMenu->addAction("&Exit", this, &HpVis::close);

    m_editMenu = menuBar->addMenu("&Edit");
    m_editMenu->addAction("&Copy Image", QKeySequence("Ctrl+Shift+C"), this,
                          [this]()
    {
        if (!m_view)
            return;
        QGuiApplication::clipboard()->setImage(
            m_view->glWidget()->grabFramebuffer());
        statusBar()->showMessage("Image copied to clipboard", 3000);
    });
    m_viewMenu = menuBar->addMenu("&View");

    m_viewMinimapAction
        = m_viewMenu->addAction("&Minimap", this, &HpVis::onMinimapToggled);
    m_viewMinimapAction->setCheckable(true);

    m_viewColorBarAction
        = m_viewMenu->addAction("&Color Bar", this, [this](bool state)
    {
        if (m_view)
            m_view->glWidget()->setColorBarVisible(state);
    });
    m_viewColorBarAction->setCheckable(true);
    m_viewColorBarAction->setChecked(true);

    m_viewMenu->addSeparator();

    // Orientation (radio items: Free / Galactic / Ecliptic / Equatorial)
    m_orientationMenu      = m_viewMenu->addMenu("&Orientation");
    auto *orientationGroup = new QActionGroup(this);
    const QStringList orientationNames
        = {"&Free", "&Galactic", "&Ecliptic", "E&quatorial"};
    const QStringList orientationTips
        = {"The map's own frame; dragging also rolls the view",
           "Show the sky in Galactic coordinates, north up",
           "Show the sky in Ecliptic coordinates, north up",
           "Show the sky in Equatorial (J2000) coordinates, north up"};
    for (int i = 0; i < orientationNames.size(); ++i)
    {
        QAction *a = m_orientationMenu->addAction(orientationNames[i]);
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
    m_insideAction         = m_perspectiveMenu->addAction("&Inside (sky)");
    m_outsideAction        = m_perspectiveMenu->addAction("&Outside (globe)");
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
    m_nestedAction      = m_pixelOrderingMenu->addAction("&Nested");
    m_ringAction        = m_pixelOrderingMenu->addAction("&Ring");
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

    // Colors
    m_viewMenu->addAction("&Background Color...", this, [this]()
    {
        if (!m_view)
            return;
        QColor c = QColorDialog::getColor(m_view->glWidget()->backgroundColor(),
                                          this, "Select Background Color");
        if (c.isValid())
            onBackgroundColorChanged(c);
    });
    m_viewMenu->addAction("&Unseen Pixel Color...", this, [this]()
    {
        if (!m_view)
            return;
        QColor c = QColorDialog::getColor(m_view->glWidget()->unseenColor(),
                                          this, "Select Unseen Color");
        if (c.isValid())
            onUnseenColorChanged(c);
    });

    // Graticule spacing and color
    m_graticuleMenu       = m_viewMenu->addMenu("&Graticule");
    m_graticuleShowAction = m_graticuleMenu->addAction(
        "&Show Graticule", this, &HpVis::onGraticuleToggled);
    m_graticuleShowAction->setCheckable(true);
    m_graticuleStepMenu = m_graticuleMenu->addMenu("&Spacing");
    auto *stepGroup     = new QActionGroup(this);
    for (double deg : {10.0, 15.0, 20.0, 30.0, 45.0, 60.0, 90.0})
    {
        QAction *a = m_graticuleStepMenu->addAction(QString("%1°").arg(deg));
        a->setCheckable(true);
        a->setData(deg);
        stepGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, deg]()
        {
            if (m_view)
                m_view->glWidget()->setGraticuleStep(deg);
        });
    }
    m_graticuleMenu->addAction("&Color...", this, [this]()
    {
        if (!m_view)
            return;
        QColor c = QColorDialog::getColor(m_view->glWidget()->graticuleColor(),
                                          this, "Select Graticule Color");
        if (c.isValid())
            m_view->glWidget()->setGraticuleColor(c);
    });

    // Pixel grid: the boundaries of the HEALPix pixels of any nside
    QMenu *cellMenu     = m_viewMenu->addMenu("&Pixel Grid");
    m_cellGridShowAction = cellMenu->addAction("&Show Pixel Grid");
    m_cellGridShowAction->setCheckable(true);
    connect(m_cellGridShowAction, &QAction::toggled, this, [this](bool on)
    {
        if (m_view)
            m_view->glWidget()->setCellGridEnabled(on);
    });
    cellMenu->addAction("&Nside...", this, [this]()
    {
        if (!m_view)
            return;
        bool ok = false;
        const int n = QInputDialog::getInt(
            this, "Pixel Grid",
            "Nside of the grid (rounded to a power of two, 1 - 8192):",
            m_view->glWidget()->cellGridNside(), 1, 8192, 1, &ok);
        if (!ok)
            return;
        m_view->glWidget()->setCellGridNside(n);
        m_view->glWidget()->setCellGridEnabled(true);
        m_cellGridShowAction->setChecked(true);
    });
    m_cellLabelsAction = cellMenu->addAction("Show Pixel &Numbers");
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
    cellMenu->addAction("&Color...", this, [this]()
    {
        if (!m_view)
            return;
        const QColor c = QColorDialog::getColor(
            m_view->glWidget()->cellGridColor(), this,
            "Select Pixel Grid Color", QColorDialog::ShowAlphaChannel);
        if (c.isValid())
            m_view->glWidget()->setCellGridColor(c);
    });

    QMenu *customMenu = m_viewMenu->addMenu("Custom Colormaps");
    customMenu->addAction("&Load Colormap...", this,
                          &HpVis::onLoadColormapTriggered);
    customMenu->addAction("&Open Colormaps Folder", this, []()
    {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(ColormapRegistry::instance().directory()));
    });

    m_reverseColormapAction = m_viewMenu->addAction(
        "Re&verse Colormap", this, &HpVis::onColormapInvertChanged);
    m_reverseColormapAction->setCheckable(true);

    QMenu *scaleMenu    = m_viewMenu->addMenu("Color S&cale");
    auto *scaleGroup    = new QActionGroup(this);
    m_scaleLinearAction = scaleMenu->addAction("&Linear");
    m_scaleLogAction    = scaleMenu->addAction("L&ogarithmic");
    m_scaleAsinhAction  = scaleMenu->addAction("&Asinh");
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
    scaleMenu->addSeparator();
    scaleMenu->addAction(softAction);
    connect(m_asinhSlider, &QSlider::valueChanged, this, [this](int v)
    {
        if (m_view)
            m_view->glWidget()->setAsinhSoftening(
                std::pow(10.0, -3.0 + 3.0 * v / 100.0));
    });

    m_viewMenu->addAction("Color Bar &Range...", this,
                          &HpVis::onColormapRangeActionTriggered);
    m_viewMenu->addAction("Reset Color Bar Ran&ge", this,
                          &HpVis::onColormapRangeResetRequested);

    m_viewMenu->addAction("Set &Rotation...", this,
                          &HpVis::onRotationActionTriggered);
    m_viewMenu->addAction("Reset R&otation", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->resetRotation();
    });
    m_viewMenu->addSeparator();

    m_viewZoomMenu = m_viewMenu->addMenu("&Zoom");

    auto zoomLevels
        = {0.1f, 0.25f, 0.5f, 1.0f,  2.0f,  4.0f,   8.0f,
       16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f};

    for (float zoom : zoomLevels)
    {
        QString label = QString::number(zoom * 100) + "x";
        m_viewZoomMenu->addAction(label, this, [this, zoom]()
        {
            if (m_view)
                m_view->glWidget()->setZoom(zoom);
        });
    }

    m_viewZoomMenu->addSeparator();

    m_viewZoomMenu->addAction("Zoom To Region", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->beginZoomToRegion();
    });

    m_viewZoomMenu->addAction("Reset Zoom", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->resetZoom();
    });

    m_viewZoomMenu->addAction("Zoom &In", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->zoomStep(1.25f);
    });
    m_viewZoomMenu->addAction("Zoom &Out", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->zoomStep(0.8f);
    });

    // Moves a zoomed-in view; the keys are up to the user (Settings > Shortcuts)
    QMenu *panMenu = m_viewMenu->addMenu("&Pan");
    const std::tuple<const char *, float, float> pans[]
        = {{"&Left", -1.0f, 0.0f},
           {"&Right", 1.0f, 0.0f},
           {"&Up", 0.0f, 1.0f},
           {"&Down", 0.0f, -1.0f}};
    for (const auto &[name, dx, dy] : pans)
        panMenu->addAction(name, this, [this, dx = dx, dy = dy]()
        {
            if (m_view)
                m_view->glWidget()->panStep(dx, dy);
        });

    QMenu *regionMenu = m_viewMenu->addMenu("Select &Statistics Region");
    const std::pair<const char *, OpenGLWidget::RegionShape> shapes[] = {
        {"&Rectangle", OpenGLWidget::RegionShape::Rectangle},
        {"&Ellipse", OpenGLWidget::RegionShape::Ellipse},
        {"&Polygon (double click or Enter to close)",
         OpenGLWidget::RegionShape::Polygon},
        {"&Line (great circle)", OpenGLWidget::RegionShape::Line}};
    for (const auto &[name, shape] : shapes)
        regionMenu->addAction(name, this, [this, shape = shape]()
        {
            if (m_view)
                m_view->glWidget()->beginStatsRegion(shape);
        });
    m_viewMenu->addAction("Clear Statistics Re&gion", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->clearStatsRegion();
    });

    m_viewResetViewAction = m_viewMenu->addAction("&Reset View", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->resetView();
    });

    m_masksMenu = menuBar->addMenu("&Masks");
    rebuildMasksMenu(nullptr);

    m_toolsMenu = menuBar->addMenu("&Tools");

    m_toolsMenu->addAction("&Operate on Maps...", this,
                           &HpVis::onOperateTriggered);
    m_toolsMenu->addAction("Change &Resolution...", this,
                           &HpVis::onDowngradeTriggered);
    m_toolsMenu->addAction("&Beam Smoothing...", this,
                           &HpVis::onSmoothTriggered);

    m_toolsAnalysisMenu = m_toolsMenu->addMenu("&Analysis");

    // The docks provide checkable actions that stay in sync with their
    // visibility (including their own close buttons)
    m_toolsAnalysisMenu->addAction(m_powerDock->toggleViewAction());

    m_toolsAnalysisMenu->addAction(m_statsDock->toggleViewAction());

    m_toolsSettingsAction = m_toolsMenu->addAction(
        "&Settings", this, &HpVis::onSettingsActionTriggered);

    auto helpMenu = menuBar->addMenu("&Help");

    helpMenu->addAction("&About", this, []()
    {
        QMessageBox::about(nullptr, "About hpvis",
                           "hpvis is a simple viewer for HEALPix maps.");
    });
}

// Rebuilds File > Open Recent from the saved list
void
HpVis::updateRecentMenu()
{
    m_fileOpenRecentMenu->clear();
    // Only the menu itself can have a key: its entries change
    m_fileOpenRecentMenu->menuAction()->setProperty("hpvisBindMenu", true);

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
    m_fileOpenRecentMenu->addAction("&Clear Recent Files", this, [this]()
    {
        m_recentFiles.clear();
        updateRecentMenu();
    });
}

void
HpVis::OpenFile(const QString &filePath, int hdu, int column,
                const QString &columnName)
{
    QString resolvedPath;
    if (filePath.isEmpty())
    {
        auto files = QFileDialog::getOpenFileNames(
            this, "Open FITS File", startDirectory(),
            "Map Files (*.fits *.fit *.fts *.npy);;FITS Files (*.fits *.fit *.fts);;"
            "NumPy Files (*.npy);;All Files (*)");

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
            [this, view, filename, resolvedPath, fromCli](bool ok,
                                                          const QString &error)
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
    for (QAction *a : m_graticuleStepMenu->actions())
        a->setChecked(a->data().toDouble() == gl->graticuleStep());

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
    }
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

    QAction *add = m_masksMenu->addAction("&Add Mask...", this, [this]()
    {
        const auto files = QFileDialog::getOpenFileNames(
            this, "Load Mask(s)", "", "Mask Files (*.fits *.npy);;FITS Files (*.fits);;"
            "NumPy Files (*.npy);;All Files (*)");
        if (!files.isEmpty())
            onMaskFilesSelected(files);
    });
    add->setEnabled(gl);

    QAction *addColumn = m_masksMenu->addAction(
        "Add Mask From &Map Column...", this, [this]()
    {
        if (!m_view)
            return;
        MaskSourceDialog dialog(openMaps(), m_view, this);
        if (dialog.exec() == QDialog::Accepted && m_view)
            m_view->glWidget()->addMasks(dialog.selected());
    });
    addColumn->setEnabled(gl);

    QAction *clear
        = m_masksMenu->addAction("&Clear All", this, &HpVis::onMaskCleared);
    clear->setEnabled(gl && gl->maskCount() > 0);

    if (!gl || gl->maskCount() == 0)
        return;

    m_masksMenu->addSeparator();
    for (int i = 0; i < gl->maskCount(); ++i)
    {
        QMenu *sub = m_masksMenu->addMenu(gl->maskName(i));
        sub->menuAction()->setProperty("hpvisNoBind", true); // one per mask
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

        sub->addAction("&Remove", this, [this, i]()
        {
            if (m_view)
                m_view->glWidget()->removeMask(i);
        });
    }

    m_masksMenu->addSeparator();
    QMenu *mode = m_masksMenu->addMenu("&Mode");
    mode->menuAction()->setProperty("hpvisNoBind", true);
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
        = m_masksMenu->addAction("Dim &Strength...", this, [this]()
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
    });
    strength->setEnabled(gl->maskMode() == 1);
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

void
HpVis::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void
HpVis::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls())
    {
        for (const QUrl &url : mimeData->urls())
        {
            OpenFile(url.toLocalFile());
        }
    }
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

namespace
{
const char *const kFinishRegionId = "region/finish-polygon";
const char *const kCancelRegionId = "region/cancel-selection";

QString
shortcutSlug(const QString &text)
{
    QString out;
    for (const QChar c : text.toLower())
    {
        if (c.isLetterOrNumber())
            out += c;
        else if (!out.isEmpty() && !out.endsWith('-'))
            out += '-';
    }
    while (out.endsWith('-'))
        out.chop(1);
    return out;
}
} // namespace

void
HpVis::collectShortcutTargets(QMenu *menu, const QStringList &path,
                              QList<ShortcutTarget> &out, QSet<QString> &seen)
{
    for (QAction *a : menu->actions())
    {
        if (a->isSeparator() || a->property("hpvisNoBind").toBool())
            continue;
        QString text = a->text();
        text.remove('&');
        if (text.isEmpty())
            continue;

        QMenu *sub = a->menu();
        if (sub && !a->property("hpvisBindMenu").toBool())
        {
            collectShortcutTargets(sub, path + QStringList{text}, out, seen);
            continue;
        }

        // The built-in key is whatever the action has the first time it is
        // seen, before any of the user's keys are applied
        if (!a->property("hpvisDefaultKey").isValid())
            a->setProperty("hpvisDefaultKey",
                           a->shortcut().toString(QKeySequence::PortableText));

        QStringList slugs;
        for (const QString &part : path + QStringList{text})
            slugs << shortcutSlug(part);
        QString id = slugs.join('/');
        for (int n = 2; seen.contains(id); ++n)
            id = slugs.join('/') + "-" + QString::number(n);
        seen.insert(id);

        ShortcutTarget t;
        t.action = a;
        t.command.id       = id;
        t.command.label    = (path.mid(1) + QStringList{text}).join(" > ");
        t.command.category = path.value(0);
        t.command.defaultKey = QKeySequence(
            a->property("hpvisDefaultKey").toString(),
            QKeySequence::PortableText);
        out.append(t);
    }
}

QList<HpVis::ShortcutTarget>
HpVis::shortcutTargets()
{
    QList<ShortcutTarget> out;
    QSet<QString> seen;
    for (QAction *top : menuBar()->actions())
    {
        QString title = top->text();
        title.remove('&');
        if (top->menu())
            collectShortcutTargets(top->menu(), {title}, out, seen);
    }

    // Keys read by the viewer rather than being menu actions
    const std::pair<const char *, const char *> region[]
        = {{kFinishRegionId, "Finish polygon region"},
           {kCancelRegionId, "Cancel region selection"}};
    for (const auto &[id, label] : region)
    {
        ShortcutTarget t;
        t.command.id       = id;
        t.command.label    = label;
        t.command.category = "Region selection";
        t.command.defaultKey
            = QKeySequence(QString(id) == kFinishRegionId ? Qt::Key_Return
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
        else if (t.command.id == kFinishRegionId)
            finish = key;
        else if (t.command.id == kCancelRegionId)
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
    if (!m_view)
        return;
    OpenGLWidget *gl = m_view->glWidget();

    QDialog dialog(this);
    dialog.setWindowTitle("Rotation");
    auto *layout = new QFormLayout(&dialog);

    double lon, lat, roll;
    gl->rotationAngles(lon, lat, roll);

    auto makeSpin = [](double min, double max, double value, bool wrap)
    {
        auto *spin = new QDoubleSpinBox();
        spin->setRange(min, max);
        spin->setDecimals(2);
        spin->setSingleStep(1.0);
        spin->setSuffix(QString::fromUtf8("\u00b0"));
        spin->setWrapping(wrap);
        spin->setValue(value);
        return spin;
    };
    auto *lonSpin  = makeSpin(-180.0, 180.0, lon, true);
    auto *latSpin  = makeSpin(-90.0, 90.0, lat, false);
    auto *rollSpin = makeSpin(-180.0, 180.0, roll, true);
    layout->addRow("Center longitude:", lonSpin);
    layout->addRow("Center latitude:", latSpin);
    layout->addRow("Roll:", rollSpin);
    rollSpin->setEnabled(!gl->orientationLocked()); // north stays up

    // Applied live
    for (auto *spin : {lonSpin, latSpin, rollSpin})
        connect(spin, &QDoubleSpinBox::valueChanged, &dialog, [=](double)
        {
            gl->setRotationAngles(lonSpin->value(), latSpin->value(),
                                  rollSpin->value());
        });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *reset   = buttons->addButton("Reset", QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, &dialog, [=]()
    {
        gl->resetRotation();
        double a, b, c;
        gl->rotationAngles(a, b, c);
        QSignalBlocker b1(lonSpin), b2(latSpin), b3(rollSpin);
        lonSpin->setValue(a);
        latSpin->setValue(b);
        rollSpin->setValue(c);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    dialog.exec();
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

// Opens a map that was computed from other maps as a new tab
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
