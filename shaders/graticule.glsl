uniform bool  graticule_enabled;
uniform float graticule_step;   // radians
uniform vec4  graticule_color;  // rgba

// 1 on a line, 0 away from it, antialiased over ~1.5 px. x is in units of
// graticule steps, so lines sit at integers.
float gridLine(float x, float w) {
    float d = abs(fract(x - 0.5) - 0.5);
    return 1.0 - smoothstep(0.0, w * 1.5, d);
}

float graticule(float colat, float phi) {
    float lat = PI * 0.5 - colat;
    float a   = lat / graticule_step;
    float b   = phi / graticule_step;

    // phi wraps at 0 / 2*pi, which makes fwidth(b) spike on the seam; the
    // shifted copy has its jump elsewhere, so take the smaller derivative
    float wb = min(fwidth(b), fwidth(fract(b + 0.5)));
    return max(gridLine(a, fwidth(a)), gridLine(b, wb));
}
