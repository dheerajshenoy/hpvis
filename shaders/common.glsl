uniform vec2 viewport_size;
uniform vec2 view_scale; // window NDC -> map NDC (inset by the padding)
// One layer per HEALPix base face, nside x nside texels each. Drivers cap the
// size of a single texture (about 1 GB on some), so the 12 faces are spread
// over up to 3 textures of seg_layers faces each.
uniform sampler2DArray mapTexture0;
uniform sampler2DArray mapTexture1;
uniform sampler2DArray mapTexture2;
uniform int seg_layers;
// Streaming (nside above the user's limit): the textures above then hold a
// coarse map, nside >> level_shift, and full-resolution tiles come from the
// atlas, found through the page table (layer of a tile, -1 = not resident)
uniform int level_shift;
uniform bool streaming;
uniform sampler2DArray tileAtlas;
uniform sampler2DArray tilePages;
uniform int nside;
uniform float vmin;
uniform float vmax;
uniform int colormap;
uniform int colormap_scale; // 0 = linear, 1 = log
uniform bool invert_colormap;
// User colormaps: one 256-entry row each; ids >= 15 select row (id - 15)
uniform sampler2D colormapLut;
uniform int lut_rows;
uniform vec3 bg_color;
// Up to 16 masks, packed 4 per RGBA8 texture set (one mask per channel), each
// set split into segments like the map
uniform sampler2DArray maskTex0_0;
uniform sampler2DArray maskTex0_1;
uniform sampler2DArray maskTex0_2;
uniform sampler2DArray maskTex1_0;
uniform sampler2DArray maskTex1_1;
uniform sampler2DArray maskTex1_2;
uniform sampler2DArray maskTex2_0;
uniform sampler2DArray maskTex2_1;
uniform sampler2DArray maskTex2_2;
uniform sampler2DArray maskTex3_0;
uniform sampler2DArray maskTex3_1;
uniform sampler2DArray maskTex3_2;
uniform int mask_bits;       // bit i set = mask i enabled
uniform int mask_invert_bits; // bit i set = mask i inverted (1 - m)
uniform int mask_mode;       // 0 = hide, 1 = dim
uniform float mask_opacity;  // strength of "dim"
uniform float zoom;
uniform vec2 panOffset;
uniform vec3 unseen_color;
uniform bool globe_shadow;   // orthographic: soft shadow outside the globe
uniform mat3 frame_to_native; // displayed frame -> the frame the map is in
uniform bool view_outside;   // seen from the outside: mirror left-right
uniform bool limb_darkening; // orthographic: darken towards the limb

#define PI 3.14159265358979323846

// Opacity of the output; below 1 while one projection fades into another
uniform float out_alpha;
