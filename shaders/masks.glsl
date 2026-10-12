// Multiplies the channels of one RGBA mask texture selected by the low 4 bits
// of `bits` (bit c = channel c enabled). Channels whose bit is set in `inv`
// are inverted (1 - m) first.
// Value of the 4 masks of group g at a texel; the face (t.z) picks the segment
vec4 fetchMask0(ivec3 t) {
    int seg = t.z / seg_layers;
    ivec3 c = ivec3(t.xy, t.z - seg * seg_layers);
    if (seg == 0) return texelFetch(maskTex0_0, c, 0);
    if (seg == 1) return texelFetch(maskTex0_1, c, 0);
    return texelFetch(maskTex0_2, c, 0);
}

vec4 fetchMask1(ivec3 t) {
    int seg = t.z / seg_layers;
    ivec3 c = ivec3(t.xy, t.z - seg * seg_layers);
    if (seg == 0) return texelFetch(maskTex1_0, c, 0);
    if (seg == 1) return texelFetch(maskTex1_1, c, 0);
    return texelFetch(maskTex1_2, c, 0);
}

vec4 fetchMask2(ivec3 t) {
    int seg = t.z / seg_layers;
    ivec3 c = ivec3(t.xy, t.z - seg * seg_layers);
    if (seg == 0) return texelFetch(maskTex2_0, c, 0);
    if (seg == 1) return texelFetch(maskTex2_1, c, 0);
    return texelFetch(maskTex2_2, c, 0);
}

vec4 fetchMask3(ivec3 t) {
    int seg = t.z / seg_layers;
    ivec3 c = ivec3(t.xy, t.z - seg * seg_layers);
    if (seg == 0) return texelFetch(maskTex3_0, c, 0);
    if (seg == 1) return texelFetch(maskTex3_1, c, 0);
    return texelFetch(maskTex3_2, c, 0);
}

float maskProduct(vec4 m, int bits, int inv) {
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
