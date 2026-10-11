#pragma once

#include "projection.hpp"

#include <QVector2D>

// CPU versions of the projections in shaders/projections.glsl. They work on
// the projection plane [-1, 1]^2 (x to the right, y up); keep them in sync
// with the GLSL.
namespace projectionmath
{
// Width / height of the plane's picture: 2 for the full-sky rectangles and
// ellipses, 1 for the disks
double
aspect(Projection projection);

// Plane point -> sky (colatitude, longitude in [0, 2pi)). False when the
// point is outside the projection's shape.
bool
inverse(Projection projection, QVector2D plane, double &colat, double &phi);

// Sky (latitude, longitude, both radians) -> plane point. False when the
// point is not shown (the far side of the globe).
bool
forward(Projection projection, double lat, double lon, QVector2D &plane);

// A meridian that is visible in the projection, where latitude labels go
double
labelMeridian(Projection projection);
} // namespace projectionmath
