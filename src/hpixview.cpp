#include "hpixview.hpp"

#include "view.hpp"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QGraphicsView>
#include <QMenuBar>
#include <QMessageBox>
#include <QVBoxLayout>

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
    initMenubar();

    layout->addWidget(m_tabWidget);
    setCentralWidget(widget);
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

    view->loadMap(resolvedPath.toStdString());
    m_tabWidget->setContentsMargins(0, 0, 0, 0);
    m_tabWidget->addTab(view, QFileInfo(resolvedPath).fileName());
}
