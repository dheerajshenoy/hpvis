#include "hpixview.hpp"

#include "fitsheaderdialog.hpp"
#include "pixelordering.hpp"
#include "powerspectrumdialog.hpp"
#include "settingsdialog.hpp"
#include "sidepanel.hpp"
#include "view.hpp"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsView>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <cmath>

HpixView::HpixView(const argparse::ArgumentParser &parser, QWidget *parent)
    : QMainWindow(parent)
{
    m_recentFiles.setMaxCount(m_settings.recentFilesMax);
    initUI();
    parseArgs(parser);
    initShortcuts();
    this->show();
}

void
HpixView::parseArgs(const argparse::ArgumentParser &parser)
{
    if (parser.is_used("files"))
    {
        auto files = parser.get<std::vector<std::string>>("files");
        for (const auto &file : files)
        {
            OpenFile(QString::fromStdString(file));
        }
    }
}

void
HpixView::initUI()
{
    QVBoxLayout *layout = new QVBoxLayout();
    QWidget *widget     = new QWidget();
    widget->setLayout(layout);

    m_tabWidget = new TabWidget();
    m_tabWidget->setContentsMargins(0, 0, 0, 0);
    m_sidePanel = new SidePanel();
    m_statusBar = new StatusBar();
    m_sidePanel->setVisible(false);
    initMenubar();
    layout->addWidget(m_tabWidget);
    setCentralWidget(widget);

    this->addDockWidget(Qt::LeftDockWidgetArea, m_sidePanel);

    // QMainWindow's own status bar sits below the dock areas and spans the
    // full window width; our widget fills it.
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(m_statusBar, 1);

    initConnections();
}

void
HpixView::initConnections()
{
    connect(m_sidePanel, &SidePanel::rotationEdited, this,
            [this](double lon, double lat, double roll)
    {
        if (!m_view)
            return;
        // Don't write the values back into the box being edited
        m_editingRotation = true;
        m_view->glWidget()->setRotationAngles(lon, lat, roll);
        m_editingRotation = false;
    });

    connect(m_sidePanel, &SidePanel::rotationResetRequested, this, [this]()
    {
        if (m_view)
            m_view->glWidget()->resetRotation();
    });

    connect(m_sidePanel, &SidePanel::projectionChanged, this,
            [this](Projection projection)
    {
        if (m_view)
            m_view->glWidget()->setProjection(projection);
    });

    connect(m_sidePanel, &SidePanel::graticuleToggled, this,
            &HpixView::onGraticuleToggled);

    connect(m_sidePanel, &SidePanel::graticuleColorChanged, this,
            [this](const QColor &color)
    {
        if (m_view)
            m_view->glWidget()->setGraticuleColor(color);
    });

    connect(m_sidePanel, &SidePanel::graticuleStepChanged, this,
            [this](double degrees)
    {
        if (m_view)
            m_view->glWidget()->setGraticuleStep(degrees);
    });

    connect(m_sidePanel, &SidePanel::fitsSelectionChanged, this,
            [this](int hdu, int column)
    {
        if (!m_view)
            return;
        if (m_view->loading())
        {
            // Busy: put the combos back to what is loaded
            m_sidePanel->setFitsInfo(m_view->fitsInfo(), m_view->hdu(),
                                     m_view->column());
            return;
        }
        m_statusBar->showMessage(
            QString("Loading HDU %1, column %2...").arg(hdu).arg(column));
        m_view->loadHdu(hdu, column);
    });

    connect(m_sidePanel, &SidePanel::pixelOrderingChanged, this,
            &HpixView::onPixelOrderingChanged);

    connect(m_sidePanel, &SidePanel::maskFilesSelected, this,
            &HpixView::onMaskFilesSelected);

    connect(m_sidePanel, &SidePanel::maskClearRequested, this,
            &HpixView::onMaskCleared);

    connect(m_sidePanel, &SidePanel::maskToggled, this,
            [this](int index, bool enabled)
    {
        if (m_view)
            m_view->glWidget()->setMaskEnabled(index, enabled);
    });

    connect(m_sidePanel, &SidePanel::maskInvertToggled, this,
            [this](int index, bool inverted)
    {
        if (m_view)
            m_view->glWidget()->setMaskInverted(index, inverted);
    });

    connect(m_sidePanel, &SidePanel::maskRemoveRequested, this,
            [this](int index)
    {
        if (m_view)
            m_view->glWidget()->removeMask(index);
    });

    connect(m_sidePanel, &SidePanel::maskModeChanged, this, [this](int mode)
    {
        if (m_view)
            m_view->glWidget()->setMaskMode(mode);
    });

    connect(m_sidePanel, &SidePanel::maskOpacityChanged, this,
            [this](float opacity)
    {
        if (m_view)
            m_view->glWidget()->setMaskOpacity(opacity);
    });

    connect(m_sidePanel, &SidePanel::backgroundColorChanged, this,
            &HpixView::onBackgroundColorChanged);

    connect(m_sidePanel, &SidePanel::unseenColorChanged, this,
            &HpixView::onUnseenColorChanged);

    connect(m_sidePanel, &SidePanel::colormapChanged, this,
            &HpixView::onColormapChanged);

    connect(m_sidePanel, &SidePanel::colormapRangeResetRequested, this,
            &HpixView::onColormapRangeResetRequested);

    connect(m_sidePanel, &SidePanel::colormapReverseChanged, this,
            &HpixView::onColormapInvertChanged);

    connect(m_sidePanel, &SidePanel::colormapScaleChanged, this,
            &HpixView::onColormapScaleChanged);

    connect(m_sidePanel, &SidePanel::colormapMaxChanged, this,
            &HpixView::onColormapMaxChanged);

    connect(m_sidePanel, &SidePanel::colormapMinChanged, this,
            &HpixView::onColormapMinChanged);

    connect(m_tabWidget, &TabWidget::currentChanged, this,
            &HpixView::onTabChanged);

    connect(m_tabWidget, &TabWidget::tabCloseRequested, this,
            &HpixView::onTabCloseRequested);
}

