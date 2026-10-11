#include "skyframe.hpp"

#include <array>
#include <cmath>

namespace skyframe
{
namespace
{
using M3 = std::array<double, 9>; // row-major

// Equatorial (J2000) -> Galactic, from the Hipparcos definition
constexpr M3 kEquatorialToGalactic
    = {-0.0548755604162154, -0.8734370902348850, -0.4838350155487132,
       +0.4941094278755837, -0.4448296299600112, +0.7469822444972189,
       -0.8676661490190047, -0.1980763734312015, +0.4559837761750669};

M3
transpose(const M3 &m)
{
    return {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
}

M3
multiply(const M3 &a, const M3 &b)
{
    M3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                r[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
    return r;
}

// Equatorial -> Ecliptic: a rotation about x by the obliquity (J2000)
M3
equatorialToEcliptic()
{
    const double eps = 23.4392911 * M_PI / 180.0;
    const double c = std::cos(eps), s = std::sin(eps);
    return {1, 0, 0, 0, c, s, 0, -s, c};
}

M3
toEquatorial(SkyFrame f)
{
    switch (f)
    {
        case SkyFrame::Galactic:
            return transpose(kEquatorialToGalactic);
        case SkyFrame::Ecliptic:
            return transpose(equatorialToEcliptic());
        case SkyFrame::Equatorial:
            break;
    }
    return {1, 0, 0, 0, 1, 0, 0, 0, 1};
}

M3
fromEquatorial(SkyFrame f)
{
    return transpose(toEquatorial(f));
}
} // namespace

QGenericMatrix<3, 3, float>
rotation(SkyFrame from, SkyFrame to)
{
    const M3 m = multiply(fromEquatorial(to), toEquatorial(from));
    float v[9];
    for (int i = 0; i < 9; ++i)
        v[i] = float(m[i]);
    return QGenericMatrix<3, 3, float>(v); // row-major
}

QVector3D
apply(const QGenericMatrix<3, 3, float> &m, const QVector3D &v)
{
    return QVector3D(m(0, 0) * v.x() + m(0, 1) * v.y() + m(0, 2) * v.z(),
                     m(1, 0) * v.x() + m(1, 1) * v.y() + m(1, 2) * v.z(),
                     m(2, 0) * v.x() + m(2, 1) * v.y() + m(2, 2) * v.z());
}

SkyFrame
fromCoordSys(const QString &coordsys)
{
    const QString c = coordsys.trimmed().toUpper();
    if (c.startsWith('E'))
        return SkyFrame::Ecliptic;
    if (c.startsWith('C') || c.startsWith('Q') || c.startsWith("EQ")
        || c.startsWith("ICRS") || c.startsWith("J2000"))
        return SkyFrame::Equatorial;
    return SkyFrame::Galactic;
}

SkyFrame
displayFrame(Orientation orientation, SkyFrame mapFrame)
{
    switch (orientation)
    {
        case Orientation::Galactic:
            return SkyFrame::Galactic;
        case Orientation::Ecliptic:
            return SkyFrame::Ecliptic;
        case Orientation::Equatorial:
            return SkyFrame::Equatorial;
        case Orientation::Free:
            break;
    }
    return mapFrame;
}

QString
name(SkyFrame f)
{
    switch (f)
    {
        case SkyFrame::Galactic:
            return "Galactic";
        case SkyFrame::Ecliptic:
            return "Ecliptic";
        case SkyFrame::Equatorial:
            return "Equatorial";
    }
    return {};
}

QString
lonName(SkyFrame f)
{
    switch (f)
    {
        case SkyFrame::Galactic:
            return "l";
        case SkyFrame::Ecliptic:
            return QString::fromUtf8("λ");
        case SkyFrame::Equatorial:
            return "RA";
    }
    return "Lon";
}

QString
latName(SkyFrame f)
{
    switch (f)
    {
        case SkyFrame::Galactic:
            return "b";
        case SkyFrame::Ecliptic:
            return QString::fromUtf8("β");
        case SkyFrame::Equatorial:
            return "Dec";
    }
    return "Lat";
}
} // namespace skyframe
