#include "hpixview.hpp"

#include <QDir>
#include <QGraphicsView>
#include <QVBoxLayout>
#include <alm_healpix_tools.h>

HpixView::HpixView(int argc, char *argv[], QWidget *parent)
    : QMainWindow(parent)
{
    (void)argc;
    (void)argv;

    initUI();

    OpenFile("~/Gits/PhD/codes/cmb_40GHz_64_s0_d0.fits");

    this->show();
}

void
HpixView::initUI()
{
    QVBoxLayout *layout = new QVBoxLayout();

    m_glWidget = new OpenGLWidget();

    layout->addWidget(m_glWidget);

    QWidget *widget = new QWidget();
    widget->setLayout(layout);

    setCentralWidget(widget);
}

void
HpixView::OpenFile(const QString &filePath)
{
    // Replace ~ with the user's home directory
    QString resolvedPath = filePath;
    if (filePath.startsWith("~"))
    {
        resolvedPath = QDir::homePath() + filePath.mid(1);
    }

    Healpix_Map<float> map;

    read_Healpix_map_from_fits(resolvedPath.toStdString(), map, 1, 2);
}