void
HpixView::initMenubar()
{
    QMenuBar *menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    m_fileMenu = menuBar->addMenu("&File");

    m_fileOpenAction
        = m_fileMenu->addAction("&Open", this, [this]() { OpenFile(); });

    m_fileOpenRecentMenu = m_fileMenu->addMenu("&Open Recent");
    updateRecentMenu();

    m_fileExportAction = m_fileMenu->addAction(
        "&Export", this, &HpixView::onExportActionTriggered);

    m_fileCloseTabAction = m_fileMenu->addAction("&Close Tab", this, [this]()
    {
        int index = m_tabWidget->currentIndex();
        if (index >= 0)
            onTabCloseRequested(index);
    });

    m_fileMenu->addAction("&Exit", this, &HpixView::close);

    m_editMenu = menuBar->addMenu("&Edit");
    m_viewMenu = menuBar->addMenu("&View");

    m_viewMinimapAction
        = m_viewMenu->addAction("&Minimap", this, &HpixView::onMinimapToggled);
    m_viewMinimapAction->setCheckable(true);

    m_viewColorBarAction
        = m_viewMenu->addAction("&Color Bar", this, [this](bool state)
    {
        if (m_view)
            m_view->glWidget()->setColorBarVisible(state);
    });
    m_viewColorBarAction->setCheckable(true);
    m_viewColorBarAction->setChecked(true);

    // The dock provides a checkable action that stays in sync with its
    // visibility (including its own close button)
    m_viewSidePanelAction = m_sidePanel->toggleViewAction();
    m_viewSidePanelAction->setText("&Side Panel");
    m_viewMenu->addAction(m_viewSidePanelAction);

    m_viewZoomMenu = m_viewMenu->addMenu("&Zoom");

    auto zoomLevels
        = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};

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

    m_viewResetViewAction = m_viewMenu->addAction("&Reset View", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->resetView();
    });

    m_toolsMenu = menuBar->addMenu("&Tools");

    m_toolsAnalysisMenu = m_toolsMenu->addMenu("&Analysis");

    m_toolsAnalysisMenu->addAction("&Power Spectrum", this,
                                   &HpixView::onPowerSpectrumActionTriggered);

    m_toolsAnalysisMenu->addAction("&Statistics", this,
                                   &HpixView::onStatisticsActionTriggered);

    m_toolsSettingsAction = m_toolsMenu->addAction(
        "&Settings", this, &HpixView::onSettingsActionTriggered);

    // A plain button in the menu bar (no drop-down)
    m_headerAction = menuBar->addAction("&Header", this, [this]()
    {
        if (!m_view)
        {
            QMessageBox::information(this, "FITS Header", "Open a file first.");
            return;
        }
        FitsHeaderDialog dialog(m_view->filePath(), m_view->hdu(), this);
        dialog.exec();
    });

    auto helpMenu = menuBar->addMenu("&Help");

    helpMenu->addAction("&About", this, []()
    {
        QMessageBox::about(nullptr, "About HpixView",
                           "HpixView is a simple viewer for HEALPix maps.");
    });
}

