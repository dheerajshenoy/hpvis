#include "projectionmath.hpp"

#include <algorithm>
#include <cmath>

namespace projectionmath
{
namespace
{
constexpr double kSqrt2 = 1.41421356237309504880;

// Werner (Bonne with the pole as standard parallel) is a heart shape; the
// plane [-1, 1]^2 is its bounding box: half sizes and the vertical center, in
// the projection's own units (the north pole is at the origin)
constexpr double kWernerHalfX = 2.021610497074358;
constexpr double kWernerHalfY = 2.029609241037196;
constexpr double kWernerCenterY = -1.111983412552597;

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

bool
isDisk(Projection projection)
{
    switch (projection)
    {
        case Projection::ORTHOGRAPHIC:
        case Projection::AZIMUTHAL_EQUIDISTANT:
        case Projection::GNOMONIC:
        case Projection::STEREOGRAPHIC:
        case Projection::LAMBERT_AZIMUTHAL:
            return true;
        default:
            return false;
    }
}

double
aspect(Projection projection)
{
    switch (projection)
    {
        case Projection::MOLLWEIDE:
        case Projection::EQUIRECTANGULAR:
            return 2.0;
        case Projection::ORTHOGRAPHIC:
        case Projection::AZIMUTHAL_EQUIDISTANT:
            return 1.0;
        case Projection::GNOMONIC:
        case Projection::MERCATOR:
        case Projection::STEREOGRAPHIC:
            return 1.0;
        case Projection::SINUSOIDAL:
        case Projection::HAMMER:
        case Projection::AITOFF:
            return 2.0;
        case Projection::WERNER:
            return kWernerHalfX / kWernerHalfY;
        case Projection::LAMBERT_AZIMUTHAL:
            return 1.0;
        case Projection::LAMBERT_CYLINDRICAL:
            return M_PI;
        case Projection::GALL_PETERS:
            return M_PI / 2.0;
    }
    return 2.0;
}

bool
inverse(Projection projection, QVector2D plane, double &colat, double &phi,
        bool fromOutside)
{
    // The outside view is the inside one mirrored left-right
    const double px = fromOutside ? -plane.x() : plane.x(), py = plane.y();

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

        case Projection::EQUIRECTANGULAR:
            if (std::abs(px) > 1.0 || std::abs(py) > 1.0)
                return false;
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

        case Projection::MERCATOR:
            if (std::abs(px) > 1.0 || std::abs(py) > 1.0)
                return false;
            colat = M_PI / 2.0 - std::atan(std::sinh(py * M_PI));
            phi   = wrap2Pi(-px * M_PI);
            return true;

        case Projection::SINUSOIDAL:
        {
            const double lat = py * M_PI / 2.0;
            const double c   = std::cos(lat);
            const double x   = -px; // lon * cos(lat) / pi
            if (std::abs(x) > c)
                return false;
            colat = M_PI / 2.0 - lat;
            phi   = wrap2Pi(c > 1e-6 ? x * M_PI / c : 0.0);
            return true;
        }

        case Projection::LAMBERT_CYLINDRICAL:
        case Projection::GALL_PETERS:
            if (std::abs(px) > 1.0 || std::abs(py) > 1.0)
                return false;
            colat = M_PI / 2.0 - std::asin(std::clamp(py, -1.0, 1.0));
            phi   = wrap2Pi(-px * M_PI);
            return true;

        case Projection::STEREOGRAPHIC:
        {
            // Plane radius tan(c / 2): the 90 degree circle is the unit
            // circle, every plane point is valid
            const double r = std::hypot(px, py);
            const double c = 2.0 * std::atan(r);
            const double ux = r > 1e-9 ? px / r : 0.0;
            const double uy = r > 1e-9 ? py / r : 0.0;
            const double dx = std::cos(c), dy = -std::sin(c) * ux,
                         dz = std::sin(c) * uy;
            colat = std::atan2(std::hypot(dx, dy), dz);
            phi   = wrap2Pi(std::atan2(dy, dx));
            return true;
        }

        case Projection::HAMMER:
        {
            const double x = -px * 2.0 * kSqrt2;
            const double y = py * kSqrt2;
            if ((x * x / 8.0) + (y * y / 2.0) > 1.0)
                return false;
            const double z = std::sqrt(
                std::max(0.0, 1.0 - x * x / 16.0 - y * y / 4.0));
            const double lon
                = 2.0 * std::atan2(z * x, 2.0 * (2.0 * z * z - 1.0));
            colat = M_PI / 2.0 - std::asin(std::clamp(z * y, -1.0, 1.0));
            phi   = wrap2Pi(lon);
            return true;
        }

        case Projection::AITOFF:
        {
            // The azimuthal equidistant map of the hemisphere around the
            // center, with longitudes halved and x doubled: the distance
            // from the center is the angle c, up to 90 degrees
            const double r = std::hypot(px, py);
            if (r > 1.0)
                return false;
            const double c = r * M_PI / 2.0;
            const double s = c > 1e-6 ? std::sin(c) / c : 1.0;
            const double sinLat = std::clamp(py * M_PI / 2.0 * s, -1.0, 1.0);
            const double lon
                = 2.0 * std::atan2(-px * M_PI / 2.0 * s, std::cos(c));
            colat = M_PI / 2.0 - std::asin(sinLat);
            phi   = wrap2Pi(lon);
            return true;
        }

        case Projection::WERNER:
        {
            const double x   = -px * kWernerHalfX; // longitude increases left
            const double y   = py * kWernerHalfY + kWernerCenterY;
            const double rho = std::hypot(x, y); // colatitude
            if (rho > M_PI)
                return false;
            const double e = std::atan2(x, -y);
            const double s = std::sin(rho);
            const double lon = s > 1e-6 ? e * rho / s : 0.0;
            if (std::abs(lon) > M_PI)
                return false; // outside the heart
            colat = rho;
            phi   = wrap2Pi(lon);
            return true;
        }

        case Projection::LAMBERT_AZIMUTHAL:
        {
            // Plane radius sin(c / 2): the whole sphere fits the unit circle
            const double r = std::hypot(px, py);
            if (r > 1.0)
                return false;
            const double c  = 2.0 * std::asin(r);
            const double ux = r > 1e-9 ? px / r : 0.0;
            const double uy = r > 1e-9 ? py / r : 0.0;
            const double dx = std::cos(c), dy = -std::sin(c) * ux,
                         dz = std::sin(c) * uy;
            colat = std::atan2(std::hypot(dx, dy), dz);
            phi   = wrap2Pi(std::atan2(dy, dx));
            return true;
        }
    }
    return false;
}

static bool
forwardInside(Projection projection, double lat, double lon, QVector2D &plane)
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

