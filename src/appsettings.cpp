#include "appsettings.hpp"

#include <QSettings>
#include <algorithm>

AppSettings
AppSettings::load()
{
    const AppSettings d; // defaults
    QSettings s;
    AppSettings a;

    a.recentFilesMax
        = std::clamp(s.value("general/recentFilesMax", d.recentFilesMax).toInt(),
                     0, 50);

    // Out-of-range values (e.g. from a newer version) fall back to the default
    const int cm = s.value("view/colormap", int(d.colormap)).toInt();
    a.colormap   = (cm >= 0 && cm <= int(Colormap::RAINBOW)) ? Colormap(cm)
                                                              : d.colormap;
    const int sc = s.value("view/scale", int(d.scale)).toInt();
    a.scale      = (sc == int(ColormapScale::LOG)) ? ColormapScale::LOG
                                                   : ColormapScale::LINEAR;

    a.reverseColormap = s.value("view/reverseColormap", d.reverseColormap).toBool();
    a.background      = QColor(s.value("view/background", d.background.name()).toString());
    a.unseen          = QColor(s.value("view/unseen", d.unseen.name()).toString());
    if (!a.background.isValid())
        a.background = d.background;
    if (!a.unseen.isValid())
        a.unseen = d.unseen;

    a.colorBar  = s.value("view/colorBar", d.colorBar).toBool();
    a.minimap   = s.value("view/minimap", d.minimap).toBool();
    a.graticule = s.value("view/graticule", d.graticule).toBool();
    a.graticuleStep
        = std::clamp(s.value("view/graticuleStep", d.graticuleStep).toDouble(),
                     1.0, 90.0);
    a.globeShadow   = s.value("view/globeShadow", d.globeShadow).toBool();
    a.limbDarkening = s.value("view/limbDarkening", d.limbDarkening).toBool();
    return a;
}

void
AppSettings::save() const
{
    QSettings s;
    s.setValue("general/recentFilesMax", recentFilesMax);

    s.setValue("view/colormap", int(colormap));
    s.setValue("view/reverseColormap", reverseColormap);
    s.setValue("view/scale", int(scale));
    s.setValue("view/background", background.name());
    s.setValue("view/unseen", unseen.name());
    s.setValue("view/colorBar", colorBar);
    s.setValue("view/minimap", minimap);
    s.setValue("view/graticule", graticule);
    s.setValue("view/graticuleStep", graticuleStep);
    s.setValue("view/globeShadow", globeShadow);
    s.setValue("view/limbDarkening", limbDarkening);
    s.sync();
}
