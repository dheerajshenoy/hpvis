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
