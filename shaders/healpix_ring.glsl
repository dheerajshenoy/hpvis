int ang2pix_ring(int ns, float theta, float phi) {
    float nside_val = float(ns);
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
    return clamp(pix, 0, npix_i - 1);
}
