#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

#include "common.glsl"
#include "colormaps.glsl"
#include "healpix_ring.glsl"
#include "healpix_nest.glsl"
#include "projections.glsl"
#include "scale.glsl"
#include "masks.glsl"

void main() {
    float colat, phi;

    // Camera: screen NDC -> zoomed/panned point on the projection plane
    vec2 p = (TexCoord - 0.5) * 2.0 / zoom + panOffset;
    vec2 viewUV = p * 0.5 + 0.5;

    if (!inverseMollweide(viewUV, colat, phi)) {
        FragColor = vec4(bg_color, 1.0); // background outside
        return;
    }

    int pixIdx = (pixel_ordering == 0) ? ang2pix_ring(nside, colat, phi)
                                       : ang2pix_nest(nside, colat, phi);

    // The map is packed into a 2D texture (1D textures are size-limited).
    // Integer math + texelFetch keeps indices exact for large nside.
    const int W = 4096;
    float mapVal = texelFetch(mapTexture, ivec2(pixIdx % W, pixIdx / W), 0).r;

    // HEALPix UNSEEN is -1.6375e30 (finite, not NaN); also catch NaN/Inf.
    if (mapVal < -1.0e30 || isnan(mapVal) || isinf(mapVal)) {
        FragColor = vec4(unseen_color, 1.0);
        return;
    }

    float t = applyScale(mapVal);
    vec3 color = applyColormap(t, colormap, invert_colormap);

    if (mask_bits != 0) {
        // Product of the enabled masks: 1 = keep, 0 = masked
        ivec2 mc = ivec2(pixIdx % W, pixIdx / W);
        float keep = 1.0;
        if ((mask_bits & 0x000F) != 0) keep *= maskProduct(maskTex0, mc, mask_bits);
        if ((mask_bits & 0x00F0) != 0) keep *= maskProduct(maskTex1, mc, mask_bits >> 4);
        if ((mask_bits & 0x0F00) != 0) keep *= maskProduct(maskTex2, mc, mask_bits >> 8);
        if ((mask_bits & 0xF000) != 0) keep *= maskProduct(maskTex3, mc, mask_bits >> 12);

        if (mask_mode == 0)
            color = (keep < 0.5) ? unseen_color : color;
        else
            color = mix(color, unseen_color, (1.0 - keep) * mask_opacity);
    }
    FragColor = vec4(color, 1.0);
}