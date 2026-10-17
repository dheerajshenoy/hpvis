#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

#include "common.glsl"
#include "colormaps.glsl"
#include "healpix.glsl"
#include "projections.glsl"
#include "scale.glsl"
#include "masks.glsl"
#include "graticule.glsl"

// Sky angles in the displayed frame at a point of the projection plane (the
// same steps as in main()); false outside the projection
bool skyAngles(vec2 uv, out float colat, out float phi) {
    if (!inverseProjection(uv, colat, phi))
        return false;
    vec3 dir = vec3(sin(colat) * cos(phi), sin(colat) * sin(phi), cos(colat));
    dir   = globe_rotation * dir;
    colat = atan(length(dir.xy), dir.z);
    phi   = mod(atan(dir.y, dir.x), 2.0 * PI);
    return true;
}

// Change of latitude and longitude over one screen pixel along x and along y
// (summed, like fwidth). A neighbour outside the projection is replaced by
// the one on the other side.
void graticuleDerivatives(vec2 uv, float colat, float phi,
                          out float dLat, out float dPhi) {
    vec2 step = view_scale / (zoom * viewport_size); // plane-uv per pixel
    dLat = 0.0;
    dPhi = 0.0;
    for (int k = 0; k < 2; ++k) {
        vec2 off = (k == 0) ? vec2(step.x, 0.0) : vec2(0.0, step.y);
        float c, p;
        if (!skyAngles(uv + off, c, p) && !skyAngles(uv - off, c, p)) {
            dLat = 10.0; // isolated pixel: no line
            dPhi = 10.0;
            return;
        }
        dLat += abs(c - colat);
        dPhi += abs(mod(p - phi + PI, 2.0 * PI) - PI) ;
    }
}

// Id of the pixel of cell_grid_nside at a point of the projection plane, in
// the frame of the map; -1 outside the projection
int cellGridId(vec2 uv) {
    float colat, phi;
    if (!skyAngles(uv, colat, phi))
        return -1;
    vec3 ndir = frame_to_native * vec3(sin(colat) * cos(phi),
                                       sin(colat) * sin(phi), cos(colat));
    float nc = atan(length(ndir.xy), ndir.z);
    float np = mod(atan(ndir.y, ndir.x), 2.0 * PI);
    ivec3 xyf = ang2xyf(cell_grid_nside, nc, np);
    return (xyf.z * cell_grid_nside + xyf.y) * cell_grid_nside + xyf.x;
}

// 1 on the boundary between two pixels of the grid. Fades out when the pixels
// get smaller than a few screen pixels, where the lines would only smear.
float cellGrid(vec2 uv, float dLat) {
    float cellSize = sqrt(PI / 3.0) / float(cell_grid_nside); // radians
    float fade = smoothstep(2.0, 6.0, cellSize / max(dLat, 1.0e-9));
    if (fade <= 0.0)
        return 0.0;
    vec2 step = view_scale / (zoom * viewport_size);
    int c = cellGridId(uv);
    if (c < 0)
        return 0.0;
    int r = cellGridId(uv + vec2(step.x, 0.0));
    int u = cellGridId(uv + vec2(0.0, step.y));
    bool edge = (r >= 0 && r != c) || (u >= 0 && u != c);
    return edge ? fade : 0.0;
}

void main() {
    float colat, phi;

    vec2 px = TexCoord * viewport_size;

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
        FragColor = vec4(outside, out_alpha);
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

    // One texel per pixel, one array layer per base face
    ivec3 texel = ang2xyf(nside, ncolat, nphi);
    // The resident textures may hold a coarser level
    ivec3 rt    = ivec3(texel.xy >> level_shift, texel.z);
    int seg     = rt.z / seg_layers;
    ivec3 tc    = ivec3(rt.xy, rt.z - seg * seg_layers);
    float mapVal = (seg == 0) ? texelFetch(mapTexture0, tc, 0).r
                 : (seg == 1) ? texelFetch(mapTexture1, tc, 0).r
                              : texelFetch(mapTexture2, tc, 0).r;
    if (streaming) {
        // 512 = TileCache::kTileSize
        float layer = texelFetch(tilePages, ivec3(texel.xy >> 9, texel.z), 0).r;
        if (layer >= 0.0)
            mapVal = texelFetch(tileAtlas, ivec3(texel.xy & 511, int(layer)), 0).r;
    }

    vec3 color;

    // HEALPix UNSEEN is -1.6375e30 (finite, not NaN); also catch NaN/Inf.
    // Threshold must match isSeen() / UNSEEN in include/unseen.hpp.
    if (mapVal < -1.0e30 || isnan(mapVal) || isinf(mapVal)) {
        color = unseen_color;
    } else {
        color = applyColormap(applyScale(mapVal), colormap, invert_colormap);

        if (mask_bits != 0) {
            // Product of the enabled masks: 1 = keep, 0 = masked
            float keep = 1.0;
            if ((mask_bits & 0x000F) != 0) keep *= maskProduct(fetchMask0(rt), mask_bits, mask_invert_bits);
            if ((mask_bits & 0x00F0) != 0) keep *= maskProduct(fetchMask1(rt), mask_bits >> 4, mask_invert_bits >> 4);
            if ((mask_bits & 0x0F00) != 0) keep *= maskProduct(fetchMask2(rt), mask_bits >> 8, mask_invert_bits >> 8);
            if ((mask_bits & 0xF000) != 0) keep *= maskProduct(fetchMask3(rt), mask_bits >> 12, mask_invert_bits >> 12);

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
    if (graticule_enabled) {
        float dLat, dPhi;
        graticuleDerivatives(viewUV, colat, phi, dLat, dPhi);
        color = mix(color, graticule_color.rgb,
                    graticule_color.a * graticule(colat, phi, dLat, dPhi));
    }

    if (cell_grid_enabled) {
        float dLat, dPhi;
        graticuleDerivatives(viewUV, colat, phi, dLat, dPhi);
        color = mix(color, cell_grid_color.rgb,
                    cell_grid_color.a * cellGrid(viewUV, dLat));
    }

    FragColor = vec4(color, out_alpha);
}
