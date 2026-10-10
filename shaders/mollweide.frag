#version 330 core
out vec4 FragColor;
in vec2 TexCoord; // [0, 1] range

uniform sampler2D u_mapTexture;
uniform long u_nside;
uniform long u_npix;

#define PI 3.14159265358979323846

// Function to compute HEALPix RING pixel index from celestial coordinates (theta, phi)
int ang2pix_ring(float theta, float phi) {
    float nside_f = float(u_nside);
    float z = cos(theta);
    float za = abs(z);

    float 2d_fact = 3.0 * nside_f * nside_f;
    int pix;

    if (za > (2.0 / 3.0)) { // --- Polar Caps ---
        float temp = nside_f * sqrt(3.0 * (1.0 - za));
        int jp = int(floor(nside_f * (1.0 - sqrt(3.0 * (1.0 - za))))); // ring index
        int j = int(floor(temp));

        // Safety clamps
        j = clamp(j, 0, int(u_nside) - 1);

        float fa = (phi / (0.5 * PI)); // phi in [0, 2pi] mapped to sectors
        // Additional polar cap sector geometry math...
        // (Standard HEALPix analytical formula branch)

        if (z > 0.0) {
            int ring = int(floor(nside_f * sqrt(3.0 * (1.0 - z))));
            float cf = phi / (0.5 * PI);
            int jp = int(floor(nside_f * (1.0 - sqrt(3.0 * (1.0 - z)))));
            // North cap formulation
            pix = 2 * ring * (ring + 1); // simplified tracking
        } else {
            // South cap formulation
            int ring = int(floor(nside_f * sqrt(3.0 * (1.0 - za))));
            pix = u_npix - 2 * ring * (ring + 1);
        }
    } else { // --- Equatorial Belt ---
        float temp = nside_f * (0.5 + z);
        int jp = int(floor(nside_f * (0.5 + z)));
        int j = int(floor(nside_f * (0.75 - z))); // ring index

        float s = mod(phi, 2.0 * PI) / (0.5 * PI);
        int ip = int(floor(nside_f * (0.5 + z + 0.75))); // placeholder sector

        // Base equatorial index computation
        int ring_idx = int(floor(nside_f * (0.75 - z)));
        float dphi = phi - (floor(phi / (0.5 * PI)) * 0.5 * PI);

        // Final ring formula mapping for equatorial belt
        pix = int(2 * nside_f * (nside_f + 1) + (ring_idx - int(u_nside)) * 4 * nside_f);
    }

    return clamp(pix, 0, u_npix - 1);
}

void main() {
    // 1. Map texture coordinates to Mollweide plane coordinates (x, y)
    float x_range = 2.82842712; // 2 * sqrt(2)
    float y_range = 1.41421356; // sqrt(2)

    float x = (TexCoord.x - 0.5) * 2.0 * x_range;
    float y = (TexCoord.y - 0.5) * 2.0 * y_range;

    // 2. Check ellipse boundary
    if ((x*x / 8.0) + (y*y / 2.0) > 1.0) {
        FragColor = vec4(0.05, 0.05, 0.07, 1.0); // Outside canvas background
        return;
    }

    // 3. Newton-Raphson iteration for Mollweide authalic angle theta
    float theta_aux = y;
    for (int i = 0; i < 6; ++i) {
        theta_aux -= (2.0 * theta_aux + sin(2.0 * theta_aux) - PI * y / y_range) / (2.0 + 2.0 * cos(2.0 * theta_aux));
    }
    theta_aux = clamp(theta_aux, -PI/2.0, PI/2.0);

    // 4. Extract sky coordinates
    float theta_sky = asin(clamp(2.0 * theta_aux / PI, -1.0, 1.0));
    theta_sky = PI / 2.0 - theta_sky; // Colatitude [0, pi]

    float phi_sky = x / (2.0 * sqrt(2.0) * cos(theta_aux));
    phi_sky = mod(phi_sky * PI + PI, 2.0 * PI); // Longitude [0, 2pi]

    // 5. Get pixel index and fetch from 1D texture buffer
    int pix_idx = ang2pix_ring(theta_sky, phi_sky);
    float val = texelFetch(u_mapTexture, pix_idx).r;

    // 6. Simple stretch / colormap hook (e.g., grayscale or mock scaling)
    float normalized_val = clamp((val + 0.001) / 0.005, 0.0, 1.0);
    FragColor = vec4(vec3(normalized_val), 1.0);
}
