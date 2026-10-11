#include "argparse.hpp"
#include "hpixview.hpp"

#include <QApplication>

void
addArguments(argparse::ArgumentParser &parser)
{
    parser.add_argument("files").remaining();
}

int
main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Names the per-user data directory (e.g. ~/.local/share/Hpixview)
    QCoreApplication::setApplicationName(PROJECT_NAME);
    // QSettings needs both names (config goes to ~/.config/Hpixview/)
    QCoreApplication::setOrganizationName(PROJECT_NAME);
    QCoreApplication::setOrganizationName("Dheeraj Shenoy");
    QCoreApplication::setOrganizationDomain("dheerajshenoy.github.io/hpixview");
    QCoreApplication::setApplicationName("HpixView");

    argparse::ArgumentParser parser(PROJECT_NAME, PROJECT_VERSION,
                                    argparse::default_arguments::all);
    addArguments(parser);
    try
    {
        parser.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << parser;
        exit(1);
    }

    HpixView view(parser);

    app.exec();
}
