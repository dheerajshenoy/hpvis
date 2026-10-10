#include "hpixview.hpp"

#include "pixelordering.hpp"
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
    initUI();
    parseArgs(parser);
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

    m_statusBar->showMessage("Welcome to HpixView!", 5000);
    initMenubar();

    layout->addWidget(m_tabWidget);
    setCentralWidget(widget);

    this->addDockWidget(Qt::LeftDockWidgetArea, m_sidePanel);

    // QMainWindow's own status bar sits below the dock areas and spans the
    // full window width; our widget fills it.
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(m_statusBar, 1);

    initConnections();
    populateRecentFilesMenu();
}

void
HpixView::initConnections()
{
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

    connect(m_sidePanel, &SidePanel::colorBarToggled, this, [this](bool state)
    {
        if (m_view)
            m_view->glWidget()->setColorBarVisible(state);
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
    m_fileOpenAction->setShortcut(QKeySequence("Ctrl+O"));

    m_fileOpenRecentMenu = m_fileMenu->addMenu("&Open Recent");
    m_fileOpenRecentMenu->menuAction()->setShortcut(
        QKeySequence("Ctrl+Shift+O"));

    m_fileExportAction = m_fileMenu->addAction(
        "&Export", this, &HpixView::onExportActionTriggered);

    m_fileCloseTabAction = m_fileMenu->addAction("&Close Tab", this, [this]()
    {
        int index = m_tabWidget->currentIndex();
        if (index >= 0)
            onTabCloseRequested(index);
    });
    m_fileCloseTabAction->setShortcut(QKeySequence("Ctrl+W"));

    m_fileMenu->addAction("&Exit", this, &HpixView::close);

    m_editMenu = menuBar->addMenu("&Edit");
    m_viewMenu = menuBar->addMenu("&View");

    m_viewMinimapAction
        = m_viewMenu->addAction("&Minimap", this, &HpixView::onMinimapToggled);
    m_viewMinimapAction->setCheckable(true);

    // The dock provides a checkable action that stays in sync with its
    // visibility (including its own close button)
    m_viewSidePanelAction = m_sidePanel->toggleViewAction();
    m_viewSidePanelAction->setText("&Side Panel");
    m_viewMenu->addAction(m_viewSidePanelAction);

    m_viewResetViewAction = m_viewMenu->addAction("&Reset View", this, [this]()
    {
        if (m_view)
            m_view->glWidget()->resetView();
    });

    m_toolsMenu = menuBar->addMenu("&Tools");

    m_toolsSettingsAction = m_toolsMenu->addAction(
        "&Settings", this, &HpixView::onSettingsActionTriggered);

    auto helpMenu = menuBar->addMenu("&Help");

    helpMenu->addAction("&About", this, []()
    {
        QMessageBox::about(nullptr, "About HpixView",
                           "HpixView is a simple viewer for HEALPix maps.");
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
        {
            OpenFile(file);
        }

        return; // Exit the function after processing selected files
    }
    else
        resolvedPath = filePath;

    if (filePath.startsWith("~"))
        resolvedPath = QDir::homePath() + filePath.mid(1);

    View *view = new View();

    if (view->loadMap(resolvedPath.toStdString()))
    {
        auto filename = QFileInfo(resolvedPath).fileName();
        m_tabWidget->addTab(view, filename);
        initViewConnections(view);
    }
}

void
HpixView::initViewConnections(const View *view)
{
    connect(view, &View::cursorPosChanged, m_statusBar,
            &StatusBar::setCursorData);
    connect(view, &View::zoomChanged, m_statusBar, &StatusBar::setZoom);

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
            m_sidePanel->setColorBarVisible(
                view->glWidget()->colorBarVisible());
        updateMaskListFromView(view);
    });
}

void
HpixView::updateSidePanelStateFromView(const View *view)
{
    if (!view)
        return;

    m_sidePanel->setPixelOrdering(view->glWidget()->pixelOrdering());
    m_sidePanel->setColormapRange(view->glWidget()->colormapMin(),
                                  view->glWidget()->colormapMax());
    updateMaskListFromView(view);
    m_sidePanel->setColormapScale(view->glWidget()->colormapScale());
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
        m_viewMinimapAction->setChecked(m_view->glWidget()->minimapEnabled());
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
    m_statusBar->setCursorData(QPointF(), std::nan(""), -1); // clear stale
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
HpixView::populateRecentFilesMenu()
{
    // TODO: Implement recent files functionality
}

void
HpixView::onSettingsActionTriggered()
{
    // TODO: Implement settings dialog
}