// Rebuilds File > Open Recent from the saved list
void
HpixView::updateRecentMenu()
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
    m_fileOpenRecentMenu->addAction("&Clear Recent Files", this, [this]()
    {
        m_recentFiles.clear();
        updateRecentMenu();
    });
}

void
HpixView::OpenFile(const QString &filePath)
{
    QString resolvedPath;
    if (filePath.isEmpty())
    {
        auto files = QFileDialog::getOpenFileNames(
            this, "Open FITS File", QDir::homePath(),
            "FITS Files (*.fits *.fit *.fts);;All Files (*)");

        for (const auto &file : files)
            OpenFile(file);

        return; // Exit the function after processing selected files
    }
    else
        resolvedPath = filePath;

    if (filePath.startsWith("~"))
        resolvedPath = QDir::homePath() + filePath.mid(1);

    const QString filename = QFileInfo(resolvedPath).fileName();
    m_statusBar->showMessage(QString("Loading %1...").arg(filename));

    // Parented to the window so it is cleaned up if we quit while loading
    View *view = new View(this);
    view->hide();

    connect(view, &View::loadFinished, this,
            [this, view, filename, resolvedPath](bool ok, const QString &error)
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
        m_tabWidget->addTab(view, filename);
        // Show it now so the GPU upload happens at open time, not on the
        // first tab switch
        m_tabWidget->setCurrentIndex(m_tabWidget->indexOf(view));
        initViewConnections(view);

        // The tab-changed signal fired inside addTab(), before this view had
        // its file info; fill the side panel and status bar now
        if (m_view == view)
        {
            updateSidePanelStateFromView(view);
            updateStatusBarFromView(view);
        }
        m_recentFiles.add(resolvedPath);
        updateRecentMenu();
        m_statusBar->showMessage(QString("Loaded %1").arg(filename), 3000);

        m_sidePanel->setVisible(true);
    });

    view->loadMap(resolvedPath);
}

void
HpixView::initViewConnections(const View *view)
{
    connect(view->glWidget(), &OpenGLWidget::mapLoaded, this,
            [this, view](long nside, long npix, double avg, double rms,
                         double std, float min, float max)
    {
        if (view == m_view)
            m_sidePanel->setMapInfo(nside, npix, avg, rms, std, min, max);
    });

    connect(view, &View::cursorPosChanged, m_statusBar,
            &StatusBar::setCursorData);

    connect(view, &View::zoomChanged, m_statusBar, &StatusBar::setZoom);

    // Dragging (or resetting) the view moves the side panel's values
    connect(view->glWidget(), &OpenGLWidget::rotationChanged, this,
            [this, view]()
    {
        if (view == m_view && !m_editingRotation)
            syncRotationControls(view);
    });

    // Result of switching HDU / column from the side panel
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
            m_sidePanel->setFitsInfo(view->fitsInfo(), view->hdu(),
                                     view->column());
    });

    // Mask load/clear/toggle changes the implicit colormap range
    connect(view, &View::colormapRangeChanged, this,
            [this, view](float min, float max)
    {
        if (view == m_view)
            m_sidePanel->setColormapRange(min, max);
    });

    connect(view, &View::masksChanged, this, [this, view]()
    {
        if (view == m_view)
        {
            // Statistics follow the enabled masks
            const auto *gl = view->glWidget();
            m_sidePanel->setMapInfo(gl->healpixMap()->Nside(),
                                    gl->healpixMap()->Npix(), gl->mapAverage(),
                                    gl->mapRms(), gl->mapStd(), gl->mapMin(),
                                    gl->mapMax());
        }
        updateMaskListFromView(view);
    });
}

