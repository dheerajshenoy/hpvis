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
#include "colorbar.glsl"
#include "graticule.glsl"

void main() {
    float colat, phi;

    vec2 px = TexCoord * viewport_size;

    // colorbar_rect = (x0, y0, x1, y1) in device pixels, origin bottom-left
    float bar_left  = colorbar_rect.x;
    float bar_y0    = colorbar_rect.y;
    float bar_right = colorbar_rect.z;
    float bar_y1    = colorbar_rect.w;
    float bar_w     = bar_right - bar_left;

    if (colorbar_enabled) {
        if (px.x > bar_left && px.x < bar_right && px.y > bar_y0 && px.y < bar_y1) {
            float t = (px.x - bar_left) / bar_w;   // 0 at the left end, 1 at the right
            FragColor = vec4(applyColormap(t, colormap, invert_colormap), 1.0);
            return;
        }
    }

    // Camera: screen NDC -> zoomed/panned point on the projection plane
    vec2 ndc = (TexCoord - 0.5) * 2.0 * view_scale;
    vec2 p = ndc / zoom + panOffset;
    vec2 viewUV = p * 0.5 + 0.5;

    if (!inverseProjection(viewUV, colat, phi)) {
        vec3 outside = bg_color;
        if (projection_type == PROJ_ORTHOGRAPHIC && globe_shadow) {
            // Globe edge is at r = 1 in this unit; fade a dark halo outwards
            float r = length(viewUV - vec2(0.5)) * 2.0;
            float shadow = 0.45 * exp(-(r - 1.0) * 12.0);
            outside = mix(bg_color, vec3(0.0), shadow);
        }
        FragColor = vec4(outside, 1.0);
        return;
    }

    // Rotate the sky: view-frame direction -> sky direction. atan(length, z)
    // stays accurate near the poles where acos(z) would not.
    vec3 dir = vec3(sin(colat) * cos(phi), sin(colat) * sin(phi), cos(colat));
    dir   = globe_rotation * dir;
    colat = atan(length(dir.xy), dir.z);
    phi   = mod(atan(dir.y, dir.x), 2.0 * PI);


    // colat / phi are in the displayed frame (the graticule uses them); the
    // map is looked up in its own frame
    vec3 ndir = frame_to_native * vec3(sin(colat) * cos(phi),
                                       sin(colat) * sin(phi), cos(colat));
    float ncolat = atan(length(ndir.xy), ndir.z);
    float nphi   = mod(atan(ndir.y, ndir.x), 2.0 * PI);

    int pixIdx = (pixel_ordering == 0) ? ang2pix_ring(nside, ncolat, nphi)
                                       : ang2pix_nest(nside, ncolat, nphi);

    // The map is packed into a 2D texture (1D textures are size-limited).
    // Integer math + texelFetch keeps indices exact for large nside.
    const int W = 4096;
    float mapVal = texelFetch(mapTexture, ivec2(pixIdx % W, pixIdx / W), 0).r;

    vec3 color;

    // HEALPix UNSEEN is -1.6375e30 (finite, not NaN); also catch NaN/Inf.
    if (mapVal < -1.0e30 || isnan(mapVal) || isinf(mapVal)) {
        color = unseen_color;
    } else {
        color = applyColormap(applyScale(mapVal), colormap, invert_colormap);

        if (mask_bits != 0) {
            // Product of the enabled masks: 1 = keep, 0 = masked
            ivec2 mc = ivec2(pixIdx % W, pixIdx / W);
            float keep = 1.0;
            if ((mask_bits & 0x000F) != 0) keep *= maskProduct(maskTex0, mc, mask_bits, mask_invert_bits);
            if ((mask_bits & 0x00F0) != 0) keep *= maskProduct(maskTex1, mc, mask_bits >> 4, mask_invert_bits >> 4);
            if ((mask_bits & 0x0F00) != 0) keep *= maskProduct(maskTex2, mc, mask_bits >> 8, mask_invert_bits >> 8);
            if ((mask_bits & 0xF000) != 0) keep *= maskProduct(maskTex3, mc, mask_bits >> 12, mask_invert_bits >> 12);

            if (mask_mode == 0)
                color = (keep < 0.5) ? unseen_color : color;
            else
                color = mix(color, unseen_color, (1.0 - keep) * mask_opacity);
        }
    }

    if (limb_darkening && projection_type == PROJ_ORTHOGRAPHIC) {
        vec2 q = (viewUV - vec2(0.5)) * 2.0;
        float mu = sqrt(max(0.0, 1.0 - dot(q, q))); // 1 at center, 0 at limb
        color *= 1.0 - 0.55 * (1.0 - mu);
    }

    // Drawn last so the lines also cross unseen / masked pixels
    if (graticule_enabled)
        color = mix(color, graticule_color.rgb,
                    graticule_color.a * graticule(colat, phi));

    FragColor = vec4(color, 1.0);
}
