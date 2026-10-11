#include "hpvis.hpp"

#include "fitsheaderdialog.hpp"
#include "pixelordering.hpp"
#include "powerspectrumdialog.hpp"
#include "settingsdialog.hpp"
#include "view.hpp"
#include "viewtoolbar.hpp"

#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsView>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QVBoxLayout>
#include <cmath>

HpVis::HpVis(const argparse::ArgumentParser &parser, QWidget *parent)
    : QMainWindow(parent)
{
    m_recentFiles.setMaxCount(m_settings.recentFilesMax);
    initUI();
    parseArgs(parser);
    initShortcuts();
    this->show();
}

void
HpVis::parseArgs(const argparse::ArgumentParser &parser)
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
    m_statusBar = new StatusBar();
    m_toolBar->setEnabled(false);
    initMenubar();
    layout->addWidget(m_tabWidget);
    setCentralWidget(widget);

    addToolBar(Qt::TopToolBarArea, m_toolBar);

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

    m_fileExportAction = m_fileMenu->addAction(
        "&Export", this, &HpVis::onExportActionTriggered);

    m_headerAction = m_fileMenu->addAction("FITS &Header...", this, [this]()
    {
        if (!m_view)
        {
            QMessageBox::information(this, "FITS Header", "Open a file first.");
            return;
        }
        FitsHeaderDialog dialog(m_view->filePath(), m_view->hdu(), this);
        dialog.exec();
    });

    m_fileCloseTabAction = m_fileMenu->addAction("&Close Tab", this, [this]()
    {
        int index = m_tabWidget->currentIndex();
        if (index >= 0)
            onTabCloseRequested(index);
    });

    m_fileMenu->addAction("&Exit", this, &HpVis::close);

    m_editMenu = menuBar->addMenu("&Edit");
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

    m_reverseColormapAction = m_viewMenu->addAction(
        "Re&verse Colormap", this, &HpVis::onColormapInvertChanged);
    m_reverseColormapAction->setCheckable(true);

    QMenu *scaleMenu    = m_viewMenu->addMenu("Color S&cale");
    auto *scaleGroup    = new QActionGroup(this);
    m_scaleLinearAction = scaleMenu->addAction("&Linear");
    m_scaleLogAction    = scaleMenu->addAction("L&ogarithmic");
    for (QAction *a : {m_scaleLinearAction, m_scaleLogAction})
    {
        a->setCheckable(true);
        scaleGroup->addAction(a);
    }
    m_scaleLinearAction->setChecked(true);
    connect(m_scaleLinearAction, &QAction::triggered, this,
            [this]() { onColormapScaleChanged(ColormapScale::LINEAR); });
    connect(m_scaleLogAction, &QAction::triggered, this,
            [this]() { onColormapScaleChanged(ColormapScale::LOG); });

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

    m_masksMenu = menuBar->addMenu("&Masks");
    rebuildMasksMenu(nullptr);

    m_toolsMenu = menuBar->addMenu("&Tools");

    m_toolsAnalysisMenu = m_toolsMenu->addMenu("&Analysis");

    m_toolsAnalysisMenu->addAction("&Power Spectrum", this,
                                   &HpVis::onPowerSpectrumActionTriggered);

    m_toolsAnalysisMenu->addAction("&Statistics", this,
                                   &HpVis::onStatisticsActionTriggered);

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
HpVis::OpenFile(const QString &filePath)
{
    QString resolvedPath;
    if (filePath.isEmpty())
    {
        auto files = QFileDialog::getOpenFileNames(
            this, "Open FITS File", startDirectory(),
            "FITS Files (*.fits *.fit *.fts);;All Files (*)");

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
            updateControlsFromView(view);
            updateStatusBarFromView(view);
        }
        m_recentFiles.add(resolvedPath);
        updateRecentMenu();
        m_statusBar->showMessage(QString("Loaded %1").arg(filename), 3000);

        m_toolBar->setEnabled(true);
    });

    view->loadMap(resolvedPath);
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
            m_toolBar->setFitsInfo(view->fitsInfo(), view->hdu(),
                                   view->column());
    });

    connect(view, &View::masksChanged, this, [this, view]()
    {
        if (view == m_view)
            updateMaskListFromView(view);
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
    (gl->colormapScale() == ColormapScale::LINEAR ? m_scaleLinearAction
                                                  : m_scaleLogAction)
        ->setChecked(true);
    m_graticuleShowAction->setChecked(gl->graticuleEnabled());
    m_reverseColormapAction->setChecked(gl->colormapInvert());
    m_toolBar->setColormap(gl->colormap());

    (gl->pixelOrdering() == PixelOrdering::NESTED ? m_nestedAction
                                                  : m_ringAction)
        ->setChecked(true);
    for (QAction *a : m_graticuleStepMenu->actions())
        a->setChecked(a->data().toDouble() == gl->graticuleStep());

    updateMaskListFromView(view);
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
    m_masksMenu->clear();
    OpenGLWidget *gl = view ? view->glWidget() : nullptr;

    QAction *add = m_masksMenu->addAction("&Add Mask...", this, [this]()
    {
        const auto files = QFileDialog::getOpenFileNames(
            this, "Load Mask(s)", "", "FITS Files (*.fits);;All Files (*)");
        if (!files.isEmpty())
            onMaskFilesSelected(files);
    });
    add->setEnabled(gl);

    QAction *clear
        = m_masksMenu->addAction("&Clear All", this, &HpVis::onMaskCleared);
    clear->setEnabled(gl && gl->maskCount() > 0);

    if (!gl || gl->maskCount() == 0)
        return;

    m_masksMenu->addSeparator();
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

        sub->addAction("&Remove", this, [this, i]()
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
    m_statusBar->setFilename(info.fileName());
    m_statusBar->setFilesize(QLocale().formattedDataSize(info.size()));
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
    m_tabWidget->removeTab(index);
    delete widget;

    if (m_tabWidget->count() == 0)
    {
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

    const QString filePath
        = QFileDialog::getSaveFileName(this, "Export Image", startDirectory(),
                                       "PNG Files (*.png);;All Files (*)");

    if (!filePath.isEmpty())
    {
        rememberDirectory(filePath);
        m_view->glWidget()->exportImage(filePath);
    }
}

void
HpVis::onSettingsActionTriggered()
{
    SettingsDialog dialog(this);
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
    m_recentFiles.setMaxCount(m_settings.recentFilesMax);
    updateRecentMenu();
    applyInterfaceSettings();

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
    gl->setProjection(m_settings.projection);
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
}

void
HpVis::onPowerSpectrumActionTriggered()
{
    if (!m_view)
        return;
    OpenGLWidget *gl              = m_view->glWidget();
    const Healpix_Map<float> *map = gl->healpixMap();
    if (!map)
        return;

    PowerSpectrumInput input;
    input.path = m_view->filePath();
    input.hdu  = m_view->hdu();
    // A copy keeps the worker safe if the map is replaced meanwhile
    input.map  = std::make_shared<Healpix_Map<float>>(*map);
    for (int i = 0; i < gl->maskCount(); ++i)
        input.maskActive |= gl->maskEnabled(i);

    PowerSpectrumDialog dialog(input, this);
    dialog.exec();
}

void
HpVis::onStatisticsActionTriggered()
{
    if (!m_view || !m_view->glWidget()->healpixMap())
    {
        QMessageBox::information(this, "Statistics", "Open a file first.");
        return;
    }
    const OpenGLWidget *gl = m_view->glWidget();

    QDialog dialog(this);
    dialog.setWindowTitle("Statistics");
    auto *layout = new QFormLayout(&dialog);

    auto add = [&](const QString &name, const QString &value)
    {
        auto *label = new QLabel(value);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addRow(name, label);
    };
    auto num = [](double v)
    {
        return QString::number(v, 'g', 6);
    };

    add("Nside:", QString::number(gl->healpixMap()->Nside()));
    add("Npix:", QString::number(gl->healpixMap()->Npix()));
    add("Min:", num(gl->mapMin()));
    add("Max:", num(gl->mapMax()));
    add("Average:", num(gl->mapAverage()));
    add("RMS:", num(gl->mapRms()));
    add("Std dev:", num(gl->mapStd()));
    add("Sky fraction:", QString::number(gl->fsky(), 'f', 6));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    dialog.exec();
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
