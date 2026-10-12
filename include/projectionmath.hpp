#pragma once

#include "projection.hpp"

#include <QVector2D>

// CPU versions of the projections in shaders/projections.glsl. They work on
// the projection plane [-1, 1]^2 (x to the right, y up); keep them in sync
// with the GLSL.
namespace projectionmath
{
// The globe-like projections (a disk centered on a point you rotate), as
// opposed to the full-sky maps with the poles on top and bottom
bool
isDisk(Projection projection);

// Width / height of the plane's picture: 2 for the full-sky rectangles and
// ellipses, 1 for the disks
double
aspect(Projection projection);

// All the functions below take `fromOutside`: false is the sky seen from the
// inside (longitude increasing to the left, as in healpy), true is the mirror
// image, seen from the outside like a globe (longitude increasing to the
// right).

// Plane point -> sky (colatitude, longitude in [0, 2pi)). False when the
// point is outside the projection's shape.
bool
inverse(Projection projection, QVector2D plane, double &colat, double &phi,
        bool fromOutside = false);

// Sky (latitude, longitude, both radians) -> plane point. False when the
// point is not shown (the far side of the globe).
bool
forward(Projection projection, double lat, double lon, QVector2D &plane,
        bool fromOutside = false);

// A meridian that is visible in the projection, where latitude labels go
double
labelMeridian(Projection projection, bool fromOutside = false);
} // namespace projectionmath
