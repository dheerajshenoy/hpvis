#pragma once

#include <QGenericMatrix>
#include <QString>
#include <QVector3D>

// Celestial coordinate systems HEALPix maps are given in
enum class SkyFrame
{
    Galactic,
    Ecliptic,
    Equatorial // ICRS / J2000 (FITS COORDSYS "C")
};

// How the view is oriented: Free uses the map's own frame with no constraint
// (a trackball that also rolls); the others show the sky in that frame and
// keep its north pole up when dragging.
enum class Orientation
{
    Free,
    Galactic,
    Ecliptic,
    Equatorial
};

namespace skyframe
{
// Rotation taking a unit vector in `from` to the same direction in `to`
QGenericMatrix<3, 3, float> rotation(SkyFrame from, SkyFrame to);

QVector3D apply(const QGenericMatrix<3, 3, float> &m, const QVector3D &v);

// FITS COORDSYS keyword ("G", "E", "C", "Galactic", ...); Galactic when
// missing or unknown, which is what most CMB maps use
SkyFrame fromCoordSys(const QString &coordsys);

// The frame an orientation shows, given the map's own frame
SkyFrame displayFrame(Orientation orientation, SkyFrame mapFrame);

// FITS COORDSYS code: "G", "E" or "C"
QString fitsCode(SkyFrame frame);

QString name(SkyFrame frame);
// Names of the longitude / latitude pair, e.g. "l", "b"
QString lonName(SkyFrame frame);
QString latName(SkyFrame frame);
} // namespace skyframe
