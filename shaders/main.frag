#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

#include "common.glsl"
#include "colormaps.glsl"
#include "healpix_ring.glsl"
#include "healpix_nest.glsl"
#include "projections.glsl"

void main() {
    float colat, phi;
    if (!inverseMollweide(TexCoord, colat, phi)) {
        FragColor = vec4(0.04, 0.04, 0.06, 1.0); // background outside
        return;
    }

    float pixIdx = (pixel_ordering == 0) ? ang2pix_ring(nside, colat, phi)
                                         : ang2pix_nest(nside, colat, phi);

    // The map is packed into a 2D texture (1D textures are size-limited)
    float W = 4096.0;
    float H = ceil(npix / W);
    vec2 uv = vec2((mod(pixIdx, W) + 0.5) / W, (floor(pixIdx / W) + 0.5) / H);
    float mapVal = texture(mapTexture, uv).r;
    float t = clamp((mapVal - vmin) / (vmax - vmin), 0.0, 1.0);
    FragColor = vec4(applyColormap(t, colormap), 1.0);
}
