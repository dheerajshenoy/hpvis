// Multiplies the channels of one RGBA mask texture selected by the low 4 bits
// of `bits` (bit c = channel c enabled).
float maskProduct(sampler2D tex, ivec2 coord, int bits) {
    vec4 m = texelFetch(tex, coord, 0);
    float keep = 1.0;
    if ((bits & 1) != 0) keep *= m.r;
    if ((bits & 2) != 0) keep *= m.g;
    if ((bits & 4) != 0) keep *= m.b;
    if ((bits & 8) != 0) keep *= m.a;
    return keep;
}