        case Projection::EQUIRECTANGULAR:
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

        case Projection::MERCATOR:
        {
            const double y = std::asinh(std::tan(lat)) / M_PI;
            if (!std::isfinite(y) || std::abs(y) > 1.0)
                return false; // beyond about 85 degrees latitude
            plane = QVector2D(float(-wrapPi(lon) / M_PI), float(y));
            return true;
        }

        case Projection::SINUSOIDAL:
            plane = QVector2D(float(-wrapPi(lon) * std::cos(lat) / M_PI),
                              float(lat / (M_PI / 2.0)));
            return true;

        case Projection::LAMBERT_CYLINDRICAL:
        case Projection::GALL_PETERS:
            plane = QVector2D(float(-wrapPi(lon) / M_PI),
                              float(std::sin(lat)));
            return true;

        case Projection::STEREOGRAPHIC:
        {
            const double z = std::cos(lat) * std::cos(lon);
            if (z <= -1.0 + 1e-6)
                return false; // the antipode, at infinity
            plane = QVector2D(float(-std::cos(lat) * std::sin(lon) / (1.0 + z)),
                              float(std::sin(lat) / (1.0 + z)));
            return true;
        }

        case Projection::HAMMER:
        {
            const double l = wrapPi(lon);
            const double d
                = std::sqrt(1.0 + std::cos(lat) * std::cos(l / 2.0));
            // Hammer's x / (2 sqrt 2) and y / sqrt 2
            plane = QVector2D(float(-std::cos(lat) * std::sin(l / 2.0) / d),
                              float(std::sin(lat) / d));
            return true;
        }

        case Projection::AITOFF:
        {
            const double l = wrapPi(lon);
            const double a
                = std::acos(std::clamp(std::cos(lat) * std::cos(l / 2.0),
                                       -1.0, 1.0));
            const double k = a > 1e-6 ? a / std::sin(a) : 1.0;
            // x / pi and y / (pi / 2)
            plane = QVector2D(
                float(-2.0 * std::cos(lat) * std::sin(l / 2.0) * k / M_PI),
                float(std::sin(lat) * k / (M_PI / 2.0)));
            return true;
        }

        case Projection::WERNER:
        {
            const double l   = wrapPi(lon);
            const double rho = M_PI / 2.0 - lat;
            const double e   = rho > 1e-9 ? l * std::cos(lat) / rho : 0.0;
            const double x   = rho * std::sin(e);
            const double y   = -rho * std::cos(e);
            plane = QVector2D(float(-x / kWernerHalfX),
                              float((y - kWernerCenterY) / kWernerHalfY));
            return true;
        }

        case Projection::LAMBERT_AZIMUTHAL:
        {
            const double z = std::cos(lat) * std::cos(lon);
            if (z <= -1.0 + 1e-9)
                return false; // the antipode: a whole circle
            const double k = 1.0 / std::sqrt(2.0 * (1.0 + z));
            plane = QVector2D(float(-std::cos(lat) * std::sin(lon) * k),
                              float(std::sin(lat) * k));
            return true;
        }
    }
    return false;
}

bool
forward(Projection projection, double lat, double lon, QVector2D &plane,
        bool fromOutside)
{
    if (!forwardInside(projection, lat, lon, plane))
        return false;
    if (fromOutside)
        plane.setX(-plane.x());
    return true;
}

static double
labelMeridianInside(Projection projection);

double
labelMeridian(Projection projection, bool fromOutside)
{
    // Mirroring the picture is the same as flipping the sign of longitude
    const double m = labelMeridianInside(projection);
    return fromOutside ? -m : m;
}

static double
labelMeridianInside(Projection projection)
{
    switch (projection)
    {
        case Projection::MOLLWEIDE:
        case Projection::EQUIRECTANGULAR:
            return M_PI; // the left edge
        case Projection::ORTHOGRAPHIC:
            return 80.0 * M_PI / 180.0; // near the left limb
        case Projection::AZIMUTHAL_EQUIDISTANT:
            return M_PI / 2.0; // the left side of the disk
        case Projection::GNOMONIC:
            return 35.0 * M_PI / 180.0; // left part of the visible area
        case Projection::MERCATOR:
        case Projection::SINUSOIDAL:
        case Projection::LAMBERT_CYLINDRICAL:
        case Projection::GALL_PETERS:
        case Projection::HAMMER:
        case Projection::AITOFF:
        case Projection::WERNER:
            return M_PI; // the left edge
        case Projection::LAMBERT_AZIMUTHAL:
            return M_PI / 2.0; // the left side of the disk
        case Projection::STEREOGRAPHIC:
            return 60.0 * M_PI / 180.0; // left part of the visible area
    }
    return M_PI;
}
} // namespace projectionmath
