uniform bool  graticule_enabled;
uniform float graticule_step;   // radians
uniform vec4  graticule_color;  // rgba

// 1 on a line, 0 away from it, antialiased over ~1.5 px. x is in units of
// graticule steps, so lines sit at integers; w is the change of x per pixel.
// Where the lines get closer than about 2 px (a projection's edge, near the
// poles) they fade out instead of turning into a smear.
float gridLine(float x, float w) {
    float d    = abs(fract(x - 0.5) - 0.5);
    float line = 1.0 - smoothstep(0.0, w * 1.5, d);
    return line * (1.0 - smoothstep(0.25, 0.5, w));
}

// dLat / dPhi: how much the latitude / longitude change over one screen pixel
// (radians), measured explicitly: fwidth() is wrong at the edge of a shape
// where the neighbouring pixels are outside the projection.
float graticule(float colat, float phi, float dLat, float dPhi) {
    float a = (PI * 0.5 - colat) / graticule_step;
    float b = phi / graticule_step;
    return max(gridLine(a, dLat / graticule_step),
               gridLine(b, dPhi / graticule_step));
}