void
HpixView::updateSidePanelStateFromView(const View *view)
{
    if (!view || !view->glWidget() || !view->glWidget()->healpixMap())
        return;

    auto *map = view->glWidget()->healpixMap();
    m_sidePanel->setFitsInfo(view->fitsInfo(), view->hdu(), view->column());
    m_sidePanel->setProjection(view->glWidget()->projection());
    syncRotationControls(view);
    m_sidePanel->setMapInfo(
        map->Nside(), map->Npix(), view->glWidget()->mapAverage(),
        view->glWidget()->mapRms(), view->glWidget()->mapStd(),
        view->glWidget()->mapMin(), view->glWidget()->mapMax());
    m_sidePanel->setPixelOrdering(view->glWidget()->pixelOrdering());
    m_sidePanel->setColormapRange(view->glWidget()->colormapMin(),
                                  view->glWidget()->colormapMax());
    updateMaskListFromView(view);
    m_sidePanel->setColormapScale(view->glWidget()->colormapScale());
    m_sidePanel->setGraticule(view->glWidget()->graticuleEnabled(),
                              view->glWidget()->graticuleStep());
    m_sidePanel->setColormapReverse(view->glWidget()->colormapInvert());
    m_sidePanel->setBackgroundColor(view->glWidget()->backgroundColor().rgb()
                                    & 0xFFFFFF);
    m_sidePanel->setUnseenColor(view->glWidget()->unseenColor().rgb()
                                & 0xFFFFFF);

    // setColormap emits colormapChanged only on change; block to avoid a
    // loop
    QSignalBlocker blocker(m_sidePanel);
    m_sidePanel->setColormap(view->glWidget()->colormap());
}

void
HpixView::onPixelOrderingChanged(PixelOrdering ordering)
{
    if (!m_view)
        return;

    m_view->glWidget()->setPixelOrdering(ordering);
}

void
HpixView::onTabChanged(int index)
{
    if (index < 0 || index >= m_tabWidget->count())
        return;

    QWidget *widget = m_tabWidget->widget(index);
    m_view          = qobject_cast<View *>(widget);

    updateSidePanelStateFromView(m_view);
    updateStatusBarFromView(m_view);
    if (m_view)
    {
        m_viewMinimapAction->setChecked(m_view->glWidget()->minimapEnabled());
        m_viewColorBarAction->setChecked(m_view->glWidget()->colorBarVisible());
    }
}

void
HpixView::updateMaskListFromView(const View *view)
{
    QStringList names, paths;
    QList<bool> enabled, inverted;
    for (int i = 0; i < view->glWidget()->maskCount(); ++i)
    {
        names << view->glWidget()->maskName(i);
        paths << view->glWidget()->maskPath(i);
        enabled << view->glWidget()->maskEnabled(i);
        inverted << view->glWidget()->maskInverted(i);
    }

    m_sidePanel->setFSky(view->glWidget()->fsky());
    m_sidePanel->setMasks(names, paths, enabled, inverted,
                          view->glWidget()->maskMode(),
                          view->glWidget()->maskOpacity());
}

void
HpixView::updateStatusBarFromView(const View *view)
{
    if (!view)
        return;

    const QFileInfo info(view->filePath());
    m_statusBar->setFilename(info.fileName());
    m_statusBar->setFilesize(QLocale().formattedDataSize(info.size()));
    m_statusBar->setZoom(view->glWidget()->zoom());
    m_statusBar->setCursorData(QPointF(), QPointF(), std::nan(""),
                               -1); // clear stale
}

void
HpixView::onTabCloseRequested(int index)
{
    if (index < 0 || index >= m_tabWidget->count())
        return;

    QWidget *widget = m_tabWidget->widget(index);
    m_tabWidget->removeTab(index);
    delete widget;

    if (m_tabWidget->count() == 0)
    {
        m_sidePanel->close();
        m_statusBar->clear();
    }
}

void
HpixView::onColormapChanged(Colormap colormap)
{
    if (m_view)
        m_view->glWidget()->setColormap(colormap);
}

void
HpixView::onColormapMaxChanged(double max)
{
    if (m_view)
        m_view->glWidget()->setColormapMax(max);
}

void
HpixView::onColormapMinChanged(double min)
{
    if (m_view)
        m_view->glWidget()->setColormapMin(min);
}

void
HpixView::onColormapInvertChanged(bool reversed)
{
    if (m_view)
        m_view->glWidget()->setColormapInvert(reversed);
}

void
HpixView::onColormapScaleChanged(ColormapScale scale)
{
    if (m_view)
        m_view->glWidget()->setColormapScale(scale);
}

