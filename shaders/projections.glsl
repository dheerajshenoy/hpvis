const int PROJ_MOLLWEIDE = 0;
const int PROJ_PLATE_CARREE = 1;
const int PROJ_ORTHOGRAPHIC = 2;
const int PROJ_AZIMUTHAL_EQUIDISTANT = 3;

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

// This is the simplest projection: longitude and latitude map linearly to screen coordinates. The map is rectangular, but it distorts areas substantially near the poles.
bool inversePlateCarree(
    vec2 uv,
    out float colat,
    out float phi
) {
    float lon = -(uv.x - 0.5) * 2.0 * PI;
    float lat = (0.5 - uv.y) * PI;

    colat = PI / 2.0 - lat;
    phi = mod(lon, 2.0 * PI);

    return true;
}

// This produces a circular, globe-like view of one hemisphere. Pixels outside the circle are invalid.
bool inverseOrthographic(
    vec2 uv,
    out float colat,
    out float phi
) {
    float x = (uv.x - 0.5) * 2.0;
    float y = (0.5 - uv.y) * 2.0;

    float r2 = x*x + y*y;

    colat = 0.0;
    phi = 0.0;

    if (r2 > 1.0)
        return false;

    float z = sqrt(max(0.0, 1.0 - r2));

    colat = acos(clamp(y, -1.0, 1.0));
    phi = atan(x, z);

    // This is a local view, not a full-sphere longitude map.
    // Use the local spherical direction for your HEALPix lookup.
    return true;
}

bool inverseAzimuthalEquidistant(
    vec2 uv,
    out float colat,
    out float phi
) {
    float x = (uv.x - 0.5) * 2.0 * PI;
    float y = (0.5 - uv.y) * 2.0 * PI;

    float r = length(vec2(x, y));

    colat = 0.0;
    phi = 0.0;

    if (r > PI)
        return false;

    colat = r;
    phi = atan(x, y);

    phi = mod(phi, 2.0 * PI);

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
