#pragma once

#include "colormap.hpp"

#include <QColor>

// User preferences, stored with QSettings (~/.config/Hpixview/Hpixview.conf
// on Linux). The view options are the defaults for newly opened maps.
struct AppSettings
{
    // General
    int recentFilesMax = 10;

    // New maps
    Colormap colormap         = Colormap::VIRIDIS;
    bool reverseColormap      = false;
    ColormapScale scale       = ColormapScale::LINEAR;
    QColor background         = QColor(10, 10, 15);
    QColor unseen             = QColor(0, 0, 0);
    bool colorBar             = true;
    bool minimap              = false;
    bool graticule            = false;
    double graticuleStep      = 30.0; // degrees
    bool globeShadow          = true; // orthographic outer shadow
    bool limbDarkening        = true; // orthographic limb darkening

    static AppSettings load();
    void save() const;
};