void
HpixView::onColormapRangeResetRequested()
{
    if (m_view)
    {
        m_view->glWidget()->resetColormapRange();
        m_sidePanel->setColormapRange(m_view->glWidget()->colormapMin(),
                                      m_view->glWidget()->colormapMax());
    }
}

void
HpixView::onBackgroundColorChanged(const QColor &color)
{
    if (m_view)
        m_view->glWidget()->setBackgroundColor(color);
}

void
HpixView::onUnseenColorChanged(const QColor &color)
{
    if (m_view)
        m_view->glWidget()->setUnseenColor(color);
}

void
HpixView::onMaskFilesSelected(const QStringList &files)
{
    if (m_view)
        m_view->glWidget()->addMaskFiles(files);
}

void
HpixView::onMaskCleared()
{
    if (m_view)
        m_view->glWidget()->clearMasks();
}

void
HpixView::onMinimapToggled(bool state)
{
    if (m_view)
        m_view->glWidget()->setMinimapEnabled(state);
}

void
HpixView::onExportActionTriggered()
{
    if (!m_view)
        return;

    const QString filePath
        = QFileDialog::getSaveFileName(this, "Export Image", QDir::homePath(),
                                       "PNG Files (*.png);;All Files (*)");

    if (!filePath.isEmpty())
    {
        m_view->glWidget()->exportImage(filePath);
    }
}

void
HpixView::onSettingsActionTriggered()
{
    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::settingsSaved, this,
            &HpixView::applySavedSettings);
    dialog.exec();
}

// Picks up the settings the dialog just saved
void
HpixView::applySavedSettings()
{
    const AppSettings previous = m_settings;
    m_settings                 = AppSettings::load();
    m_recentFiles.setMaxCount(m_settings.recentFilesMax);
    updateRecentMenu();

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
    }

    // Refresh the side panel and menu check marks for the current tab
    if (m_view)
        onTabChanged(m_tabWidget->currentIndex());
}

void
HpixView::syncRotationControls(const View *view)
{
    double lon, lat, roll;
    view->glWidget()->rotationAngles(lon, lat, roll);
    m_sidePanel->setRotation(lon, lat, roll);
}

// Gives a newly loaded map the user's default view options
void
HpixView::applyDefaults(View *view)
{
    OpenGLWidget *gl = view->glWidget();
    gl->setColormap(m_settings.colormap);
    gl->setColormapInvert(m_settings.reverseColormap);
    gl->setColormapScale(m_settings.scale);
    gl->setBackgroundColor(m_settings.background);
    gl->setUnseenColor(m_settings.unseen);
    gl->setColorBarVisible(m_settings.colorBar);
    gl->setMinimapEnabled(m_settings.minimap);
    gl->setGraticuleEnabled(m_settings.graticule);
    gl->setGraticuleStep(m_settings.graticuleStep);
    gl->setGlobeShadow(m_settings.globeShadow);
    gl->setLimbDarkening(m_settings.limbDarkening);
}

void
HpixView::onGraticuleToggled(bool enabled)
{
    if (m_view)
        m_view->glWidget()->setGraticuleEnabled(enabled);
}

void
HpixView::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void
HpixView::dropEvent(QDropEvent *event)
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
HpixView::initShortcuts()
{
    m_fileCloseTabAction->setShortcut(QKeySequence("Ctrl+W"));
    m_fileOpenRecentMenu->menuAction()->setShortcut(
        QKeySequence("Ctrl+Shift+O"));
    m_fileOpenAction->setShortcut(QKeySequence("Ctrl+O"));
    m_viewMinimapAction->setShortcut(QKeySequence("M"));
    m_viewColorBarAction->setShortcut(QKeySequence("C"));
    m_viewResetViewAction->setShortcut(QKeySequence("0"));
}

void
HpixView::onPowerSpectrumActionTriggered()
{
    OpenGLWidget *gl = m_view->glWidget();
    const Healpix_Map<float> *map = gl->healpixMap();
    if (!map)
        return;

    PowerSpectrumInput input;
    input.path = m_view->filePath();
    input.hdu  = m_view->hdu();
    // A copy keeps the worker safe if the map is replaced meanwhile
    input.map = std::make_shared<Healpix_Map<float>>(*map);
    for (int i = 0; i < gl->maskCount(); ++i)
        input.maskActive |= gl->maskEnabled(i);

    PowerSpectrumDialog dialog(input, this);
    dialog.exec();
}

void
HpixView::onStatisticsActionTriggered()
{
}
