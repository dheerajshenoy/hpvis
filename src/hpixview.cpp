#include "hpixview.hpp"

#include "pixelordering.hpp"
#include "sidepanel.hpp"
#include "view.hpp"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QGraphicsView>
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
}

void
HpixView::initConnections()
{
    connect(m_sidePanel, &SidePanel::pixelOrderingChanged, this,
            &HpixView::onPixelOrderingChanged);

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

    m_fileMenu->addAction("&Open", this, [this]() { OpenFile(); });
    m_fileMenu->addAction("&Exit", this, &HpixView::close);

    m_editMenu = menuBar->addMenu("&Edit");

    m_viewMenu = menuBar->addMenu("&View");

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
}

void
HpixView::updateSidePanelStateFromView(const View *view)
{
    if (!view)
        return;

    m_sidePanel->setPixelOrdering(view->pixelOrdering());
    m_sidePanel->setColormapRange(view->colormapMin(), view->colormapMax());
    m_sidePanel->setColormapScale(view->colormapScale());
    m_sidePanel->setColormapReverse(view->colormapInvert());
    m_sidePanel->setBackgroundColor(view->backgroundColor().rgb() & 0xFFFFFF);
    m_sidePanel->setUnseenColor(view->unseenColor().rgb() & 0xFFFFFF);

    // setColormap emits colormapChanged only on change; block to avoid a loop
    QSignalBlocker blocker(m_sidePanel);
    m_sidePanel->setColormap(view->colormap());
}

void
HpixView::onPixelOrderingChanged(PixelOrdering ordering)
{
    if (!m_view)
        return;

    m_view->setPixelOrdering(ordering);
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
}

void
HpixView::updateStatusBarFromView(const View *view)
{
    if (!view)
        return;

    const QFileInfo info(view->filePath());
    m_statusBar->setFilename(info.fileName());
    m_statusBar->setFilesize(QLocale().formattedDataSize(info.size()));
    m_statusBar->setZoom(view->zoom());
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
}

void
HpixView::onColormapChanged(Colormap colormap)
{
    if (m_view)
        m_view->setColormap(colormap);
}

void
HpixView::onColormapMaxChanged(double max)
{
    if (m_view)
        m_view->setColormapMax(max);
}

void
HpixView::onColormapMinChanged(double min)
{
    if (m_view)
        m_view->setColormapMin(min);
}

void
HpixView::onColormapInvertChanged(bool reversed)
{
    if (m_view)
        m_view->setColormapInvert(reversed);
}

void
HpixView::onColormapScaleChanged(ColormapScale scale)
{
    if (m_view)
        m_view->setColormapScale(scale);
}

void
HpixView::onColormapRangeResetRequested()
{
    if (m_view)
    {
        m_view->resetColormapRange();
        m_sidePanel->setColormapRange(m_view->colormapMin(),
                                      m_view->colormapMax());
    }
}

void
HpixView::onBackgroundColorChanged(const QColor &color)
{
    if (m_view)
        m_view->setBackgroundColor(color);
}

void
HpixView::onUnseenColorChanged(const QColor &color)
{
    if (m_view)
        m_view->setUnseenColor(color);
}
