#include "projectionmath.hpp"

#include <algorithm>
#include <cmath>

namespace projectionmath
{
namespace
{
constexpr double kSqrt2 = 1.41421356237309504880;

double
wrapPi(double lon) // (-pi, pi]
{
    return std::remainder(lon, 2.0 * M_PI);
}

double
wrap2Pi(double lon) // [0, 2pi)
{
    lon = std::fmod(lon, 2.0 * M_PI);
    return lon < 0.0 ? lon + 2.0 * M_PI : lon;
}
} // namespace

double
aspect(Projection projection)
{
    switch (projection)
    {
        case Projection::MOLLWEIDE:
        case Projection::PLATE_CARREE:
            return 2.0;
        case Projection::ORTHOGRAPHIC:
        case Projection::AZIMUTHAL_EQUIDISTANT:
            return 1.0;
        case Projection::GNOMONIC:
            return 1.0;
    }
    return 2.0;
}

bool
inverse(Projection projection, QVector2D plane, double &colat, double &phi)
{
    const double px = plane.x(), py = plane.y();

    switch (projection)
    {
        case Projection::MOLLWEIDE:
        {
            const double x = -px * 2.0 * kSqrt2; // longitude increases left
            const double y = py * kSqrt2;
            if ((x * x / 8.0) + (y * y / 2.0) > 1.0)
                return false;

            const double aux    = std::asin(std::clamp(y / kSqrt2, -1.0, 1.0));
            const double lat    = std::asin(std::clamp(
                (2.0 * aux + std::sin(2.0 * aux)) / M_PI, -1.0, 1.0));
            const double cosAux = std::cos(aux);
            double lon          = 0.0;
            if (std::abs(cosAux) > 0.0001)
                lon = (M_PI * x) / (2.0 * kSqrt2 * cosAux);
            colat = M_PI / 2.0 - lat;
            phi   = wrap2Pi(lon);
            return true;
        }

        case Projection::PLATE_CARREE:
            colat = M_PI / 2.0 - py * M_PI / 2.0;
            phi   = wrap2Pi(-px * M_PI);
            return true;

        case Projection::ORTHOGRAPHIC:
        {
            const double r2 = px * px + py * py;
            if (r2 > 1.0)
                return false;
            const double z = std::sqrt(std::max(0.0, 1.0 - r2));
            colat          = std::acos(std::clamp(py, -1.0, 1.0));
            phi            = wrap2Pi(std::atan2(-px, z));
            return true;
        }

        case Projection::AZIMUTHAL_EQUIDISTANT:
        {
            const double x = px * M_PI, y = py * M_PI;
            const double r = std::hypot(x, y);
            if (r > M_PI)
                return false;
            colat = r;
            phi   = wrap2Pi(std::atan2(-x, y));
            return true;
        }

        case Projection::GNOMONIC:
        {
            // Tangent plane at the center: direction (1, -x, y) normalized
            // (-x: longitude increases left); every plane point is valid
            const double len = std::sqrt(1.0 + px * px + py * py);
            const double dx = 1.0 / len, dy = -px / len, dz = py / len;
            colat = std::atan2(std::hypot(dx, dy), dz);
            phi   = wrap2Pi(std::atan2(dy, dx));
            return true;
        }
    }
    return false;
}

bool
forward(Projection projection, double lat, double lon, QVector2D &plane)
{
    switch (projection)
    {
        case Projection::MOLLWEIDE:
        {
            // Solve 2t + sin(2t) = pi * sin(lat) by Newton iteration
            double t = lat;
            for (int i = 0; i < 30; ++i)
            {
                const double d = 2.0 + 2.0 * std::cos(2.0 * t);
                if (std::abs(d) < 1e-9)
                    break;
                t -= (2.0 * t + std::sin(2.0 * t) - M_PI * std::sin(lat)) / d;
            }
            plane = QVector2D(float(-wrapPi(lon) * std::cos(t) / M_PI),
                              float(std::sin(t)));
            return true;
        }

        case Projection::PLATE_CARREE:
            plane = QVector2D(float(-wrapPi(lon) / M_PI),
                              float(lat / (M_PI / 2.0)));
            return true;

        case Projection::ORTHOGRAPHIC:
        {
            const double z = std::cos(lat) * std::cos(lon);
            if (z < 0.0)
                return false; // far side
            plane = QVector2D(float(-std::cos(lat) * std::sin(lon)),
                              float(std::sin(lat)));
            return true;
        }

        case Projection::AZIMUTHAL_EQUIDISTANT:
        {
            const double r = M_PI / 2.0 - lat; // colatitude
            plane          = QVector2D(float(-r * std::sin(lon) / M_PI),
                                       float(r * std::cos(lon) / M_PI));
            return true;
        }

        case Projection::GNOMONIC:
        {
            const double z = std::cos(lat) * std::cos(lon);
            if (z <= 1e-6)
                return false; // 90 degrees or more from the center
            plane = QVector2D(float(-std::cos(lat) * std::sin(lon) / z),
                              float(std::sin(lat) / z));
            return true;
        }
    }
    return false;
}

double
labelMeridian(Projection projection)
{
    switch (projection)
    {
        case Projection::MOLLWEIDE:
        case Projection::PLATE_CARREE:
            return M_PI; // the left edge
        case Projection::ORTHOGRAPHIC:
            return 80.0 * M_PI / 180.0; // near the left limb
        case Projection::AZIMUTHAL_EQUIDISTANT:
            return M_PI / 2.0; // the left side of the disk
        case Projection::GNOMONIC:
            return 35.0 * M_PI / 180.0; // left part of the visible area
    }
    return M_PI;
}
} // namespace projectionmath
