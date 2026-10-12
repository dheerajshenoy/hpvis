const int PROJ_MOLLWEIDE = 0;
const int PROJ_EQUIRECTANGULAR = 1;
const int PROJ_ORTHOGRAPHIC = 2;
const int PROJ_AZIMUTHAL_EQUIDISTANT = 3;
const int PROJ_GNOMONIC = 4;
const int PROJ_MERCATOR = 5;
const int PROJ_SINUSOIDAL = 6;
const int PROJ_LAMBERT_CYLINDRICAL = 7;
const int PROJ_GALL_PETERS = 8;
const int PROJ_STEREOGRAPHIC = 9;
const int PROJ_HAMMER = 10;

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
bool inverseEquirectangular(
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

// Tangent-plane projection centered on longitude 0 / latitude 0 (rotate to look
// elsewhere): great circles are straight lines. The plane edge is 45 degrees
// from the center, so the plane coordinates are plain tangents (tan 45 = 1).
// Every point of the plane is valid. The CPU copy is in projectionmath.cpp.
bool inverseGnomonic(
    vec2 uv,
    out float colat,
    out float phi
) {
    float x = (uv.x - 0.5) * 2.0;
    float y = (uv.y - 0.5) * 2.0;

    vec3 dir = normalize(vec3(1.0, -x, y)); // -x: longitude increases left
    colat = atan(length(dir.xy), dir.z);
    phi   = mod(atan(dir.y, dir.x), 2.0 * PI);

    return true;
}

// Mercator, cut at latitude +-85.05 degrees so the plane is a square:
// y = ln(tan(pi/4 + lat/2)) = pi at the edge. Conformal, areas grow to the poles.
bool inverseMercator(
    vec2 uv,
    out float colat,
    out float phi
) {
    float lon = -(uv.x - 0.5) * 2.0 * PI;
    float y = (uv.y - 0.5) * 2.0 * PI;
    float lat = atan(sinh(y));

    colat = PI / 2.0 - lat;
    phi = mod(lon, 2.0 * PI);

    return true;
}

// Equal-area, parallels are straight lines and meridians are curves that
// squeeze together toward the poles. Valid inside the sine-shaped outline.
bool inverseSinusoidal(
    vec2 uv,
    out float colat,
    out float phi
) {
    float x = -(uv.x - 0.5) * 2.0;  // lon * cos(lat) / pi
    float lat = (uv.y - 0.5) * PI;
    float c = cos(lat);

    colat = 0.0;
    phi = 0.0;

    if (abs(x) > c)
        return false;

    float lon = (c > 1e-6) ? x * PI / c : 0.0;
    colat = PI / 2.0 - lat;
    phi = mod(lon, 2.0 * PI);

    return true;
}

// Cylindrical equal-area: x = lon, y = sin(lat). Gall-Peters is the same
// mapping drawn with a different aspect ratio (standard parallels at 45).
bool inverseLambertCylindrical(
    vec2 uv,
    out float colat,
    out float phi
) {
    float lon = -(uv.x - 0.5) * 2.0 * PI;
    float s = (uv.y - 0.5) * 2.0;

    colat = PI / 2.0 - asin(clamp(s, -1.0, 1.0));
    phi = mod(lon, 2.0 * PI);

    return true;
}

// Conformal map of the sphere onto a plane centered on longitude 0 / latitude
// 0 (rotate to look elsewhere). Circles stay circles. The plane edge is 90
// degrees from the center, so the plane radius is tan(colatitude / 2).
bool inverseStereographic(
    vec2 uv,
    out float colat,
    out float phi
) {
    float x = (uv.x - 0.5) * 2.0;
    float y = (uv.y - 0.5) * 2.0;

    float c = 2.0 * atan(length(vec2(x, y)));   // angle from the center
    float r = length(vec2(x, y));
    vec2 d = (r > 1e-9) ? vec2(x, y) / r : vec2(0.0);

    vec3 dir = vec3(cos(c), -sin(c) * d.x, sin(c) * d.y); // -x: longitude increases left
    colat = atan(length(dir.xy), dir.z);
    phi   = mod(atan(dir.y, dir.x), 2.0 * PI);

    return true;
}

// Hammer (Hammer-Aitoff): equal-area ellipse with the same outline as
// Mollweide but curved parallels.
bool inverseHammer(
    vec2 uv,
    out float colat,
    out float phi
) {
    float x = -(uv.x - 0.5) * 2.0 * 2.82842712;
    float y = (uv.y - 0.5) * 2.0 * 1.41421356;

    colat = 0.0;
    phi = 0.0;

    if ((x * x / 8.0) + (y * y / 2.0) > 1.0)
        return false;

    float z = sqrt(max(0.0, 1.0 - x * x / 16.0 - y * y / 4.0));
    float lon = 2.0 * atan(z * x, 2.0 * (2.0 * z * z - 1.0));
    float lat = asin(clamp(z * y, -1.0, 1.0));

    colat = PI / 2.0 - lat;
    phi = mod(lon, 2.0 * PI);

    return true;
}

bool inverseProjection(
    vec2 uv,
    out float colat,
    out float phi
) {
    // The outside view is the inside one mirrored left-right
    if (view_outside)
        uv.x = 1.0 - uv.x; // uv is in [0, 1], centered on 0.5

    if (projection_type == PROJ_MOLLWEIDE)
        return inverseMollweide(uv, colat, phi);

    if (projection_type == PROJ_EQUIRECTANGULAR)
        return inverseEquirectangular(uv, colat, phi);

    if (projection_type == PROJ_ORTHOGRAPHIC)
        return inverseOrthographic(uv, colat, phi);

    if (projection_type == PROJ_AZIMUTHAL_EQUIDISTANT)
        return inverseAzimuthalEquidistant(
            uv, colat, phi
        );

    if (projection_type == PROJ_GNOMONIC)
        return inverseGnomonic(uv, colat, phi);

    if (projection_type == PROJ_MERCATOR)
        return inverseMercator(uv, colat, phi);

    if (projection_type == PROJ_SINUSOIDAL)
        return inverseSinusoidal(uv, colat, phi);

    if (projection_type == PROJ_LAMBERT_CYLINDRICAL
        || projection_type == PROJ_GALL_PETERS)
        return inverseLambertCylindrical(uv, colat, phi);

    if (projection_type == PROJ_STEREOGRAPHIC)
        return inverseStereographic(uv, colat, phi);

    if (projection_type == PROJ_HAMMER)
        return inverseHammer(uv, colat, phi);

    return false;
}
