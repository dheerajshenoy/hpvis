#include "hpixview.hpp"

#include <QApplication>

int
main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    HpixView view(argc, argv);

    app.exec();
}
