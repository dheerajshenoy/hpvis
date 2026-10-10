// Multiplies the channels of one RGBA mask texture selected by the low 4 bits
// of `bits` (bit c = channel c enabled). Channels whose bit is set in `inv`
// are inverted (1 - m) first.
float maskProduct(sampler2D tex, ivec2 coord, int bits, int inv) {
    vec4 m = texelFetch(tex, coord, 0);
    if ((inv & 1) != 0) m.r = 1.0 - m.r;
    if ((inv & 2) != 0) m.g = 1.0 - m.g;
    if ((inv & 4) != 0) m.b = 1.0 - m.b;
    if ((inv & 8) != 0) m.a = 1.0 - m.a;

    float keep = 1.0;
    if ((bits & 1) != 0) keep *= m.r;
    if ((bits & 2) != 0) keep *= m.g;
    if ((bits & 4) != 0) keep *= m.b;
    if ((bits & 8) != 0) keep *= m.a;
    return keep;
}
