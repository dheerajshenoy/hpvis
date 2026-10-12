// Direction -> (ix, iy, face) of the containing pixel. The map is stored as 12
// face textures of nside x nside texels, so no pixel number (and hence no
// RING/NEST distinction) is needed to look a value up.
ivec3 ang2xyf(int ns, float theta, float phi) {
    float nside_val = float(ns);
    float z = cos(theta);
    float za = abs(z);
    float tt = mod(phi, 2.0 * PI) / (0.5 * PI); // [0, 4)

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

    return ivec3(ix, iy, face_num);
}
