#pragma once

#include "colormap.hpp"
#include "projection.hpp"
#include "skyframe.hpp"

#include <QColor>
#include <QMap>
#include <QString>

// User preferences, stored with QSettings (~/.config/hpvis/hpvis.conf
// on Linux). The view options are the defaults for newly opened maps.
struct AppSettings
{
    // General
    int recentFilesMax   = 10;
    bool rememberLastDir = true; // file dialogs start in the last used folder
    bool showToolbar     = true;
    bool showStatusBar   = true;
    // Maps with a larger nside are streamed (coarse map + visible tiles)
    // instead of being kept on the GPU in full
    int streamingNside   = 4096;
    // Render into a 16-bit float framebuffer (no banding); needs a restart
    bool hdrRendering    = true;

    // New maps
    Colormap colormap       = Colormap::VIRIDIS;
    bool reverseColormap    = false;
    ColormapScale scale     = ColormapScale::LINEAR;
    QColor background       = QColor(10, 10, 15);
    QColor unseen           = QColor(0, 0, 0);
    Orientation orientation = Orientation::Free;
    bool viewFromOutside    = false; // false: sky seen from the inside
    Projection projection   = Projection::MOLLWEIDE;
    QColor graticuleColor   = QColor(255, 255, 255);
    int maskMode            = 0;  // 0 = hide, 1 = dim
    int maskOpacityPercent  = 80; // dim strength
    bool colorBar           = true;
    bool minimap            = false;
    bool graticule          = false;
    double graticuleStep    = 30.0; // degrees
    bool globeShadow        = true; // orthographic outer shadow
    bool limbDarkening      = true; // orthographic limb darkening
    // Power spectrum plot
    QColor plotBackground   = QColor(0x12, 0x12, 0x17);
    QColor plotForeground   = QColor(0xe6, 0xe6, 0xe6);
    QColor plotLine         = QColor(0x7f, 0xd1, 0xff);
    bool animateOrthoGlobe  = true; // orthographic globe rotation animation
    bool animateProjection  = true; // fade between projections
    // Theoretical CMB spectrum overplotted on the power spectrum
    int theoryPreset        = 0; // TheoryPreset
    QString theoryFile;          // for the custom preset
    int theoryUnit          = 0; // TheoryUnit: the maps' unit (uK, mK, K)

    // Key bindings that differ from the built-in ones: command id -> key
    // sequence (PortableText). An empty sequence means the key was removed.
    QMap<QString, QString> shortcuts;

    bool operator==(const AppSettings &) const = default;

    static AppSettings load();
    void save() const;
};
