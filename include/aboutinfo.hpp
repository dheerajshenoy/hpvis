#pragma once

#include <QFile>
#include <QString>

// The text of Help > About and of the About page in the settings, so the two
// always agree (the name is in the AboutBanner above it). A tagline with the
// version, then a table of labelled rows; the label column is gray so it works
// in light and dark themes.
inline QString
aboutHtml()
{
    const QString label = QStringLiteral("<td style=\"color: gray; "
                                         "padding-right: 18px; "
                                         "white-space: nowrap;\">%1</td>");
    auto row = [&label](const QString &name, const QString &value)
    {
        return QStringLiteral("<tr>") + label.arg(name)
               + QStringLiteral("<td style=\"padding-bottom: 6px;\">")
               + value + QStringLiteral("</td></tr>");
    };

    return QStringLiteral(
               "<p style=\"font-size: large; margin-bottom: 0;\">"
               "A viewer for HEALPix maps.</p>"
               "<p style=\"color: gray; margin-top: 2px;\">Version "
               PROJECT_VERSION "<br></p>"
               "<table cellspacing=\"0\" cellpadding=\"0\">")
           + row("Author",
                 "<a href=\"https://dheerajshenoy.github.io\">Dheeraj Vittal "
                 "Shenoy</a>")
           + row("Source",
                 "<a href=\"https://github.com/dheerajshenoy/hpvis\">"
                 "github.com/dheerajshenoy/hpvis</a>")
           + row("License", "AGPL-3.0")
           + row("Support",
                 "<a href=\"https://ko-fi.com/dheerajshenoy\">Ko-fi</a>"
                 " &middot; "
                 "<a href=\"https://github.com/sponsors/dheerajshenoy\">"
                 "GitHub Sponsors</a>"
                 " &middot; "
                 "<a href=\"https://liberapay.com/dheerajshenoy\">"
                 "Liberapay</a>")
           + QStringLiteral("</table>");
}

// The code libraries hpvis uses: name (linked) and what for
inline QString
librariesHtml()
{
    struct Lib
    {
        const char *name, *url, *use;
    };
    static const Lib libs[] = {
        {"Qt 6", "https://www.qt.io", "Interface, OpenGL and plots"},
        {"HEALPix C++", "https://healpix.sourceforge.io",
         "Pixelization and spherical harmonic transforms"},
        {"libsharp", "https://gitlab.mpcdf.mpg.de/mtr/libsharp",
         "Spherical harmonic transforms backend"},
        {"NaMaster", "https://github.com/LSSTDESC/NaMaster",
         "Mask-corrected power spectra (MASTER)"},
        {"CFITSIO", "https://heasarc.gsfc.nasa.gov/fitsio/",
         "Reading and writing FITS files"},
        {"libzip", "https://libzip.org", "Reading NumPy .npz files"},
        {"argparse", "https://github.com/p-ranav/argparse",
         "Command-line arguments"},
    };

    QString html = QStringLiteral("<table cellspacing=\"0\" "
                                  "cellpadding=\"0\">");
    for (const Lib &l : libs)
        html += QStringLiteral("<tr><td style=\"padding: 0 18px 8px 0; "
                               "white-space: nowrap;\"><a href=\"%1\">%2</a>"
                               "</td><td style=\"padding-bottom: 8px;\">%3"
                               "</td></tr>")
                    .arg(QLatin1String(l.url), QLatin1String(l.name),
                         QLatin1String(l.use));
    return html + QStringLiteral("</table>");
}

// The licence of hpvis (embedded from the LICENSE file of the repository)
inline QString
licenseText()
{
    QFile file(QStringLiteral(":/LICENSE"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("The license text could not be loaded. See "
                              "https://www.gnu.org/licenses/agpl-3.0.html");
    return QString::fromUtf8(file.readAll());
}
