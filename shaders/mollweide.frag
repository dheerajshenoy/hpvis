#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D mapTexture;
uniform float nside;
uniform float npix;
uniform float vmin;
uniform float vmax;
uniform int pixel_ordering;

#define PI 3.14159265358979323846

float ang2pix_ring(float nside_val, float theta, float phi) {
    int ns = int(nside_val + 0.5);
    float z = cos(theta);
    float za = abs(z);
    float tt = mod(phi, 2.0 * PI) / (0.5 * PI); // [0,4)
    int npix_i = 12 * ns * ns;
    int ncap = 2 * ns * (ns - 1);
    int pix;

    if (za <= 2.0 / 3.0) { // equatorial belt
        float temp1 = nside_val * (0.5 + tt);
        float temp2 = nside_val * z * 0.75;
        int jp = int(temp1 - temp2);
        int jm = int(temp1 + temp2);
        int ir = ns + 1 + jp - jm;
        int kshift = 1 - (ir & 1);
        int ip = (jp + jm - ns + kshift + 1) / 2;
        ip = ip % (4 * ns);
        if (ip < 0) ip += 4 * ns;
        pix = ncap + (ir - 1) * 4 * ns + ip;
    } else { // polar caps
        float tp = tt - floor(tt);
        float tmp = nside_val * sqrt(3.0 * (1.0 - za));
        int jp = int(tp * tmp);
        int jm = int((1.0 - tp) * tmp);
        int ir = jp + jm + 1;
        int ip = int(tt * float(ir));
        ip = ip % (4 * ir);
        if (z > 0.0) pix = 2 * ir * (ir - 1) + ip;
        else         pix = npix_i - 2 * ir * (ir + 1) + ip;
    }
    return float(clamp(pix, 0, npix_i - 1));
}

float ang2pix_nest(float nside_val, float theta, float phi) {
    int ns = int(nside_val + 0.5);
    float z = cos(theta);
    float za = abs(z);
    float tt = mod(phi, 2.0 * PI) / (0.5 * PI); // [0, 4)
    int npix_i = 12 * ns * ns;

    int face_num, ix, iy;

    if (za <= 2.0 / 3.0) { // equatorial belt
        float temp1 = nside_val * (0.5 + tt);
        float temp2 = nside_val * z * 0.75;
        int jp = int(temp1 - temp2); // ascending edge line index
        int jm = int(temp1 + temp2); // descending edge line index
        int ifp = jp / ns;
        int ifm = jm / ns;
        if (ifp == ifm)     face_num = ifp | 4;
        else if (ifp < ifm) face_num = ifp;
        else                face_num = ifm + 8;
        ix = jm & (ns - 1);
        iy = ns - (jp & (ns - 1)) - 1;
    } else { // polar caps
        int ntt = min(int(tt), 3);
        float tp = tt - float(ntt);
        float tmp = nside_val * sqrt(3.0 * (1.0 - za));
        int jp = min(int(tp * tmp), ns - 1);
        int jm = min(int((1.0 - tp) * tmp), ns - 1);
        if (z >= 0.0) {
            face_num = ntt;
            ix = ns - jm - 1;
            iy = ns - jp - 1;
        } else {
            face_num = ntt + 8;
            ix = jp;
            iy = jm;
        }
    }

    // Interleave bits: ix -> even bits, iy -> odd bits
    int pix_within_face = 0;
    for (int i = 0; i < 15; i++) {
        pix_within_face |= ((ix >> i) & 1) << (2 * i);
        pix_within_face |= ((iy >> i) & 1) << (2 * i + 1);
    }

    int pix = face_num * ns * ns + pix_within_face;
    return float(clamp(pix, 0, npix_i - 1));
}

void main() {
    // 1. Scale UV coordinates to Mollweide plane dimensions
    float x_range = 2.82842712; // 2 * sqrt(2)
    float y_range = 1.41421356; // sqrt(2)

    // Longitude increases to the left (healpy/astronomical convention)
    float x = -(TexCoord.x - 0.5) * 2.0 * x_range;
    float y = (TexCoord.y - 0.5) * 2.0 * y_range;

    // 2. Check ellipse boundary
    if ((x*x / 8.0) + (y*y / 2.0) > 1.0) {
        FragColor = vec4(0.04, 0.04, 0.06, 1.0); // Background outside
        return;
    }

    // 3. Inverse Mollweide Projection -> spherical coords (lat, lon)
    float sinTheta = clamp(y / 1.41421356, -1.0, 1.0);
    float theta_aux = asin(sinTheta);
    float lat = asin(clamp((2.0 * theta_aux + sin(2.0 * theta_aux)) / PI, -1.0, 1.0));

    float cosTheta = cos(theta_aux);
    float lon = 0.0;
    if (abs(cosTheta) > 0.0001) {
        lon = (PI * x) / (2.82842712 * cosTheta);
    }
    lon = mod(lon, 2.0 * PI);
    if (lon < 0.0) lon += 2.0 * PI;

    // Convert latitude/longitude to HEALPix colatitude (theta) and azimuth (phi)
    float colat = PI / 2.0 - lat;
    float phi = lon;

    float pixIdx;
    // 4. Look up pixel index using HEALPix scheme
    if (pixel_ordering == 0) {
        pixIdx = ang2pix_ring(nside, colat, phi);
    } else {
        pixIdx = ang2pix_nest(nside, colat, phi);
    }

    float W = 4096.0;
    float H = ceil(npix / W);
    vec2 uv = vec2((mod(pixIdx, W) + 0.5) / W, (floor(pixIdx / W) + 0.5) / H);
    float mapVal = texture(mapTexture, uv).r;
    float t = clamp((mapVal - vmin) / (vmax - vmin), 0.0, 1.0);
    vec3 c = t < 0.5 ? mix(vec3(0.0, 0.0, 1.0), vec3(1.0), t * 2.0)
                     : mix(vec3(1.0), vec3(1.0, 0.0, 0.0), (t - 0.5) * 2.0);
    FragColor = vec4(c, 1.0);
}
