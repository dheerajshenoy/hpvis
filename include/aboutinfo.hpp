#pragma once

#include <QString>

// The text of Help > About and of the About page in the settings, so the two
// always agree
inline QString
aboutHtml()
{
    return QStringLiteral(
        "<h2>hpvis</h2>"
        "<p>Version: " PROJECT_VERSION "</p>"
        "<p>A viewer for HEALPix maps.</p>"
        "<p>Developed by <a href=\"https://dheerajshenoy.github.io\">Dheeraj "
        "Vittal Shenoy</a>.</p>"
        "<p><a href=\"https://github.com/dheerajshenoy/hpvis\">"
        "GitHub repository</a></p>"
        "<p>Licensed under the AGPL-3.0 License.</p>"
        "<p>Support the development: "
        "<a href=\"https://ko-fi.com/dheerajshenoy\">Ko-fi</a>"
        " &middot; "
        "<a href=\"https://github.com/sponsors/dheerajshenoy\">"
        "GitHub Sponsors</a>"
        " &middot; "
        "<a href=\"https://liberapay.com/dheerajshenoy\">"
        "Liberapay</a></p>");
}
