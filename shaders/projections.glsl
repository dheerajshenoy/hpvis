const int PROJ_MOLLWEIDE = 0;
const int PROJ_PLATE_CARREE = 1;
const int PROJ_ORTHOGRAPHIC = 2;
const int PROJ_AZIMUTHAL_EQUIDISTANT = 3;

uniform mat3 globe_rotation;
uniform int projection_type;

// Inverse Mollweide: normalized screen coords uv in [0,1]^2 -> HEALPix
// colatitude / azimuth. Returns false outside the ellipse.
bool inverseMollweide(vec2 uv, out float colat, out float phi) {
    float x_range = 2.82842712; // 2 * sqrt(2)
    float y_range = 1.41421356; // sqrt(2)

    // Longitude increases to the left (healpy/astronomical convention)
    float x = -(uv.x - 0.5) * 2.0 * x_range;
    float y = (uv.y - 0.5) * 2.0 * y_range;

    colat = 0.0;
    phi   = 0.0;
    if ((x * x / 8.0) + (y * y / 2.0) > 1.0)
        return false;

    float theta_aux = asin(clamp(y / 1.41421356, -1.0, 1.0));
    float lat = asin(clamp((2.0 * theta_aux + sin(2.0 * theta_aux)) / PI, -1.0, 1.0));

    float cosTheta = cos(theta_aux);
    float lon = 0.0;
    if (abs(cosTheta) > 0.0001)
        lon = (PI * x) / (2.82842712 * cosTheta);
    lon = mod(lon, 2.0 * PI);
    if (lon < 0.0) lon += 2.0 * PI;

    colat = PI / 2.0 - lat;
    phi   = lon;
    return true;
}

// Every projection maps the plane [-1,1]^2 (here as uv in [0,1]^2, v up, so
// north is up) to a point on the sky. Longitude increases to the left in all
// of them, like healpy. The CPU copy in src/projectionmath.cpp must match.

// Longitude and latitude map linearly to screen coordinates (2:1 plane). The
// map is rectangular, but areas are distorted substantially near the poles.
bool inversePlateCarree(
    vec2 uv,
    out float colat,
    out float phi
) {
    float lon = -(uv.x - 0.5) * 2.0 * PI;
    float lat = (uv.y - 0.5) * PI;

    colat = PI / 2.0 - lat;
    phi = mod(lon, 2.0 * PI);

    return true;
}

// A globe seen from outside, centered on longitude 0 / latitude 0. Only the
// near hemisphere is visible; pixels outside the circle are invalid.
bool inverseOrthographic(
    vec2 uv,
    out float colat,
    out float phi
) {
    float x = (uv.x - 0.5) * 2.0;
    float y = (uv.y - 0.5) * 2.0;

    float r2 = x*x + y*y;

    colat = 0.0;
    phi = 0.0;

    if (r2 > 1.0)
        return false;

    float z = sqrt(max(0.0, 1.0 - r2)); // toward the viewer

    colat = acos(clamp(y, -1.0, 1.0));
    phi = mod(atan(-x, z), 2.0 * PI);   // -x: longitude increases to the left

    return true;
}

// The whole sphere as a disk centered on the north pole: distance from the
// center is the colatitude, the direction is the longitude.
bool inverseAzimuthalEquidistant(
    vec2 uv,
    out float colat,
    out float phi
) {
    float x = (uv.x - 0.5) * 2.0 * PI;
    float y = (uv.y - 0.5) * 2.0 * PI;

    float r = length(vec2(x, y));

    colat = 0.0;
    phi = 0.0;

    if (r > PI)
        return false;

    colat = r;
    phi = mod(atan(-x, y), 2.0 * PI);   // 0 at the top, counter-clockwise

    return true;
}

bool inverseProjection(
    vec2 uv,
    out float colat,
    out float phi
) {
    if (projection_type == PROJ_MOLLWEIDE)
        return inverseMollweide(uv, colat, phi);

    if (projection_type == PROJ_PLATE_CARREE)
        return inversePlateCarree(uv, colat, phi);

    if (projection_type == PROJ_ORTHOGRAPHIC)
        return inverseOrthographic(uv, colat, phi);

    if (projection_type == PROJ_AZIMUTHAL_EQUIDISTANT)
        return inverseAzimuthalEquidistant(
            uv, colat, phi
        );

    return false;
}
