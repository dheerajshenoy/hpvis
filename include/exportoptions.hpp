#pragma once

#include <QString>

struct ExportOptions
{
    QString title;           // drawn above the map when not empty
    int width        = 0;    // output width in pixels (0 = as shown)
    int quality      = -1;   // 0-100 for lossy formats (-1 = format default)
    bool includeAlpha = false;
};
