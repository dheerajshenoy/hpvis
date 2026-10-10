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
