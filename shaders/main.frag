#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

#include "common.glsl"
#include "colormaps.glsl"
#include "healpix_ring.glsl"
#include "healpix_nest.glsl"
#include "projections.glsl"
#include "scale.glsl"

void main() {
    float colat, phi;
    if (!inverseMollweide(TexCoord, colat, phi)) {
        FragColor = vec4(0.04, 0.04, 0.06, 1.0); // background outside
        return;
    }

    int pixIdx = (pixel_ordering == 0) ? ang2pix_ring(nside, colat, phi)
                                       : ang2pix_nest(nside, colat, phi);

    // The map is packed into a 2D texture (1D textures are size-limited).
    // Integer math + texelFetch keeps indices exact for large nside.
    const int W = 4096;
    float mapVal = texelFetch(mapTexture, ivec2(pixIdx % W, pixIdx / W), 0).r;
    float t = applyScale(mapVal);
    FragColor = vec4(applyColormap(t, colormap), 1.0);
}
